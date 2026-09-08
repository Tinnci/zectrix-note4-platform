#pragma once

#include "esp_err.h"
struct BoardHostAdcCalibration;
using adc_cali_handle_t = BoardHostAdcCalibration*;
esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t, int, int*);
