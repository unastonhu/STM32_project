#include "ens160_aht21.h"
#include "cmsis_os2.h"

// 硬件 I2C 地址 (适配 STM32 HAL 左移 1 位)
#define AHT21_ADDR  (0x38 << 1) 
#define ENS160_ADDR (0x52 << 1) // 如果读不到 ENS160 数据，请改为 (0x53 << 1)

// ENS160 寄存器地址
#define ENS160_REG_OPMODE       0x10
#define ENS160_REG_COMMAND      0x12
#define ENS160_REG_TEMP_IN      0x13
#define ENS160_REG_DATA_STATUS  0x20
#define ENS160_REG_DATA_AQI     0x21

static I2C_HandleTypeDef *env_i2c = NULL;

// ==========================================
// 1. 初始化模块 (AHT21 和 ENS160)
// ==========================================
void ENV_Module_Init(I2C_HandleTypeDef *hi2c) {
    env_i2c = hi2c;
    uint8_t val;
    osDelay(50); // 上电等待芯片稳定

    // --- AHT21 初始化 (使用 AHT21 专属的 0xBE 指令) ---
    uint8_t aht_init[3] = {0xBE, 0x08, 0x00};
    HAL_I2C_Master_Transmit(env_i2c, AHT21_ADDR, aht_init, 3, 100);

    // --- ENS160 初始化 ---
    // 1. 设置为 Idle 模式 (0x01)
    val = 0x01;
    HAL_I2C_Mem_Write(env_i2c, ENS160_ADDR, ENS160_REG_OPMODE, 1, &val, 1, 100);
    
    // 2. 清除以前的错误状态 (0xCC)
    val = 0xCC;
    HAL_I2C_Mem_Write(env_i2c, ENS160_ADDR, ENS160_REG_COMMAND, 1, &val, 1, 100);
    osDelay(20);

    // 3. 进入 Standard 标准工作模式 (0x02)
    val = 0x02;
    HAL_I2C_Mem_Write(env_i2c, ENS160_ADDR, ENS160_REG_OPMODE, 1, &val, 1, 100);
    osDelay(50);
}

// ==========================================
// 2. 内部函数：向 ENS160 注入温湿度
// ==========================================
static void ENS160_SetEnvironment(float temp, float hum) {
    uint16_t tval = (uint16_t)((temp + 273.15f) * 64.0f);
    uint16_t hval = (uint16_t)(hum * 512.0f);
    
    uint8_t trh_in[4];
    trh_in[0] = tval & 0xFF;
    trh_in[1] = (tval >> 8) & 0xFF;
    trh_in[2] = hval & 0xFF;
    trh_in[3] = (hval >> 8) & 0xFF;
    
    HAL_I2C_Mem_Write(env_i2c, ENS160_ADDR, ENS160_REG_TEMP_IN, 1, trh_in, 4, 100);
}

// ==========================================
// 3. 终极一键读取函数 (完美数据融合)
// ==========================================
int8_t ENV_Module_ReadAll(float *temp, float *hum, uint16_t *tvoc, uint16_t *eco2, uint8_t *aqi) {
    
    // ------- 第一步：触发并读取 AHT21 -------
    uint8_t aht_cmd[3] = {0xAC, 0x33, 0x00}; 
    if (HAL_I2C_Master_Transmit(env_i2c, AHT21_ADDR, aht_cmd, 3, 100) != HAL_OK) return -1;
    
    osDelay(80); // AHT21 官方手册要求等待测量完成
    
    uint8_t aht_buf[6];
    if (HAL_I2C_Master_Receive(env_i2c, AHT21_ADDR, aht_buf, 6, 100) != HAL_OK) return -1;
    
    // 检查 AHT21 状态位 (借鉴了你上传代码里的最高位判断)
    if ((aht_buf[0] & 0x80) != 0) return -1; 
    
    // 提取自 Thinary_AHT_Sensor 的经典位运算核心
    uint32_t hum_raw = ((uint32_t)aht_buf[1] << 12) | ((uint32_t)aht_buf[2] << 4) | (aht_buf[3] >> 4);
    uint32_t temp_raw = (((uint32_t)aht_buf[3] & 0x0F) << 16) | ((uint32_t)aht_buf[4] << 8) | aht_buf[5];
    
    *hum = ((float)hum_raw / 1048576.0f) * 100.0f;
    *temp = ((float)temp_raw / 1048576.0f) * 200.0f - 50.0f;

    // ------- 第二步：喂食 ENS160 -------
    ENS160_SetEnvironment(*temp, *hum);

    // ------- 第三步：读取 ENS160 解算结果 -------
    uint8_t status;
    HAL_I2C_Mem_Read(env_i2c, ENS160_ADDR, ENS160_REG_DATA_STATUS, 1, &status, 1, 100);
    
    // 检查 STAT_NEWDAT 位 (Bit 1)，只有新数据算好了才去读
    if (status & 0x02) {
        uint8_t ens_buf[7];
        // 高效连读法：从 0x21 开始，一口气读出 AQI(1), TVOC(2), eCO2(2)
        HAL_I2C_Mem_Read(env_i2c, ENS160_ADDR, ENS160_REG_DATA_AQI, 1, ens_buf, 7, 100);
        
        *aqi = ens_buf[0];
        *tvoc = ens_buf[1] | (ens_buf[2] << 8);
        *eco2 = ens_buf[3] | (ens_buf[4] << 8);
        return 1;
    }
    
    return -2; // 数据还在算，让主循环下一秒再来拿
}
