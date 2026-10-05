#include "audio_codec.h"

#include "note4_log_event.h"
#include <cstring>
#include <driver/i2s_common.h>

#define TAG "AudioCodec"

AudioCodec::AudioCodec() {
}

AudioCodec::~AudioCodec() {
    // The codec data interface borrows these channels; it does not delete them.
    for (i2s_chan_handle_t channel : {rx_handle_, tx_handle_}) {
        if (channel == nullptr) continue;
        const esp_err_t stopped = i2s_channel_disable(channel);
        if (stopped != ESP_OK && stopped != ESP_ERR_INVALID_STATE) {
            NOTE4_LOGW(TAG, "i2s_stop_failed", "error=%s",
                       note4::log::Token(esp_err_to_name(stopped)).c_str());
        }
        const esp_err_t released = i2s_del_channel(channel);
        if (released != ESP_OK) {
            NOTE4_LOGW(TAG, "i2s_release_failed", "error=%s",
                       note4::log::Token(esp_err_to_name(released)).c_str());
        }
    }
}

void AudioCodec::OutputData(std::vector<int16_t>& data) {
    Write(data.data(), data.size());
}

bool AudioCodec::InputData(std::vector<int16_t>& data) {
    int samples = Read(data.data(), data.size());
    if (samples > 0) {
        return true;
    }
    return false;
}

void AudioCodec::Start() {
    if (tx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    }

    if (rx_handle_ != nullptr) {
        ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    }

    EnableInput(true);
    EnableOutput(true);
    NOTE4_LOGI(TAG, "codec_started", "");
}

void AudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    NOTE4_LOGI(TAG, "volume_set", "volume=%d", output_volume_);
}

void AudioCodec::SetInputGain(float gain) {
    input_gain_ = gain;
    NOTE4_LOGI(TAG, "gain_set", "gain=%.1f", input_gain_);
}

void AudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    input_enabled_ = enable;
    NOTE4_LOGI(TAG, "input_set", "enabled=%s",
               note4::log::Token(enable ? "true" : "false").c_str());
}

void AudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    output_enabled_ = enable;
    NOTE4_LOGI(TAG, "output_set", "enabled=%s",
               note4::log::Token(enable ? "true" : "false").c_str());
}
