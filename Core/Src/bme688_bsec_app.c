/**
 * BME688 BSEC2 AI 算法 FreeRTOS 调度与集成文件
 * 
 * [升级记录]: 
 * 1. 纯净版：修复了宏定义冲突与语法符号问题
 * 2. 架构升级：数据中枢化，汇总入全局 sysData。
 * 3. 业务替换：现已切换为 [食物腐败与冰箱保鲜] 模型！
 */

#include "cmsis_os2.h"
#include "bsec_interface.h"
#include "bsec_datatypes.h"
#include "bme68x.h"
#include "bme688_port.h"
#include "system_data.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

// BSEC 配置数组及其真实长度由 bsec_config.c 统一导出。
extern const uint8_t bsec_food_spoilage[];
extern const uint32_t bsec_food_spoilage_size;

static uint8_t bsec_work_buffer[BSEC_MAX_WORKBUFFER_SIZE];

extern struct bme68x_dev bme_dev;
extern SystemData_t sysData;  

static int64_t BSEC_Get_Timestamp_ns(void)
{
    return (int64_t)HAL_GetTick() * 1000000;
}

static void BSEC_Process_Data(const bsec_input_t *inputs, uint8_t n_inputs, const bsec_output_t *outputs, uint8_t n_outputs)
{
    float iaq_index = sysData.bme688.iaq_index;
    float eco2 = sysData.bme688.eco2;
    float temp = sysData.bme688.temp;
    float hum = sysData.bme688.hum;
    float food_spoilage_risk = sysData.bme688.food_spoilage_risk;
    uint8_t accuracy = sysData.bme688.accuracy;

    (void)inputs;
    (void)n_inputs;

    for (uint8_t i = 0; i < n_outputs; i++)
    {
        switch (outputs[i].sensor_id)
        {
            case BSEC_OUTPUT_IAQ:
                iaq_index = outputs[i].signal;
                accuracy = outputs[i].accuracy;
                break;
            case BSEC_OUTPUT_CO2_EQUIVALENT:
                eco2 = outputs[i].signal;
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                temp = outputs[i].signal;
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                hum = outputs[i].signal;
                break;
            case BSEC_OUTPUT_GAS_ESTIMATE_1:
                food_spoilage_risk = outputs[i].signal;
                break;
            default:
                break;
        }
    }

    /*
     * BSEC 可能在一轮中更新多个虚拟传感器，计算完成后统一发布，
     * 避免 USB/电子鼻任务读到一半新、一半旧的算法结果。
     */
    taskENTER_CRITICAL();
    sysData.bme688.iaq_index = iaq_index;
    sysData.bme688.eco2 = eco2;
    sysData.bme688.temp = temp;
    sysData.bme688.hum = hum;
    sysData.bme688.food_spoilage_risk = food_spoilage_risk;
    sysData.bme688.accuracy = accuracy;
    taskEXIT_CRITICAL();
}

void BME688_BSEC_Task(void *argument)
{
    (void)argument;
    bsec_bme_settings_t bme_conf;
    struct bme68x_conf hw_conf;
    struct bme68x_heatr_conf heatr_conf;

    bsec_library_return_t bsec_status = bsec_init();
    sysData.bme688.algorithm_status = (int16_t)bsec_status;
    if (bsec_status < BSEC_OK) {
        sysData.bme688.status = -3;
        for (;;) {
            osDelay(1000);
        }
    }

    // 将食物腐败配置送入算法核心，长度不再硬编码。
    bsec_status = bsec_set_configuration(
        bsec_food_spoilage,
        bsec_food_spoilage_size,
        bsec_work_buffer,
        sizeof(bsec_work_buffer)
    );
    sysData.bme688.algorithm_status = (int16_t)bsec_status;
    if (bsec_status < BSEC_OK) {
        sysData.bme688.status = -4;
        for (;;) {
            osDelay(1000);
        }
    }

    bsec_sensor_configuration_t requested_virtual_sensors[5];
    uint8_t n_requested = 5;

    requested_virtual_sensors[0].sensor_id = BSEC_OUTPUT_IAQ;
    requested_virtual_sensors[0].sample_rate = BSEC_SAMPLE_RATE_SCAN;
    requested_virtual_sensors[1].sensor_id = BSEC_OUTPUT_CO2_EQUIVALENT;
    requested_virtual_sensors[1].sample_rate = BSEC_SAMPLE_RATE_SCAN;
    requested_virtual_sensors[2].sensor_id = BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE;
    requested_virtual_sensors[2].sample_rate = BSEC_SAMPLE_RATE_SCAN;
    requested_virtual_sensors[3].sensor_id = BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY;
    requested_virtual_sensors[3].sample_rate = BSEC_SAMPLE_RATE_SCAN;
    requested_virtual_sensors[4].sensor_id = BSEC_OUTPUT_GAS_ESTIMATE_1; 
    requested_virtual_sensors[4].sample_rate = BSEC_SAMPLE_RATE_SCAN;

    bsec_sensor_configuration_t required_sensor_settings[BSEC_MAX_PHYSICAL_SENSOR];
    uint8_t n_required = BSEC_MAX_PHYSICAL_SENSOR;

    bsec_status = bsec_update_subscription(
        requested_virtual_sensors,
        n_requested,
        required_sensor_settings,
        &n_required
    );
    sysData.bme688.algorithm_status = (int16_t)bsec_status;
    if (bsec_status < BSEC_OK) {
        sysData.bme688.status = -5;
        for (;;) {
            osDelay(1000);
        }
    }

    while (1)
    {
        int64_t time_stamp = BSEC_Get_Timestamp_ns();

        bsec_status = bsec_sensor_control(time_stamp, &bme_conf);
        sysData.bme688.algorithm_status = (int16_t)bsec_status;
        if (bsec_status < BSEC_OK) {
            sysData.bme688.status = -6;
            osDelay(1000);
            continue;
        }

        if (bme_conf.trigger_measurement)
        {
            int8_t bme_status;
            uint32_t measurement_duration_us;
            uint32_t measurement_duration_ms;

            hw_conf.os_hum = bme_conf.humidity_oversampling;
            hw_conf.os_temp = bme_conf.temperature_oversampling;
            hw_conf.os_pres = bme_conf.pressure_oversampling;
            hw_conf.filter = BME68X_FILTER_SIZE_3;
            hw_conf.odr = BME68X_ODR_NONE;
            
            heatr_conf.enable = bme_conf.run_gas ? BME68X_ENABLE : BME68X_DISABLE;
            heatr_conf.heatr_temp = bme_conf.heater_temperature;
            heatr_conf.heatr_dur = bme_conf.heater_duration;
            heatr_conf.heatr_temp_prof = bme_conf.heater_temperature_profile;
            heatr_conf.heatr_dur_prof = bme_conf.heater_duration_profile;
            heatr_conf.profile_len = bme_conf.heater_profile_len;
            measurement_duration_us =
                bme68x_get_meas_dur(bme_conf.op_mode, &hw_conf, &bme_dev);
            measurement_duration_ms = measurement_duration_us / 1000U;
            /*
             * shared_heatr_dur 是无符号值。测量时间超过 140 ms 时必须
             * 钳到 0，否则减法下溢会变成一个极长的加热/阻塞时间。
             */
            heatr_conf.shared_heatr_dur =
                measurement_duration_ms < 140U
                    ? (uint16_t)(140U - measurement_duration_ms)
                    : 0U;

            /*
             * Bosch read/write 回调内部已经逐次获取 i2c_mutex。
             * 此处绝不能再套同一个非递归 mutex，否则任务会自锁。
             */
            bme_status = bme68x_set_conf(&hw_conf, &bme_dev);
            if (bme_status == BME68X_OK) {
                bme_status = bme68x_set_heatr_conf(
                    bme_conf.op_mode,
                    &heatr_conf,
                    &bme_dev
                );
            }
            if (bme_status == BME68X_OK) {
                bme_status =
                    bme68x_set_op_mode(bme_conf.op_mode, &bme_dev);
            }
            if (bme_status != BME68X_OK) {
                sysData.bme688.status = -1;
                osDelay(100U);
                continue;
            }

            uint32_t delay_ms =
                measurement_duration_ms + heatr_conf.shared_heatr_dur;
            if (delay_ms == 0U) {
                delay_ms = 1U;
            }
            osDelay(delay_ms);

            struct bme68x_data raw_data[3];
            uint8_t n_fields = 0;

            bme_status = bme68x_get_data(
                bme_conf.op_mode,
                raw_data,
                &n_fields,
                &bme_dev
            );

            if (bme_status == BME68X_OK && n_fields > 0)
            {
                /*
                 * 原始气阻是 Cube.AI 的输入，不应等待 IAQ accuracy>0 才有效。
                 * 但必须同时检查 Bosch 驱动的 gas-valid 位，禁止把全 0 假数据
                 * 标成 status=1 写入历史 Flash。
                 */
                bool raw_gas_valid =
                    (raw_data[0].status & BME68X_GASM_VALID_MSK) != 0U &&
                    raw_data[0].gas_resistance > 0.0f;
                taskENTER_CRITICAL();
                sysData.bme688.temp = raw_data[0].temperature;
                sysData.bme688.hum = raw_data[0].humidity;
                sysData.bme688.press = raw_data[0].pressure / 100.0f;
                sysData.bme688.gas_res = raw_data[0].gas_resistance;
                sysData.bme688.status = raw_gas_valid ? 1 : -2;
                taskEXIT_CRITICAL();

                bsec_input_t bsec_inputs[BSEC_MAX_PHYSICAL_SENSOR];
                uint8_t n_bsec_inputs = 0;
                bsec_output_t bsec_outputs[BSEC_NUMBER_OUTPUTS];

                for (uint8_t i = 0; i < n_fields; i++)
                {
                    uint8_t n_bsec_outputs = BSEC_NUMBER_OUTPUTS;
                    n_bsec_inputs = 0;
                    bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_TEMPERATURE;
                    bsec_inputs[n_bsec_inputs].signal = raw_data[i].temperature;
                    bsec_inputs[n_bsec_inputs].time_stamp = time_stamp;
                    n_bsec_inputs++;
                    
                    bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_HUMIDITY;
                    bsec_inputs[n_bsec_inputs].signal = raw_data[i].humidity;
                    bsec_inputs[n_bsec_inputs].time_stamp = time_stamp;
                    n_bsec_inputs++;
                    
                    bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_PRESSURE;
                    bsec_inputs[n_bsec_inputs].signal = raw_data[i].pressure;
                    bsec_inputs[n_bsec_inputs].time_stamp = time_stamp;
                    n_bsec_inputs++;
                    
                    if (raw_data[i].status & BME68X_GASM_VALID_MSK)
                    {
                        bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_GASRESISTOR;
                        bsec_inputs[n_bsec_inputs].signal = raw_data[i].gas_resistance;
                        bsec_inputs[n_bsec_inputs].time_stamp = time_stamp;
                        n_bsec_inputs++;
                        
                        bsec_inputs[n_bsec_inputs].sensor_id = BSEC_INPUT_PROFILE_PART;
                        bsec_inputs[n_bsec_inputs].signal = raw_data[i].gas_index;
                        bsec_inputs[n_bsec_inputs].time_stamp = time_stamp;
                        n_bsec_inputs++;
                    }

                    bsec_status = bsec_do_steps(
                        bsec_inputs,
                        n_bsec_inputs,
                        bsec_outputs,
                        &n_bsec_outputs
                    );
                    sysData.bme688.algorithm_status =
                        (int16_t)bsec_status;

                    if (bsec_status >= BSEC_OK && n_bsec_outputs > 0)
                    {
                        BSEC_Process_Data(bsec_inputs, n_bsec_inputs, bsec_outputs, n_bsec_outputs);
                    }
                }
            }
            else 
            {
                sysData.bme688.status = -1;
            }
        }
        else
        {
            int64_t wait_ns = bme_conf.next_call - time_stamp;
            /*
             * next_call 已经过期时不能先转 uint32_t，否则负数会变成
             * 数十亿毫秒。至少让出一个 tick 后立即重新询问 BSEC。
             */
            uint32_t wait_ms =
                wait_ns > 0 ? (uint32_t)(wait_ns / 1000000) : 1U;
            osDelay(wait_ms > 0U ? wait_ms : 1U);
        }
    }
}
