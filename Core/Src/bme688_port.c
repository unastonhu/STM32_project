/*
*
*@file    bme688_port.c
*@brief   BME688 传感器底层 I2C 通信与业务逻辑适配文件
*[修改记录]
*初始版本：实现基于 STM32 HAL 和 FreeRTOS 的基础通信接口。
*BSEC算法融合：移除全局变量 bme_dev 的 static 声明。
*线程安全升级：引入 I2C 互斥锁 (i2c_mutex)，彻底杜绝多任务抢占 I2C 总线导致的死机。
*/

#include "bme688_port.h"
#include "bme68x.h"
#include "cmsis_os2.h"

// [新增]: 引入外部定义好的 I2C 公共钥匙（互斥锁）
extern osMutexId_t i2c_mutex;

static I2C_HandleTypeDef *bme_i2c = NULL;

// BME688 硬件设备结构体
struct bme68x_dev bme_dev;

static struct bme68x_conf bme_conf;
static struct bme68x_heatr_conf bme_heatr_conf;

static uint8_t dev_addr = BME68X_I2C_ADDR_HIGH;

// ==========================================
// 底层硬件适配层 (STM32 HAL + FreeRTOS)
// ==========================================

/*
*@brief  I2C 读总线数据 (带互斥锁保护)
*/
BME68X_INTF_RET_TYPE bme68x_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
uint8_t addr = (uint8_t)intf_ptr;
HAL_StatusTypeDef status;

// 1. 拿钥匙锁门 (如果别人在用，这里会自动等待)
osMutexAcquire(i2c_mutex, osWaitForever);

// 2. 独占使用 I2C 总线
status = HAL_I2C_Mem_Read(bme_i2c, addr << 1, reg_addr, I2C_MEMADD_SIZE_8BIT, reg_data, len, 100);

// 3. 用完开门交钥匙
osMutexRelease(i2c_mutex);

if (status == HAL_OK) return BME68X_OK;
return BME68X_E_COM_FAIL;
}

/*
*@brief  I2C 写总线数据 (带互斥锁保护)
*/
BME68X_INTF_RET_TYPE bme68x_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
uint8_t addr = (uint8_t)intf_ptr;
HAL_StatusTypeDef status;

// 1. 拿钥匙锁门
osMutexAcquire(i2c_mutex, osWaitForever);

// 2. 独占使用 I2C 总线
status = HAL_I2C_Mem_Write(bme_i2c, addr << 1, reg_addr, I2C_MEMADD_SIZE_8BIT, (uint8_t*)reg_data, len, 100);

// 3. 用完开门交钥匙
osMutexRelease(i2c_mutex);

if (status == HAL_OK) return BME68X_OK;
return BME68X_E_COM_FAIL;
}

/*
*@brief  微秒级延时函数
*/
void bme68x_delay_us(uint32_t period, void *intf_ptr) {
(void)intf_ptr;
uint32_t ms = (period / 1000) + 1;
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

if (bme68x_init(&bme_dev) != BME68X_OK) return -1;

bme_conf.filter  = BME68X_FILTER_SIZE_3;
bme_conf.odr     = BME68X_ODR_NONE;
bme_conf.os_hum  = BME68X_OS_16X;
bme_conf.os_pres = BME68X_OS_1X;
bme_conf.os_temp = BME68X_OS_2X;
bme68x_set_conf(&bme_conf, &bme_dev);

bme_heatr_conf.enable     = BME68X_ENABLE;
bme_heatr_conf.heatr_temp = 300;
bme_heatr_conf.heatr_dur  = 100;
bme68x_set_heatr_conf(BME68X_FORCED_MODE, &bme_heatr_conf, &bme_dev);

return 1;


}

int8_t BME688_Port_Read(float *temp, float *hum, float *press, float *gas_res) {
struct bme68x_data data;
uint8_t n_fields;

if (bme68x_set_op_mode(BME68X_FORCED_MODE, &bme_dev) != BME68X_OK) return -1;

uint32_t del_period = bme68x_get_meas_dur(BME68X_FORCED_MODE, &bme_conf, &bme_dev) + (bme_heatr_conf.heatr_dur * 1000);
bme_dev.delay_us(del_period, bme_dev.intf_ptr);

if (bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &bme_dev) == BME68X_OK && n_fields > 0) {
    *temp    = data.temperature;
    *hum     = data.humidity;
    *press   = data.pressure / 100.0f; 
    *gas_res = data.gas_resistance;    
    return 1;
}
return -1;


}
