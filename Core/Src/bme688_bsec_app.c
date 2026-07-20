/*

BME688 BSEC2 AI 算法 FreeRTOS 调度与集成文件

纯净版：修复了宏定义冲突与语法符号问题
*/


#include "cmsis_os2.h"
#include "bsec_interface.h"
#include "bsec_datatypes.h"
#include "bme68x.h"
#include "bme688_port.h"
#include "system_data.h"
#include <stdio.h>

extern const uint8_t bsec_config_selectivity[1947];

/* 直接使用 bsec_datatypes.h 中定义的 4096 大小 */
static uint8_t bsec_work_buffer[BSEC_MAX_WORKBUFFER_SIZE];

extern struct bme68x_dev bme_dev;

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
printf("[BSEC] IAQ 质量指数: %.2f (准确度: %d)\r\n", outputs[i].signal, outputs[i].accuracy);
break;
case BSEC_OUTPUT_CO2_EQUIVALENT:
printf("[BSEC] eCO2 当量: %.2f ppm\r\n", outputs[i].signal);
break;
case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
printf("[BSEC] 真实温度: %.2f *C\r\n", outputs[i].signal);
break;
case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
printf("[BSEC] 真实湿度: %.2f %%\r\n", outputs[i].signal);
break;
case BSEC_OUTPUT_GAS_ESTIMATE_1:
printf("!! AI 异味/硫化物检测率: %.2f %%\r\n", outputs[i].signal * 100.0f);
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

bsec_set_configuration(bsec_config_selectivity, 1947, bsec_work_buffer, sizeof(bsec_work_buffer));

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
        bme68x_set_conf(&hw_conf, &bme_dev);

        heatr_conf.enable = bme_conf.run_gas ? BME68X_ENABLE : BME68X_DISABLE;
        heatr_conf.heatr_temp = bme_conf.heater_temperature;
        heatr_conf.heatr_dur = bme_conf.heater_duration;
        heatr_conf.heatr_temp_prof = bme_conf.heater_temperature_profile;
        heatr_conf.heatr_dur_prof = bme_conf.heater_duration_profile;
        heatr_conf.profile_len = bme_conf.heater_profile_len;
        heatr_conf.shared_heatr_dur = 140 - (bme68x_get_meas_dur(bme_conf.op_mode, &hw_conf, &bme_dev) / 1000); 

        bme68x_set_heatr_conf(bme_conf.op_mode, &heatr_conf, &bme_dev);

        bme68x_set_op_mode(bme_conf.op_mode, &bme_dev);

        uint32_t delay_ms = (uint32_t)bme68x_get_meas_dur(bme_conf.op_mode, &hw_conf, &bme_dev) / 1000;
        delay_ms += (heatr_conf.shared_heatr_dur);
        osDelay(delay_ms);

        struct bme68x_data raw_data[3];
        uint8_t n_fields = 0;
        bme68x_get_data(bme_conf.op_mode, raw_data, &n_fields, &bme_dev);

        if (n_fields > 0)
        {
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
