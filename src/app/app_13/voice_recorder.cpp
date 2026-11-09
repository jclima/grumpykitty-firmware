// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Voice Recorder: ES7210 mic -> PSRAM ring -> WAV on SD (onRunning is the only SD writer). */
#include "voice_recorder.h"
#include "wav_format.h"
#include "../app_common/sd_guard.h"
#include "../../system/system_sound.h"
#include "../../system/settings_bridge.h"
#include "../../system/usb_msc.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint32_t kRate = 16000;
constexpr size_t kRingSamples = 131072;             // 256 KB, ~8.2 s, power of two
constexpr size_t kRingMask = kRingSamples - 1;
constexpr size_t kStageSamples = 4096;              // 8 KB internal staging
constexpr uint32_t kPumpBytes = 16384;              // SD bytes moved per onRunning
constexpr uint32_t kMaxBytes = 30u * 60u * kRate * 2u;   // 30 min cap
constexpr uint32_t kPatchMs = 5000;
constexpr uint32_t kUiMs = 50;
constexpr size_t kDmaSamp = 256;
constexpr int kDmaCount = 6;
constexpr size_t kCapFrames = 512;
const char* const kDir = "/recordings";

// Mooncake retains app instances; this pointer lets Launcher service a timed close.
MOONCAKE::APPS::AppVoiceRecorder* s_voice_recorder = nullptr;

void say(lgfx::LGFXBase& lcd, int x, int y, const char* s, uint16_t fg) {
    lcd.setFont(&fonts::efontCN_16); lcd.setTextDatum(textdatum_t::top_left);
    lcd.setTextColor(fg, hp::COL_BG); lcd.setCursor(x, y); lcd.print(s);
}
}

namespace MOONCAKE::APPS {

AppVoiceRecorder::AppVoiceRecorder(DEVICES* device) : _device(device) {
    setAppInfo().name = "Voice Recorder";
    s_voice_recorder = this;
}

bool AppVoiceRecorder::closePending() {
    return s_voice_recorder && s_voice_recorder->_closePending;
}

void AppVoiceRecorder::serviceDeferredClose(DEVICES*) {
    if (!s_voice_recorder || !s_voice_recorder->_draining) return;
    if (s_voice_recorder->_recording) return;
    if (s_voice_recorder->_capActive.load(std::memory_order_acquire)) return;
    s_voice_recorder->_micStop();
    s_voice_recorder->_releaseRecordBuffers();
    s_voice_recorder->_draining = false;
    if (s_voice_recorder->_closePending) s_voice_recorder->_finishClose();
}

// ───────────────────────── lifecycle ─────────────────────────

void AppVoiceRecorder::onOpen() {
    // Exclusive audio bus ownership: the mic and speaker share MCLK/BCLK/WS.
    system_sound_suspend();
    i2s_driver_uninstall(I2S_NUM_1);   // defensive: no-op when absent
    _busy = _device->speaker.isEnabled();

    memset(_prev, 0, sizeof(_prev));
    _a_armed = _b_armed = false; _b_since = 0;
    _msg[0] = 0; _msgColor = hp::COL_FG; _msgDirty = true; _redraw = true;
    _lastUi = 0; _shownSec = -1; _shownLevel = -1; _shownDropped = 0xFFFFFFFFu; _shownRec = false;
    _level = 0;
    _recording = false; _codecReady = false; _i2sReady = false;
    _capActive.store(false, std::memory_order_release); _taskRun.store(false, std::memory_order_release);
    _closePending = false; _closing = false; _draining = false;
    _ring = nullptr; _stage = nullptr;
    _head = 0; _tail = 0; _dropped = 0; _peak = 0;
    _file = File();
    _path[0] = 0; _dataBytes = 0; _lastPatch = 0; _wroteErr = false;
    _mode = M_REC; _lstate = L_BROWSE;
    _list = nullptr; _count = _sel = _top = _probe = 0;
    _pf = File(); _premain = _pplayed = 0; _pbuf = nullptr; _spkOpen = false; _shownPlaySec = -1;

    auto& b = _device->button;
    b.update(); b.A.hasChanged(); b.B.hasChanged();
    b.Up.hasChanged(); b.Down.hasChanged(); b.Left.hasChanged(); b.Right.hasChanged();

    if (_busy) _say("Audio busy", hp::COL_ERR);
    _drawStatic();
}

void AppVoiceRecorder::onClose() {
    _closing = true;
    if (_recording) _stopRecording("", hp::COL_FG);
    if (_draining && _capActive.load(std::memory_order_acquire)) {
        _closePending = true;
        return;
    }
    if (_closePending) return;
    _finishClose();
}

void AppVoiceRecorder::_finishClose() {
    _playStop("", hp::COL_FG);
    if (_list) { heap_caps_free(_list); _list = nullptr; }
    _taskRun.store(false, std::memory_order_release);
    _micStop();                                  // defensive, idempotent
    _releaseRecordBuffers();
    if (!_busy && _device->speaker.isEnabled()) {
        _device->speaker.setMute(true);
        _device->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
        _device->speaker.end();                  // resume() calls begin() without end()
    }
    if (!system_sound_resume())
        Serial.println("[VoiceRec] failed to restore system audio");
    _closePending = false; _closing = false; _draining = false;
}

// ───────────────────────── UI ─────────────────────────

void AppVoiceRecorder::_say(const char* text, uint16_t color) {
    strncpy(_msg, text, sizeof(_msg) - 1); _msg[sizeof(_msg) - 1] = 0;
    _msgColor = color; _msgDirty = true;
}

void AppVoiceRecorder::_drawStatic() {
    auto& lcd = _device->Lcd;
    lcd.fillScreen(hp::COL_BG);
    hp::drawChrome(lcd);
    _setFooter();
    say(lcd, 8, 192, "16 kHz  mono  16-bit  max 30:00", hp::COL_DIM);
    _shownSec = -1; _shownLevel = -1; _shownDropped = 0xFFFFFFFFu;
    _msgDirty = true; _redraw = true;
    _drawDynamic(true);
}

void AppVoiceRecorder::_drawDynamic(bool force) {
    auto& lcd = _device->Lcd;
    if (force || _redraw || _shownRec != _recording) {
        _shownRec = _recording; _redraw = false;
        if (_busy) hp::drawHeader(lcd, "VOICE RECORDER", "BUSY", hp::COL_ERR);
        else if (_recording) hp::drawHeader(lcd, "VOICE RECORDER", "REC", hp::COL_ERR);
        else hp::drawHeader(lcd, "VOICE RECORDER", "READY", hp::COL_INFO);
        _shownSec = -1;
    }
    const int sec = (int)(_dataBytes / (kRate * 2));
    if (sec != _shownSec) {
        _shownSec = sec;
        char t[8]; snprintf(t, sizeof(t), "%02d:%02d", sec / 60 % 100, sec % 60);
        lcd.fillRect(1, 40, 318, 66, hp::COL_BG);
        lcd.setFont(&fonts::efontCN_16); lcd.setTextSize(4);
        lcd.setTextColor(_recording ? hp::COL_ACCENT : hp::COL_FG, hp::COL_BG);
        lcd.setTextDatum(textdatum_t::top_center);
        lcd.drawString(t, 160, 44);
        lcd.setTextSize(1);
    }
    if (_level != _shownLevel) {
        _shownLevel = _level;
        hp::drawProgress(lcd, 30, 120, 260, _level / 100.0f,
                         _level >= 97 ? hp::COL_ERR : hp::COL_FG);
    }
    const uint32_t dropped = _dropped.load();
    if (dropped != _shownDropped) {
        _shownDropped = dropped;
        lcd.fillRect(1, 142, 318, 22, hp::COL_BG);
        if (dropped) {
            char t[32]; snprintf(t, sizeof(t), "DROPPED %lu.%lu s", (unsigned long)(dropped / kRate),
                                 (unsigned long)((dropped % kRate) * 10 / kRate));
            lcd.setFont(&fonts::efontCN_16); lcd.setTextColor(hp::COL_WARN, hp::COL_BG);
            lcd.setTextDatum(textdatum_t::top_center); lcd.drawString(t, 160, 146);
        }
    }
    if (_msgDirty) {
        _msgDirty = false;
        lcd.fillRect(1, 166, 318, 22, hp::COL_BG);
        lcd.setFont(&fonts::efontCN_16); lcd.setTextColor(_msgColor, hp::COL_BG);
        lcd.setTextDatum(textdatum_t::top_center); lcd.drawString(_msg, 160, 169);
    }
}

// ───────────────────────── mic ─────────────────────────

bool AppVoiceRecorder::_micStart() {
    if (_i2sReady) { i2s_stop(I2S_NUM_1); i2s_driver_uninstall(I2S_NUM_1); _i2sReady = false; }  // left by a deferred stop
    if (!_codec.begin(kRate, ES7210_BIT_16, ES7210_FMT_I2S)) return false;
    _codecReady = true;
    _codec.selectMic(ES7210_MIC1 | ES7210_MIC2);
    _codec.setGain(ES7210_GAIN_30DB);
    _codec.start();

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = kRate;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = kDmaCount;
    cfg.dma_buf_len          = (int)kDmaSamp;
    cfg.use_apll             = true;
    cfg.tx_desc_auto_clear   = false;
    cfg.fixed_mclk           = 0;
    if (i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr) != ESP_OK) { _micStop(); return false; }
    _i2sReady = true;
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = HAL_PIN_I2S_MCLK;
    pins.bck_io_num   = HAL_PIN_I2S_BCLK;
    pins.ws_io_num    = HAL_PIN_I2S_WS;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = HAL_PIN_I2S_DIN;
    if (i2s_set_pin(I2S_NUM_1, &pins) != ESP_OK) { _micStop(); return false; }
    i2s_start(I2S_NUM_1);
    return true;
}

void AppVoiceRecorder::_micStop() {
    if (_capActive.load(std::memory_order_acquire)) return;
    if (_codecReady) { _codec.stop(); _codec.end(); _codecReady = false; }
    if (_i2sReady) {
        i2s_stop(I2S_NUM_1);
        i2s_driver_uninstall(I2S_NUM_1);
        _i2sReady = false;
    }
}

void AppVoiceRecorder::_capEntry(void* arg) {
    auto* self = static_cast<AppVoiceRecorder*>(arg);
    self->_capLoop();
    vTaskDelete(nullptr);
}

void AppVoiceRecorder::_capLoop() {
    constexpr size_t kBytes = kCapFrames * 2 * sizeof(int16_t);
    int16_t* buf = (int16_t*)heap_caps_malloc(kBytes, MALLOC_CAP_INTERNAL);
    if (!buf) { _taskRun.store(false, std::memory_order_release); _capActive.store(false, std::memory_order_release); return; }
    int32_t dcAcc = 0;
    while (_taskRun.load(std::memory_order_acquire)) {
        size_t got = 0;
        if (i2s_read(I2S_NUM_1, buf, kBytes, &got, pdMS_TO_TICKS(60)) != ESP_OK || got < 4) continue;
        if (!_taskRun.load(std::memory_order_acquire)) break;
        const size_t n = got / (2 * sizeof(int16_t));
        uint16_t peak = 0;
        for (size_t i = 0; i < n; ++i) {
            const int32_t s = buf[i * 2];                    // MIC1 channel
            dcAcc = dcAcc - (dcAcc >> 8) + s;
            int32_t v = s - (dcAcc >> 8);
            if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
            buf[i] = (int16_t)v;
            const uint16_t m = (uint16_t)(v < 0 ? -v : v);
            if (m > peak) peak = m;
        }
        if (peak > _peak.load(std::memory_order_relaxed)) _peak.store(peak, std::memory_order_relaxed);
        const uint32_t head = _head.load(std::memory_order_relaxed);
        const uint32_t tail = _tail.load(std::memory_order_acquire);
        if (kRingSamples - (head - tail) < n) {
            _dropped.fetch_add((uint32_t)n, std::memory_order_relaxed);
            continue;
        }
        for (size_t i = 0; i < n; ++i) _ring[(head + i) & kRingMask] = buf[i];
        _head.store(head + (uint32_t)n, std::memory_order_release);
    }
    heap_caps_free(buf);
    _capActive.store(false, std::memory_order_release);
}

// ───────────────────────── recording ─────────────────────────

void AppVoiceRecorder::_releaseRecordBuffers() {
    if (_ring) { heap_caps_free(_ring); _ring = nullptr; }
    if (_stage) { heap_caps_free(_stage); _stage = nullptr; }
}

bool AppVoiceRecorder::_makePath() {
    RTC_Time t{};
    if (_device->rtc.getTime(t) && t.year >= 2024 && _device->rtc.isTimeValid(t)) {
        snprintf(_path, sizeof(_path), "%s/REC_%04u%02u%02u_%02u%02u%02u.wav", kDir,
                 (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
                 (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec);
        if (!SD_MMC.exists(_path)) return true;
    }
    for (int n = 1; n <= 9999; ++n) {
        snprintf(_path, sizeof(_path), "%s/REC_%04d.wav", kDir, n);
        if (!SD_MMC.exists(_path)) return true;
    }
    return false;
}

bool AppVoiceRecorder::_startRecording() {
    _playStop("", hp::COL_FG);             // speaker must be closed before the mic opens
    const sd_guard::Status st = sd_guard::check();
    if (st != sd_guard::Status::Ok) { _say(sd_guard::message(st), hp::COL_ERR); return false; }

    // No up-front free-space check: SD_MMC.usedBytes() may scan the whole FAT
    // (seconds) and would freeze the UI/long-press-B. A full card is handled by
    // the write-error stop path in onRunning, and the file is capped at kMaxBytes.
    if (_capActive.load(std::memory_order_acquire) || _closePending || _draining) { _say("Mic busy", hp::COL_ERR); return false; }
    if (!SD_MMC.exists(kDir) && !SD_MMC.mkdir(kDir)) { _say("Cannot create /recordings", hp::COL_ERR); return false; }

    _ring = (int16_t*)heap_caps_malloc(kRingSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _stage = (int16_t*)heap_caps_malloc(kStageSamples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!_ring || !_stage) { _releaseRecordBuffers(); _say("Out of memory", hp::COL_ERR); return false; }
    if (!_makePath()) { _releaseRecordBuffers(); _say("No free file name", hp::COL_ERR); return false; }

    if (!_micStart()) { _releaseRecordBuffers(); _say("Mic init failed", hp::COL_ERR); return false; }

    _file = SD_MMC.open(_path, FILE_WRITE);
    uint8_t hdr[wavfmt::kHeaderSize];
    wavfmt::writeHeader(hdr, kRate, 1, 16, 0);
    if (!_file || _file.write(hdr, sizeof(hdr)) != sizeof(hdr)) {
        if (_file) _file.close();
        SD_MMC.remove(_path);
        _micStop(); _releaseRecordBuffers();
        _say("Cannot create file", hp::COL_ERR);
        return false;
    }

    _head = 0; _tail = 0; _dropped = 0; _peak = 0; _level = 0;
    _dataBytes = 0; _wroteErr = false; _shownSec = -1;
    _taskRun.store(true, std::memory_order_release);
    _capActive.store(true, std::memory_order_release);
    if (xTaskCreatePinnedToCore(_capEntry, "voice_cap", 4096, this, 2, nullptr, 0) != pdPASS) {
        _taskRun.store(false, std::memory_order_release); _capActive.store(false, std::memory_order_release);
        _file.close(); SD_MMC.remove(_path);
        _micStop(); _releaseRecordBuffers();
        _say("Capture task failed", hp::COL_ERR);
        return false;
    }
    _lastPatch = millis();
    _recording = true;
    const char* slash = strrchr(_path, '/');
    _say(slash ? slash + 1 : _path, hp::COL_FG);
    return true;
}

bool AppVoiceRecorder::_patchHeader() {
    uint8_t hdr[wavfmt::kHeaderSize];
    wavfmt::writeHeader(hdr, kRate, 1, 16, _dataBytes);
    bool ok = _file.seek(0) && _file.write(hdr, sizeof(hdr)) == sizeof(hdr);
    _file.flush();
    ok = _file.seek(wavfmt::kHeaderSize + _dataBytes) && ok;
    return ok;
}

// Move ring contents to the file: up to kPumpBytes, or everything when `all`.
void AppVoiceRecorder::_pump(bool all) {
    if (!_ring || !_stage || !_file) return;
    uint32_t budget = all ? 0xFFFFFFFFu : kPumpBytes;
    while (budget >= 2 && !_wroteErr) {
        const uint32_t head = _head.load(std::memory_order_acquire);
        const uint32_t tail = _tail.load(std::memory_order_relaxed);
        size_t n = head - tail;
        if (!n) break;
        if (n > kStageSamples) n = kStageSamples;
        if (n > budget / 2) n = budget / 2;
        const uint32_t room = (kMaxBytes - _dataBytes) / 2;
        if (n > room) n = room;
        if (!n) break;
        for (size_t i = 0; i < n; ++i) _stage[i] = _ring[(tail + i) & kRingMask];
        if (_file.write((const uint8_t*)_stage, n * 2) != n * 2) { _wroteErr = true; break; }
        _tail.store(tail + (uint32_t)n, std::memory_order_release);
        _dataBytes += (uint32_t)(n * 2);
        budget -= (uint32_t)(n * 2);
    }
}

void AppVoiceRecorder::_stopRecording(const char* why, uint16_t color, bool touchCard) {
    if (!_recording) return;
    _taskRun.store(false, std::memory_order_release);  // cooperative stop (i2s_read times out in 60 ms)
    for (int i = 0; i < 100 && _capActive.load(std::memory_order_acquire); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    const bool draining = _capActive.load(std::memory_order_acquire);
    if (draining) {
        Serial.println("[VoiceRec] capture task is still draining; deferring cleanup");
        _draining = true;
        if (_closing) _closePending = true;
    }

    /* The worker only appends to the SPSC ring; it never touches the file.
       Finalize the WAV once, then retain mic/I2S and buffers if it is still
       draining so the worker can finish without use-after-free. */
    if (!touchCard) {
        /* Card is owned by USB MSC (usb_msc_enable already ended SD_MMC): no
           write/patch/remove. Dropping the File handle still runs fclose, which is
           harmless here because the VFS mount is gone and nothing is flushed to the card. */
        _file = File();
        if (why && why[0]) _say(why, color);
    } else {
        _pump(true);                                    // drain what the task captured
        bool ok = !_wroteErr;
        if (_file) {
            ok = _patchHeader() && ok;
            _file.close();
        }
        const bool empty = (_dataBytes == 0);
        if (empty) SD_MMC.remove(_path);
        if (why && why[0]) _say(why, color);
        else if (!ok) _say("SD write error", hp::COL_ERR);
        else if (!empty) {
            const char* slash = strrchr(_path, '/');
            _say(slash ? slash + 1 : _path, hp::COL_FG);
        }
    }
    _recording = false;
    if (!draining) {
        _micStop();
        _releaseRecordBuffers();
    }
}

// ───────────────────────── playback ─────────────────────────

bool AppVoiceRecorder::_playStart() {
    if (_capActive.load(std::memory_order_acquire) || _closePending || _draining) {
        _drawListMsg("Mic busy", hp::COL_ERR);
        return false;
    }
    if (_sel < 0 || _sel >= _count) return false;
    char path[48];
    snprintf(path, sizeof(path), "%s/%s", kDir, _list[_sel].name);
    _pf = SD_MMC.open(path, FILE_READ);
    if (!_pf) { _drawListMsg("Cannot open file", hp::COL_ERR); return false; }
    uint8_t head[512];
    const int got = _pf.read(head, sizeof(head));
    if (got <= 0 || !wavfmt::parse(head, (size_t)got, _pf.size(), _pinfo) ||
        !_pf.seek(_pinfo.dataOffset)) {
        _pf.close();
        _list[_sel].state = 2;
        _drawListMsg("Unsupported WAV", hp::COL_ERR);
        return false;
    }
    _pbuf = (int16_t*)heap_caps_malloc(1024 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!_pbuf) { _pf.close(); _drawListMsg("Out of memory", hp::COL_ERR); return false; }

    // Mic side is already torn down (never open outside a recording).
    auto& spk = _device->speaker;
    spk.config().sample_rate = _pinfo.rate;
    if (!spk.begin(&In_I2C)) {
        spk.end();
        heap_caps_free(_pbuf); _pbuf = nullptr; _pf.close();
        _drawListMsg("Speaker init failed", hp::COL_ERR);
        return false;
    }
    _spkOpen = true;
    spk.setMute(true);
    spk.setVolume(sys_get_volume() * SPK_VOLUME_MAX / 100);
    _device->io_exp.digitalWrite(HAL_IOEXP_PA_EN, HIGH);
    delay(12);
    spk.setMute(false);
    _premain = _pinfo.dataBytes; _pplayed = 0; _shownPlaySec = -1;
    return true;
}

void AppVoiceRecorder::_playStop(const char* why, uint16_t color) {
    if (_spkOpen) {
        auto& spk = _device->speaker;
        spk.stop();
        spk.setMute(true);
        delay(4);
        _device->io_exp.digitalWrite(HAL_IOEXP_PA_EN, LOW);
        spk.end();
        _spkOpen = false;
    }
    if (_pf) _pf.close();
    if (_pbuf) { heap_caps_free(_pbuf); _pbuf = nullptr; }
    if (_mode == M_LIST && _lstate == L_PLAY) {
        _lstate = L_BROWSE;
        if (why && why[0]) _drawListMsg(why, color);
        _setFooter();
        if (_list) _drawRow(_sel);
    }
}

// One ~1024-sample slice per call; playPcm blocks only until the DMA accepts it.
void AppVoiceRecorder::_playPump() {
    if (!_spkOpen || !_pbuf) return;
    const uint32_t frame = _pinfo.channels * 2u;
    uint32_t n = 1024 * 2;
    if (n > _premain) n = _premain;
    n -= n % frame;
    int r = n ? _pf.read((uint8_t*)_pbuf, n) : 0;
    r -= r % (int)frame;
    if (r <= 0) {
        // Natural end: push silence through so the DMA tail is played, not cut by stop().
        memset(_pbuf, 0, 1024 * sizeof(int16_t));
        for (int i = 0; i < 2; ++i)
            _device->speaker.playPcm(_pbuf, 1024 / _pinfo.channels * _pinfo.channels,
                                     (uint8_t)_pinfo.channels, _pinfo.rate);
        _playStop("Finished", hp::COL_FG);
        return;
    }
    if (!_device->speaker.playPcm(_pbuf, (size_t)r / 2, (uint8_t)_pinfo.channels, _pinfo.rate)) {
        _playStop("Playback error", hp::COL_ERR);
        return;
    }
    _premain -= (uint32_t)r;
    _pplayed += (uint32_t)r;
    const int sec = (int)(_pplayed / (_pinfo.rate * frame));
    if (sec != _shownPlaySec) { _shownPlaySec = sec; _drawRow(_sel); }
}

// ───────────────────────── file list ─────────────────────────

void AppVoiceRecorder::_setFooter() {
    auto& lcd = _device->Lcd;
    if (_mode == M_REC) {
        hp::drawFooter(lcd, _busy ? "" : "A:Rec/Stop  >:List", "Hold B:Exit");
    } else if (_lstate == L_CONFIRM) {
        hp::drawFooter(lcd, "A:Delete  B:Cancel", "Hold B:Exit");
    } else if (_lstate == L_PLAY) {
        hp::drawFooter(lcd, "A:Stop  <:Back", "Hold B:Exit");
    } else {
        hp::drawFooter(lcd, "A:Play B:Del <:Rec", "Hold B:Exit");
    }
}

void AppVoiceRecorder::_enterList() {
    if (_recording) { _say("Stop recording first", hp::COL_WARN); return; }
    _mode = M_LIST; _lstate = L_BROWSE;
    _sel = _top = 0;
    _scanList();
    _drawList();
}

void AppVoiceRecorder::_leaveList() {
    _playStop("", hp::COL_FG);
    if (_list) { heap_caps_free(_list); _list = nullptr; }
    _count = 0;
    _mode = M_REC; _lstate = L_BROWSE;
    _drawStatic();
}

static int cmpDesc(const void* a, const void* b) {
    return strcmp(static_cast<const char*>(b), static_cast<const char*>(a));   // name is the first member
}

void AppVoiceRecorder::_scanList() {
    constexpr int kMax = 64;
    _count = 0; _probe = 0;
    if (!_list) _list = (Entry*)heap_caps_calloc(kMax, sizeof(Entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_list) return;
    const sd_guard::Status st = sd_guard::check();
    if (st != sd_guard::Status::Ok) return;
    File dir = SD_MMC.open(kDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    File f = dir.openNextFile();
    while (f) {
        const char* slash = strrchr(f.name(), '/');
        const char* base = slash ? slash + 1 : f.name();
        const size_t len = strlen(base);
        if (!f.isDirectory() && len > 4 && len < sizeof(Entry::name) &&
            strcasecmp(base + len - 4, ".wav") == 0) {
            int slot = -1;
            if (_count < kMax) slot = _count++;
            else {   // keep the newest kMax names: replace the smallest if this one is greater
                int mn = 0;
                for (int i = 1; i < kMax; ++i) if (strcmp(_list[i].name, _list[mn].name) < 0) mn = i;
                if (strcmp(base, _list[mn].name) > 0) slot = mn;
            }
            if (slot >= 0) {
                memset(&_list[slot], 0, sizeof(Entry));
                memcpy(_list[slot].name, base, len);
                _list[slot].size = (uint32_t)f.size();
            }
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
    qsort(_list, (size_t)_count, sizeof(Entry), cmpDesc);
}

void AppVoiceRecorder::_probeOne(int idx) {
    if (!_list || idx < 0 || idx >= _count || _list[idx].state) return;
    char path[48];
    snprintf(path, sizeof(path), "%s/%s", kDir, _list[idx].name);
    File f = SD_MMC.open(path, FILE_READ);
    uint8_t head[512];
    wavfmt::Info info{};
    int got = f ? f.read(head, sizeof(head)) : 0;
    if (got > 0 && wavfmt::parse(head, (size_t)got, f.size(), info)) {
        _list[idx].secs = info.dataBytes / (info.rate * info.channels * 2u);
        _list[idx].state = 1;
    } else {
        _list[idx].state = 2;
    }
    if (f) f.close();
}

void AppVoiceRecorder::_fixScroll() {
    if (_sel < _top) _top = _sel;
    if (_sel >= _top + hp::LIST2_VIS) _top = _sel - hp::LIST2_VIS + 1;
    if (_top < 0) _top = 0;
}

void AppVoiceRecorder::_drawRow(int idx) {
    if (_mode != M_LIST || !_list) return;
    const int row = idx - _top;
    if (row < 0 || row >= hp::LIST2_VIS) return;
    auto& lcd = _device->Lcd;
    if (idx >= _count) { hp::clearListRow2(lcd, row); return; }
    const Entry& e = _list[idx];
    char sub[40];
    char sz[16];
    if (e.size >= 1024u * 1024u) {
        const uint32_t t = (uint32_t)(((uint64_t)e.size * 10) / (1024u * 1024u));
        snprintf(sz, sizeof(sz), "%lu.%lu MB", (unsigned long)(t / 10), (unsigned long)(t % 10));
    } else {
        snprintf(sz, sizeof(sz), "%lu KB", (unsigned long)(e.size / 1024u));
    }
    if (idx == _sel && _lstate == L_PLAY) {
        const uint32_t cur = _pinfo.rate ? _pplayed / (_pinfo.rate * _pinfo.channels * 2u) : 0;
        snprintf(sub, sizeof(sub), "PLAY %02lu:%02lu / %02lu:%02lu", (unsigned long)(cur / 60 % 100),
                 (unsigned long)(cur % 60), (unsigned long)(e.secs / 60 % 100), (unsigned long)(e.secs % 60));
    } else if (e.state == 1) {
        snprintf(sub, sizeof(sub), "%02lu:%02lu  %s", (unsigned long)(e.secs / 60 % 100),
                 (unsigned long)(e.secs % 60), sz);
    } else if (e.state == 2) {
        snprintf(sub, sizeof(sub), "unsupported  %s", sz);
    } else {
        snprintf(sub, sizeof(sub), "--:--  %s", sz);
    }
    hp::drawListItemSub(lcd, row, e.name, sub, idx == _sel);
}

void AppVoiceRecorder::_drawListMsg(const char* text, uint16_t color) {
    auto& lcd = _device->Lcd;
    hp::drawToast(lcd, text, color == hp::COL_ERR ? hp::COL_ERR : hp::COL_DIM);
}

void AppVoiceRecorder::_drawList() {
    auto& lcd = _device->Lcd;
    lcd.fillScreen(hp::COL_BG);
    hp::drawChrome(lcd);
    char badge[12];
    snprintf(badge, sizeof(badge), "%d FILES", _count);
    hp::drawHeader(lcd, "RECORDINGS", badge, hp::COL_FG);
    _setFooter();
    if (!_list) {
        say(lcd, 16, 40, "Out of memory", hp::COL_ERR);
        return;
    }
    if (_count == 0) {
        const sd_guard::Status st = sd_guard::check();
        say(lcd, 16, 40, st == sd_guard::Status::Ok ? "No recordings yet" : sd_guard::message(st),
            st == sd_guard::Status::Ok ? hp::COL_DIM : hp::COL_ERR);
        return;
    }
    _fixScroll();
    for (int r = 0; r < hp::LIST2_VIS; ++r) _drawRow(_top + r);
    hp::drawScrollbar2(lcd, _count, _top, hp::LIST2_VIS);
    if (_lstate == L_CONFIRM) {
        hp::drawDialog(lcd, "Delete recording?", _list[_sel].name, hp::COL_ERR);
    }
}

void AppVoiceRecorder::_deleteSelected() {
    if (!_list || _sel < 0 || _sel >= _count) return;
    char path[48];
    snprintf(path, sizeof(path), "%s/%s", kDir, _list[_sel].name);
    const bool ok = sd_guard::check() == sd_guard::Status::Ok && SD_MMC.remove(path);
    _lstate = L_BROWSE;
    const int keep = _sel;
    _scanList();
    _sel = keep < _count ? keep : (_count ? _count - 1 : 0);
    _top = 0;
    _drawList();
    _drawListMsg(ok ? "Deleted" : "Delete failed", ok ? hp::COL_FG : hp::COL_ERR);
}

void AppVoiceRecorder::_listInput(bool aEdge, bool bTap, const bool d[4]) {
    if (d[I_LEFT]) {
        if (_lstate == L_CONFIRM) { _lstate = L_BROWSE; _drawList(); }
        else _leaveList();
        return;
    }
    if (_lstate == L_CONFIRM) {
        if (aEdge) _deleteSelected();
        else if (bTap) { _lstate = L_BROWSE; _drawList(); }
        return;
    }
    if (d[I_UP] || d[I_DOWN]) {
        if (_count > 0) {
            if (_lstate == L_PLAY) _playStop("", hp::COL_FG);
            const int old = _sel;
            _sel += d[I_DOWN] ? 1 : -1;
            if (_sel < 0) _sel = _count - 1;
            if (_sel >= _count) _sel = 0;
            const int oldTop = _top;
            _fixScroll();
            if (_top != oldTop) _drawList();
            else { _drawRow(old); _drawRow(_sel); }
        }
        return;
    }
    if (aEdge && _count > 0) {
        if (_lstate == L_PLAY) { _playStop("", hp::COL_FG); return; }
        _probeOne(_sel);
        if (_list[_sel].state != 1) { _drawRow(_sel); _drawListMsg("Unsupported WAV", hp::COL_ERR); return; }
        if (_playStart()) { _lstate = L_PLAY; _setFooter(); _drawRow(_sel); }
        return;
    }
    if (bTap && _count > 0) {
        if (_lstate == L_PLAY) _playStop("", hp::COL_FG);
        _lstate = L_CONFIRM;
        _setFooter();
        hp::drawDialog(_device->Lcd, "Delete recording?", _list[_sel].name, hp::COL_ERR);
    }
}

// ───────────────────────── main loop ─────────────────────────

void AppVoiceRecorder::onRunning() {
    const uint32_t now = millis();
    auto& b = _device->button;
    b.update(); b.tick();
    if (b.B.isLongPress()) { close(); return; }

    const bool st[6] = {
        b.Up.state() == Button_Class::PRESSED, b.Down.state() == Button_Class::PRESSED,
        b.Left.state() == Button_Class::PRESSED, b.Right.state() == Button_Class::PRESSED,
        b.A.state() == Button_Class::PRESSED, b.B.state() == Button_Class::PRESSED };
    bool edge[6];
    for (int i = 0; i < 6; ++i) edge[i] = st[i] && !_prev[i];
    // B acts on a short release; A/B that opened the app are ignored until released once.
    const bool bTap = !st[I_B] && _prev[I_B] && _b_armed && now - _b_since < 1000;
    if (edge[I_B]) _b_since = now;
    if (!st[I_B]) _b_armed = true;
    if (!st[I_A]) _a_armed = true;
    const bool aEdge = edge[I_A] && _a_armed;
    memcpy(_prev, st, sizeof(_prev));

    if (_busy) return;

    if (_mode == M_LIST) {
        _listInput(aEdge, bTap, edge);
        if (_mode != M_LIST) return;
        if (_lstate == L_PLAY) _playPump();
        else if (_lstate == L_BROWSE && _list && _probe < _count) {
            _probeOne(_probe);
            _drawRow(_probe);
            ++_probe;
        }
        return;
    }

    if (_recording) {
        if (usb_msc_is_active()) {
            _stopRecording("SD in USB mode", hp::COL_ERR, false);
        } else {
            _pump(false);
            if (!_capActive.load(std::memory_order_acquire)) _stopRecording("Capture failed", hp::COL_ERR);  // task ended on its own (e.g. buffer alloc failed)
            else if (_wroteErr) _stopRecording("SD write error", hp::COL_ERR);
            else if (_dataBytes >= kMaxBytes) _stopRecording("30 min limit reached", hp::COL_WARN);
            else if (now - _lastPatch >= kPatchMs) {
                _lastPatch = now;
                if (!_patchHeader()) _wroteErr = true;
            }
        }
    }
    if (aEdge) {
        if (_recording) _stopRecording("", hp::COL_FG);
        else _startRecording();
    } else if (edge[I_RIGHT]) {
        _enterList();
        if (_mode == M_LIST) return;
    }

    if (now - _lastUi >= kUiMs) {
        _lastUi = now;
        if (_recording) {
            const uint16_t pk = _peak.exchange(0, std::memory_order_relaxed);
            int pct = 0;
            if (pk > 0) {
                pct = (int)((20.0f * log10f(pk / 32768.0f) + 48.0f) * (100.0f / 48.0f));
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;
            }
            _level = pct > _level ? pct : (_level > 6 ? _level - 6 : 0);
        } else {
            _level = 0;
        }
        _drawDynamic(false);
    }
}

}
