// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/**
 * @file voice_recorder.h
 * @brief Voice Recorder (app_13): 16 kHz mono WAV recording to SD, plus playback.
 *
 * Audio bus ownership: the ES7210 mic (I2S1) and the ES8311 speaker (I2S0)
 * share MCLK/BCLK/WS and are never installed together. onOpen suspends the
 * system sound; onClose is the only place that resumes it.
 */
#pragma once
#include <mooncake.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <FS.h>
#include "../../bsp/devices.h"
#include "../../bsp/audio/ES7210_Class.hpp"
#include "../app_common/hp_ui.h"
#include "wav_format.h"

namespace MOONCAKE::APPS {
class AppVoiceRecorder : public mooncake::AppAbility {
public:
    explicit AppVoiceRecorder(DEVICES* device);
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

    /* Called from Launcher while the persistent app instance drains a timed
       capture close after Mooncake has returned to the GUI. */
    static void serviceDeferredClose(DEVICES* device);
    static bool closePending();

private:
    enum Idx { I_UP, I_DOWN, I_LEFT, I_RIGHT, I_A, I_B };

    DEVICES* _device;
    bool _busy = false;                 // system sound would not release the audio bus
    bool _prev[6] = {};
    bool _a_armed = false, _b_armed = false;
    uint32_t _b_since = 0;

    // ── UI ──
    char _msg[40] = {};
    uint16_t _msgColor = 0;
    bool _msgDirty = true, _redraw = true;
    uint32_t _lastUi = 0;
    int _shownSec = -1, _shownLevel = -1, _level = 0;
    uint32_t _shownDropped = 0xFFFFFFFFu;
    bool _shownRec = false;

    // ── Recording ──
    bool _recording = false;
    ES7210_Class _codec;
    bool _codecReady = false, _i2sReady = false;
    std::atomic<bool> _capActive{false};
    std::atomic<bool> _taskRun{false};
    bool _closePending = false;
    bool _closing = false;
    bool _draining = false;
    int16_t* _ring = nullptr;           // PSRAM, kRingSamples int16
    int16_t* _stage = nullptr;          // internal, kStageSamples int16
    std::atomic<uint32_t> _head{0}, _tail{0}, _dropped{0};
    std::atomic<uint16_t> _peak{0};
    File _file;
    char _path[48] = {};
    uint32_t _dataBytes = 0;
    uint32_t _lastPatch = 0;
    bool _wroteErr = false;

    // ── List / playback ──
    enum Mode : uint8_t { M_REC, M_LIST };
    enum LState : uint8_t { L_BROWSE, L_PLAY, L_CONFIRM };
    struct Entry { char name[32]; uint32_t size; uint32_t secs; uint8_t state; };  // state 0 pending, 1 ok, 2 bad
    Mode _mode = M_REC;
    LState _lstate = L_BROWSE;
    Entry* _list = nullptr;             // PSRAM
    int _count = 0, _sel = 0, _top = 0, _probe = 0;
    File _pf;
    wavfmt::Info _pinfo{};
    uint32_t _premain = 0, _pplayed = 0;
    int16_t* _pbuf = nullptr;           // internal
    bool _spkOpen = false;
    int _shownPlaySec = -1;

    void _say(const char* text, uint16_t color);
    void _drawStatic();
    void _drawDynamic(bool force);
    void _enterList();
    void _leaveList();
    void _scanList();
    void _probeOne(int idx);
    void _drawList();
    void _drawRow(int idx);
    void _drawListMsg(const char* text, uint16_t color);
    void _fixScroll();
    void _listInput(bool aEdge, bool bTap, const bool dirEdge[4]);
    bool _playStart();
    void _playStop(const char* why, uint16_t color);
    void _playPump();
    void _deleteSelected();
    void _setFooter();

    bool _micStart();
    void _micStop();
    bool _startRecording();
    void _stopRecording(const char* why, uint16_t color, bool touchCard = true);
    void _pump(bool all);
    bool _patchHeader();
    bool _makePath();
    void _releaseRecordBuffers();
    void _finishClose();
    static void _capEntry(void* arg);
    void _capLoop();
};
}
