#pragma once

#include <cstdint>
#include "esp_err.h"
struct audio_codec_if_t;
struct audio_codec_data_if_t;
struct BoardHostCodecDevice;
using esp_codec_dev_handle_t = BoardHostCodecDevice*;
enum { ESP_CODEC_DEV_TYPE_IN_OUT, ESP_CODEC_DEV_WORK_MODE_BOTH };
struct esp_codec_dev_cfg_t {
    int dev_type;
    const audio_codec_if_t* codec_if;
    const audio_codec_data_if_t* data_if;
};
struct esp_codec_dev_sample_info_t {
    int bits_per_sample, channel, channel_mask;
    uint32_t sample_rate;
    int mclk_multiple;
};
esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t*);
esp_err_t esp_codec_dev_open(esp_codec_dev_handle_t, const esp_codec_dev_sample_info_t*);
esp_err_t esp_codec_dev_close(esp_codec_dev_handle_t);
void esp_codec_dev_delete(esp_codec_dev_handle_t);
esp_err_t esp_codec_dev_set_in_gain(esp_codec_dev_handle_t, float);
esp_err_t esp_codec_dev_set_out_vol(esp_codec_dev_handle_t, int);
esp_err_t esp_codec_dev_read(esp_codec_dev_handle_t, void*, int);
esp_err_t esp_codec_dev_write(esp_codec_dev_handle_t, void*, int);
