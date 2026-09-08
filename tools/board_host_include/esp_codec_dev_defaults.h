#pragma once

#include "esp_codec_dev.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
struct audio_codec_ctrl_if_t;
struct audio_codec_gpio_if_t;
constexpr uint8_t ES8311_CODEC_DEFAULT_ADDR = 0x30;
struct audio_codec_i2s_cfg_t { int port; i2s_chan_handle_t rx_handle, tx_handle; };
struct audio_codec_i2c_cfg_t { i2c_port_t port; uint8_t addr; void* bus_handle; };
struct es8311_codec_cfg_t {
    const audio_codec_ctrl_if_t* ctrl_if;
    const audio_codec_gpio_if_t* gpio_if;
    int codec_mode;
    gpio_num_t pa_pin;
    bool use_mclk;
    struct { float pa_voltage, codec_dac_voltage; } hw_gain;
    bool pa_reverted;
};
const audio_codec_data_if_t* audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t*);
const audio_codec_ctrl_if_t* audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t*);
const audio_codec_gpio_if_t* audio_codec_new_gpio();
const audio_codec_if_t* es8311_codec_new(const es8311_codec_cfg_t*);
int audio_codec_delete_codec_if(const audio_codec_if_t*);
int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t*);
int audio_codec_delete_gpio_if(const audio_codec_gpio_if_t*);
int audio_codec_delete_data_if(const audio_codec_data_if_t*);
