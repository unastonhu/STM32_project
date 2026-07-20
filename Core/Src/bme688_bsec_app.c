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
#include <stdio.h>

// [修改 1]: 引入新的食物腐败模型数组
// ⚠️ 注意：这里的 1947 需要改成你实际生成的食物模型数组大小！
extern const uint8_t bsec_config_food_spoilage[1947]; 

static uint8_t bsec_work_buffer[BSEC_MAX_WORKBUFFER_SIZE];

extern struct bme68x_dev bme_dev;
extern osMutexId_t i2c_mutex; 
extern SystemData_t sysData;  

static int64_t BSEC_Get_Timestamp_ns(void)
{
    return (int64_t)HAL_GetTick() * 1000000;
}

static void BSEC_Process_Data(const bsec_input_t *inputs, uint8_t n_inputs, const bsec_output_t *outputs, uint8_t n_outputs)
{
    for (uint8_t i = 0; i < n_outputs; i++)
    {
        switch (outputs[i].sensor_id)
        {
            case BSEC_OUTPUT_IAQ:
                sysData.bme688.iaq_index = outputs[i].signal;
                if (outputs[i].accuracy > 0) sysData.bme688.status = 1; 
                break;
            case BSEC_OUTPUT_CO2_EQUIVALENT:
                sysData.bme688.eco2 = outputs[i].signal;
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                sysData.bme688.temp = outputs[i].signal;
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                sysData.bme688.hum = outputs[i].signal;
                break;
            case BSEC_OUTPUT_GAS_ESTIMATE_1:
                // [修改 2]: 这里的数据现在代表“食物腐败变质的概率”
                sysData.bme688.food_spoilage_risk = outputs[i].signal; 
                break;
            default:
                break;
        }
    }
}

void BME688_BSEC_Task(void *argument)
{
    (void)argument;
    bsec_bme_settings_t bme_conf;
    struct bme68x_conf hw_conf;
    struct bme68x_heatr_conf heatr_conf;

    bsec_init();

    // [修改 3]: 将新的食物腐败模型送入算法核心
    // ⚠️ 注意：这里的 1947 同样需要改成你实际生成的食物模型数组大小！
    bsec_set_configuration(bsec_config_food_spoilage, 1947, bsec_work_buffer, sizeof(bsec_work_buffer));

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

    bsec_update_subscription(requested_virtual_sensors, n_requested, required_sensor_settings, &n_required);

    while (1)
    {
        int64_t time_stamp = BSEC_Get_Timestamp_ns();

        bsec_sensor_control(time_stamp, &bme_conf);

        if (bme_conf.trigger_measurement)
        {
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
            heatr_conf.shared_heatr_dur = 140 - (bme68x_get_meas_dur(bme_conf.op_mode, &hw_conf, &bme_dev) / 1000); 

            osMutexAcquire(i2c_mutex, osWaitForever);
            bme68x_set_conf(&hw_conf, &bme_dev);
            bme68x_set_heatr_conf(bme_conf.op_mode, &heatr_conf, &bme_dev);
            bme68x_set_op_mode(bme_conf.op_mode, &bme_dev);
            osMutexRelease(i2c_mutex);

            uint32_t delay_ms = (uint32_t)bme68x_get_meas_dur(bme_conf.op_mode, &hw_conf, &bme_dev) / 1000;
            delay_ms += (heatr_conf.shared_heatr_dur);
            osDelay(delay_ms);

            struct bme68x_data raw_data[3];
            uint8_t n_fields = 0;

            osMutexAcquire(i2c_mutex, osWaitForever);
            bme68x_get_data(bme_conf.op_mode, raw_data, &n_fields, &bme_dev);
            osMutexRelease(i2c_mutex);

            if (n_fields > 0)
            {
                sysData.bme688.press = raw_data[0].pressure / 100.0f;
                sysData.bme688.gas_res = raw_data[0].gas_resistance;

                bsec_input_t bsec_inputs[BSEC_MAX_PHYSICAL_SENSOR];
                uint8_t n_bsec_inputs = 0;
                bsec_output_t bsec_outputs[BSEC_NUMBER_OUTPUTS];
                uint8_t n_bsec_outputs = BSEC_NUMBER_OUTPUTS;

                for (uint8_t i = 0; i < n_fields; i++)
                {
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

                    bsec_do_steps(bsec_inputs, n_bsec_inputs, bsec_outputs, &n_bsec_outputs);

                    if (n_bsec_outputs > 0)
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
            uint32_t wait_ms = (uint32_t)((bme_conf.next_call - time_stamp) / 1000000);
            if(wait_ms > 0) {
                osDelay(wait_ms);
            }
        }
    }
}
