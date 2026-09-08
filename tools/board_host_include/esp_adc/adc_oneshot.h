#pragma once

#include "esp_err.h"
struct BoardHostAdc;
using adc_oneshot_unit_handle_t = BoardHostAdc*;
using adc_channel_t = int;
enum { ADC_CHANNEL_3 = 3, ADC_UNIT_1 = 1, ADC_ATTEN_DB_12 = 12, ADC_BITWIDTH_12 = 12 };
struct adc_oneshot_unit_init_cfg_t { int unit_id; };
struct adc_oneshot_chan_cfg_t { int atten, bitwidth; };
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*, adc_oneshot_unit_handle_t*);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t, const adc_oneshot_chan_cfg_t*);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t, int*);
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t);
