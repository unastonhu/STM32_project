#include "bme688_port.h"
#include "bme68x.h"
#include "cmsis_os2.h"

static I2C_HandleTypeDef *bme_i2c = NULL;
static struct bme68x_dev bme_dev;
static struct bme68x_conf bme_conf;
static struct bme68x_heatr_conf bme_heatr_conf;

// I2C硬件地址：如果你读不到数据，大概率是引脚电平问题，把这里改成 BME68X_I2C_ADDR_HIGH (0x77)
static uint8_t dev_addr = BME68X_I2C_ADDR_HIGH; 

// ==========================================
// 底层硬件适配层 (STM32 HAL + FreeRTOS)
// ==========================================
BME68X_INTF_RET_TYPE bme68x_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    uint8_t addr = *(uint8_t*)intf_ptr;
    // 注意：STM32 HAL 要求地址左移 1 位
    if (HAL_I2C_Mem_Read(bme_i2c, addr << 1, reg_addr, I2C_MEMADD_SIZE_8BIT, reg_data, len, 100) == HAL_OK)
        return BME68X_OK;
    return BME68X_E_COM_FAIL;
}

BME68X_INTF_RET_TYPE bme68x_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    uint8_t addr = *(uint8_t*)intf_ptr;
    if (HAL_I2C_Mem_Write(bme_i2c, addr << 1, reg_addr, I2C_MEMADD_SIZE_8BIT, (uint8_t*)reg_data, len, 100) == HAL_OK)
        return BME68X_OK;
    return BME68X_E_COM_FAIL;
}

void bme68x_delay_us(uint32_t period, void *intf_ptr) {
    (void)intf_ptr;
    uint32_t ms = (period / 1000) + 1; // 微秒转毫秒，向上取整保证延时足够
    osDelay(ms);
}

// ==========================================
// 业务逻辑层
// ==========================================
int8_t BME688_Port_Init(I2C_HandleTypeDef *hi2c) {
    bme_i2c = hi2c;

    bme_dev.read     = bme68x_i2c_read;
    bme_dev.write    = bme68x_i2c_write;
    bme_dev.delay_us = bme68x_delay_us;
    bme_dev.intf     = BME68X_I2C_INTF;
    bme_dev.intf_ptr = &dev_addr;
    bme_dev.amb_temp = 25; 

    // 初始化内核
    if (bme68x_init(&bme_dev) != BME68X_OK) return -1;

    // 配置过采样率和滤波器
    bme_conf.filter  = BME68X_FILTER_SIZE_3;
    bme_conf.odr     = BME68X_ODR_NONE;
    bme_conf.os_hum  = BME68X_OS_16X;
    bme_conf.os_pres = BME68X_OS_1X;
    bme_conf.os_temp = BME68X_OS_2X;
    bme68x_set_conf(&bme_conf, &bme_dev);

    // 配置 300°C 气体加热板 (持续 100ms)
    bme_heatr_conf.enable     = BME68X_ENABLE;
    bme_heatr_conf.heatr_temp = 300;
    bme_heatr_conf.heatr_dur  = 100;
    bme68x_set_heatr_conf(BME68X_FORCED_MODE, &bme_heatr_conf, &bme_dev);

    return 1;
}

int8_t BME688_Port_Read(float *temp, float *hum, float *press, float *gas_res) {
    struct bme68x_data data;
    uint8_t n_fields;

    // 1. 发送触发指令
    if (bme68x_set_op_mode(BME68X_FORCED_MODE, &bme_dev) != BME68X_OK) return -1;

    // 2. 自动计算需要等待的时间并挂起任务
    uint32_t del_period = bme68x_get_meas_dur(BME68X_FORCED_MODE, &bme_conf, &bme_dev) + (bme_heatr_conf.heatr_dur * 1000);
    bme_dev.delay_us(del_period, bme_dev.intf_ptr);

    // 3. 读取结果
    if (bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &bme_dev) == BME68X_OK && n_fields > 0) {
        *temp    = data.temperature;
        *hum     = data.humidity;
        *press   = data.pressure / 100.0f; // 换算为百帕(hPa)
        *gas_res = data.gas_resistance;    // 气体阻值(欧姆)
        return 1;
    }
    return -1;
}
