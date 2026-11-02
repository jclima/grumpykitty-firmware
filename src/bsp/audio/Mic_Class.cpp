/**
 * @file Mic_Class.cpp
 * @brief Microphone recording implementation for MeowKit (ESP32-S3 + ES7210)
 *
 * I2S data path uses ESP-IDF legacy i2s driver (driver/i2s.h).
 * Recording runs in a FreeRTOS background task with DMA double-buffering.
 * Referenced from M5Unified Mic_Class, simplified for ES7210 + ESP32-S3.
 */

#include "Mic_Class.hpp"
#include <esp_log.h>
#include <cstring>
#include <algorithm>
#include <cmath>

static const char* TAG = "MIC";

// ────────────────────────────────────────────────────────────
//  I2S setup / teardown (legacy driver/i2s.h API)
// ────────────────────────────────────────────────────────────

bool Mic_Class::_setup_i2s()
{
    if (_cfg.pin_data_in < 0) {
        ESP_LOGE(TAG, "pin_data_in not configured");
        return false;
    }

    _uninstall_i2s(); // clean up any previous instance

    // ── I2S driver config ──
    i2s_config_t i2s_cfg = {};
    i2s_cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    i2s_cfg.sample_rate          = _cfg.sample_rate * _cfg.over_sampling;
    i2s_cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_cfg.channel_format       = _cfg.stereo
                                     ? I2S_CHANNEL_FMT_RIGHT_LEFT
                                     : I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    i2s_cfg.dma_buf_count        = _cfg.dma_buf_count;
    i2s_cfg.dma_buf_len          = _cfg.dma_buf_len;
    i2s_cfg.use_apll             = false;
    i2s_cfg.tx_desc_auto_clear   = false;
    i2s_cfg.fixed_mclk           = 0;

    esp_err_t err = i2s_driver_install(_cfg.i2s_port, &i2s_cfg, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    // ── Pin config ──
    i2s_pin_config_t pin_cfg = {};
    pin_cfg.mck_io_num   = _cfg.pin_mclk;
    pin_cfg.bck_io_num   = _cfg.pin_bclk;
    pin_cfg.ws_io_num    = _cfg.pin_ws;
    pin_cfg.data_out_num = I2S_PIN_NO_CHANGE;
    pin_cfg.data_in_num  = _cfg.pin_data_in;

    err = i2s_set_pin(_cfg.i2s_port, &pin_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_pin failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(_cfg.i2s_port);
        return false;
    }

    i2s_zero_dma_buffer(_cfg.i2s_port);
    _i2s_installed = true;

    ESP_LOGI(TAG, "I2S%d RX configured: rate=%lu, DIN=%d, BCLK=%d, WS=%d, MCLK=%d",
             _cfg.i2s_port,
             (unsigned long)(i2s_cfg.sample_rate),
             _cfg.pin_data_in, _cfg.pin_bclk, _cfg.pin_ws, _cfg.pin_mclk);
    return true;
}

void Mic_Class::_uninstall_i2s()
{
    if (_i2s_installed) {
        i2s_stop(_cfg.i2s_port);
        i2s_driver_uninstall(_cfg.i2s_port);
        _i2s_installed = false;
    }
}

// ────────────────────────────────────────────────────────────
//  Background recording task
// ────────────────────────────────────────────────────────────

void Mic_Class::mic_task(void* args)
{
    auto* self = static_cast<Mic_Class*>(args);

    int oversampling = self->_cfg.over_sampling;
    if (oversampling < 1) oversampling = 1;
    if (oversampling > 8) oversampling = 8;

    const bool in_stereo = self->_cfg.stereo;
    const float f_gain   = (float)self->_cfg.magnification / (oversampling << 1);

    const size_t dma_buf_len = self->_cfg.dma_buf_len;
    int16_t* src_buf = (int16_t*)malloc(dma_buf_len * sizeof(int16_t));
    if (!src_buf) {
        ESP_LOGE(TAG, "Failed to allocate src_buf");
        self->_task_handle = nullptr;
        self->_task_running = false;
        vTaskDelete(nullptr);
        return;
    }
    memset(src_buf, 0, dma_buf_len * sizeof(int16_t));

    // Enable I2S
    i2s_start(self->_cfg.i2s_port);

    // Flush initial samples
    size_t bytes_read = 0;
    i2s_read(self->_cfg.i2s_port, src_buf,
             dma_buf_len * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(100));
    i2s_read(self->_cfg.i2s_port, src_buf,
             dma_buf_len * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(100));

    size_t   src_idx   = ~0u;
    size_t   src_len   = 0;
    int32_t  sum_value[4] = {0};
    int32_t  prev_value[2] = {0};
    int32_t  os_remain = oversampling;

    while (self->_task_running)
    {
        bool rec_flip = self->_rec_flip;
        recording_info_t* current = &self->_rec_info[!rec_flip];
        recording_info_t* next    = &self->_rec_info[ rec_flip];

        size_t dst_remain = current->length;
        if (dst_remain == 0)
        {
            rec_flip = !rec_flip;
            self->_rec_flip = rec_flip;
            xSemaphoreGive(self->_task_semaphore);
            std::swap(current, next);
            dst_remain = current->length;
            if (dst_remain == 0)
            {
                self->_is_recording = false;
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                src_idx = ~0u;
                src_len = 0;
                sum_value[0] = sum_value[1] = 0;
                continue;
            }
        }
        self->_is_recording = true;

        for (;;)
        {
            // Refill source buffer from DMA
            if (src_idx >= src_len)
            {
                bytes_read = 0;
                i2s_read(self->_cfg.i2s_port, src_buf,
                         dma_buf_len * sizeof(int16_t), &bytes_read,
                         pdMS_TO_TICKS(100));
                src_len = bytes_read >> 1; // samples
                src_idx = 0;
            }

            // Accumulate oversampling
            do {
                sum_value[0] += src_buf[src_idx];
                sum_value[1] += (src_idx + 1 < src_len) ? src_buf[src_idx + 1] : 0;
                src_idx += 2;
            } while (--os_remain && src_idx < src_len);

            if (os_remain) continue;
            os_remain = oversampling;

            // Use channel order for ESP32-S3  
            int32_t sv0 = sum_value[0];
            int32_t sv1 = sum_value[1];

            // Auto DC-offset removal
            int32_t value_tmp = (sv0 + sv1) << 3;
            int32_t offset = self->_offset;
            offset -= (value_tmp + offset + 16) >> 5;
            self->_offset = offset;
            offset = (offset + 8) >> 4;
            sum_value[0] = sv0 + offset;
            sum_value[1] = sv1 + offset;

            // Noise filter (low-pass IIR)
            int32_t nf = self->_cfg.noise_filter;
            if (nf) {
                for (int i = 0; i < 2; ++i) {
                    int32_t v = (sum_value[i] * (256 - nf) + prev_value[i] * nf + 128) >> 8;
                    prev_value[i] = v;
                    sum_value[i] = (int32_t)(v * f_gain);
                }
            } else {
                for (int i = 0; i < 2; ++i) {
                    sum_value[i] = (int32_t)(sum_value[i] * f_gain);
                }
            }

            // Determine output sample count
            int output_num = 2;
            if (in_stereo != current->is_stereo) {
                if (in_stereo) {
                    // stereo -> mono
                    sum_value[0] = (sum_value[0] + sum_value[1] + 1) >> 1;
                    output_num = 1;
                } else {
                    // mono -> stereo
                    sum_value[3] = sum_value[1];
                    sum_value[2] = sum_value[1];
                    sum_value[1] = sum_value[0];
                    output_num = 4;
                }
            }

            // Write to destination buffer
            for (int i = 0; i < output_num; ++i) {
                auto value = sum_value[i];
                if (current->is_16bit) {
                    if      (value < INT16_MIN + 16) value = INT16_MIN + 16;
                    else if (value > INT16_MAX - 16) value = INT16_MAX - 16;
                    auto* dst = (int16_t*)current->data;
                    *dst++ = (int16_t)value;
                    current->data = dst;
                } else {
                    value = ((value + 128) >> 8) + 128;
                    if      (value < 0)   value = 0;
                    else if (value > 255) value = 255;
                    auto* dst = (uint8_t*)current->data;
                    *dst++ = (uint8_t)value;
                    current->data = dst;
                }
            }

            sum_value[0] = sum_value[1] = 0;
            dst_remain -= output_num;
            if ((int32_t)dst_remain <= 0) {
                current->length = 0;
                break;
            }
        }
    }

    // Cleanup
    self->_is_recording = false;
    i2s_stop(self->_cfg.i2s_port);
    free(src_buf);

    self->_task_handle = nullptr;
    vTaskDelete(nullptr);
}

// ────────────────────────────────────────────────────────────
//  Public API
// ────────────────────────────────────────────────────────────

uint32_t Mic_Class::_calc_rec_rate() const
{
    return _cfg.sample_rate * _cfg.over_sampling;
}

bool Mic_Class::begin()
{
    if (_task_running) {
        uint32_t rate = _calc_rec_rate();
        if (_rec_sample_rate == rate) return true;
        // Rate changed, restart
        while (isRecording()) vTaskDelay(1);
        end();
        _rec_sample_rate = rate;
    }

    if (!_task_semaphore) {
        _task_semaphore = xSemaphoreCreateBinary();
    }

    // Initialise codec if available
    if (_codec) {
        if (!_codec->isEnabled()) {
            if (!_codec->begin(_cfg.sample_rate, ES7210_BIT_16, ES7210_FMT_I2S)) {
                ESP_LOGE(TAG, "ES7210 codec init failed");
                return false;
            }
        }
        _codec->start();
    }

    // Setup I2S
    if (!_setup_i2s()) return false;

    _rec_sample_rate = _calc_rec_rate();
    _task_running = true;

    size_t stack = 2048 + (_cfg.dma_buf_len * sizeof(uint16_t));

    BaseType_t created;
    if (_cfg.task_pinned_core < portNUM_PROCESSORS) {
        created = xTaskCreatePinnedToCore(mic_task, "mic_task", stack, this,
                                          _cfg.task_priority, &_task_handle,
                                          _cfg.task_pinned_core);
    } else {
        created = xTaskCreate(mic_task, "mic_task", stack, this,
                              _cfg.task_priority, &_task_handle);
    }
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create mic_task");
        _task_running = false;
        _task_handle  = nullptr;
        _uninstall_i2s();
        if (_codec && _codec->isStarted()) _codec->stop();
        return false;
    }

    ESP_LOGI(TAG, "Recording task started (rate=%lu, os=%d)",
             _cfg.sample_rate, _cfg.over_sampling);
    return true;
}

void Mic_Class::end()
{
    // The task may already have exited on its own (e.g. buffer allocation
    // failure) while I2S is still installed, so do not bail out early.
    if (!_task_running && !_task_handle && !_i2s_installed) return;
    _task_running = false;

    if (_task_handle) {
        xTaskNotifyGive(_task_handle);
        while (_task_handle) vTaskDelay(1);
    }

    _uninstall_i2s();

    if (_codec && _codec->isStarted()) {
        _codec->stop();
    }

    ESP_LOGI(TAG, "Recording stopped");
}

bool Mic_Class::_rec_raw(void* data, size_t length, bool is_16bit,
                         uint32_t sample_rate, bool stereo)
{
    recording_info_t info;
    info.data      = data;
    info.length    = length;
    info.is_16bit  = is_16bit;
    info.is_stereo = stereo;

    if (sample_rate > 0) {
        _cfg.sample_rate = sample_rate;
    }

    if (!begin()) return false;
    if (length == 0) return true;

    // Wait for previous recording to finish
    while (_rec_info[_rec_flip].length) {
        xSemaphoreTake(_task_semaphore, 1);
    }

    _rec_info[_rec_flip] = info;
    if (_task_handle) {
        xTaskNotifyGive(_task_handle);
    }
    return true;
}

bool Mic_Class::record(int16_t* data, size_t length, uint32_t sample_rate, bool stereo)
{
    return _rec_raw(data, length, true, sample_rate, stereo);
}

bool Mic_Class::record(uint8_t* data, size_t length, uint32_t sample_rate, bool stereo)
{
    return _rec_raw(data, length, false, sample_rate, stereo);
}
