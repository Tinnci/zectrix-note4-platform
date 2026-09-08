#pragma once

#include "driver/gpio.h"
struct BoardHostI2sChannel;
using i2s_chan_handle_t = BoardHostI2sChannel*;
enum { I2S_NUM_0, I2S_ROLE_MASTER, I2S_CLK_SRC_DEFAULT, I2S_MCLK_MULTIPLE_256,
       I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_BIT_WIDTH_AUTO, I2S_SLOT_MODE_STEREO,
       I2S_STD_SLOT_BOTH };
struct i2s_chan_config_t {
    int id, role, dma_desc_num, dma_frame_num;
    bool auto_clear_after_cb, auto_clear_before_cb;
    int intr_priority;
};
struct i2s_std_config_t {
    struct { uint32_t sample_rate_hz; int clk_src, mclk_multiple; } clk_cfg;
    struct {
        int data_bit_width, slot_bit_width, slot_mode, slot_mask, ws_width;
        bool ws_pol, bit_shift;
    } slot_cfg;
    struct { gpio_num_t mclk, bclk, ws, dout, din; } gpio_cfg;
};
esp_err_t i2s_new_channel(const i2s_chan_config_t*, i2s_chan_handle_t*, i2s_chan_handle_t*);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t*);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_del_channel(i2s_chan_handle_t);
