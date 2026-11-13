/**
 * @file app_badusb.h
 * @author Mingo
 * @brief AppBadUSB — Bad USB v2.0 (Momentum-Firmware DuckyScript port)
 *        Full DuckyScript parser + USB HID keyboard/mouse/consumer-control
 *        via ESP32-S3 TinyUSB.  TUI style (same as App11).
 * @version 2.0
 * @date 2025-08-05
 * @copyright Copyright (c) 2025
 */
#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"
#include <vector>
#include <string>
#include "../app_common/hp_ui.h"

/* ── TUI colors: aliases to shared hp_ui palette (file scope) ── */
static constexpr uint16_t BU_BG        = hp::COL_BG;
static constexpr uint16_t BU_FG        = hp::COL_FG;
static constexpr uint16_t BU_FG_DIM    = hp::COL_DIM;
static constexpr uint16_t BU_ACCENT    = hp::COL_ACCENT;
static constexpr uint16_t BU_HIGHLIGHT = hp::COL_HL;

#include "../../system/usb_hid.h"
#include "../../system/usb_manager.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{

/* ── Execution states ── */
enum class ExecState : uint8_t {
    Idle,
    WaitConnect,   /* Script loaded, waiting for USB host to mount HID */
    Ready,         /* USB connected, awaiting [A] to start */
    Running,
    Paused,        /* User-toggled pause via [A] during run */
    Delay,
    StringDelay,
    WaitButton,
    Done,
    Error
};

/* ── HID transport mode (UI toggle via [B]) ── */
enum class BuMode : uint8_t { USB, BLE };

/* ── Page IDs ── */
enum class BuPage : uint8_t {
    FileList,
    Layouts,
    Running,
};

class AppBadUSB : public AppAbility {
public:
    AppBadUSB(DEVICES* device);
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    DEVICES* _device = nullptr;

    /* ── Page / scene ── */
    BuPage _page = BuPage::FileList;
    bool   _sceneDirty = true;
    void   _switchPage(BuPage p);

    /* ── TUI helpers (app_11 style) ── */
    void _drawHeader(const char* title);
    void _drawMenuItem(int y, int index, const char* text, bool selected);
    void _drawFooter(const char* left, const char* right);
    void _drawFooterBright4(const char* s1, const char* s2,
                            const char* s3, const char* s4);
    /* Evenly-distributed 3-segment footer: [Dir] left, [A] centre, [B] right. */
    void _drawFooterBright3(const char* dirHint, const char* aHint,
                            const char* bHint);
    void _drawMsgBox(const char* line1, const char* line2 = nullptr);
    void _drawConnIcon(bool connected);
    void _drawRunningStatic();
    void _drawRunningDynamic();
    void _drawRunningFooter();
    void _drawStateBadge();
    bool _isConnected() const;

    /* ── Scene handlers ── */
    void _enterFileList();
    void _runFileList();
    void _enterLayouts();
    void _runLayouts();
    void _enterRunning();
    void _updateRunning();

    /* ── Menu state ── */
    int _menuSel      = 0;
    int _menuCount    = 0;
    int _scrollOffset = 0;

    /* ── File listing ── */
    std::vector<std::string> _scriptFiles;
    std::string _selectedFile;
    void _scanScripts();

    /* ── Layout listing ── */
    std::vector<std::string> _layoutFiles;
    void _scanLayouts();

    /* ── Script lines ── */
    std::vector<std::string> _lines;
    size_t   _lineIdx    = 0;
    std::string _prevLine;       /* line currently being executed */
    std::string _repeatLine;     /* last repeatable line (for REPEAT) */

    /* ── Execution state ── */
    ExecState     _execState  = ExecState::Idle;
    uint32_t      _defDelay   = 0;
    uint32_t      _strDelay   = 0;
    uint32_t      _defStrDelay = 0;
    uint32_t      _repeatCnt  = 0;
    uint8_t       _keyHoldNb  = 0;
    unsigned long _delayEnd   = 0;
    unsigned long _lastDraw   = 0;

    /* string typing: STRING / STRINGLN / ALTSTRING all run in the StringDelay
     * state; with an effective delay of 0 it types a small chunk per update. */
    std::string   _printStr;
    size_t        _printPos   = 0;
    bool          _printAlt   = false;  /* ALTSTRING: type via Alt+numpad */
    void          _typeStrChar();

    /* REPEAT of a STRING line re-applies the one-shot STRING_DELAY that was
     * active when the line first ran. */
    bool          _repeatIsStr    = false;
    uint32_t      _repeatStrDelay = 0;

    /* Keys currently held by HOLD (one entry per hold); combos and SYSRQ never
     * release these. */
    uint8_t       _heldKeys[32] = {0};
    uint8_t       _heldCnt      = 0;
    bool          _isHeld(uint8_t k) const;
    void          _holdAdd(uint8_t k);
    void          _holdRemove(uint8_t k);
    void          _reassertHeld();
    void          _releaseUnheld(uint8_t k);
    void          _tapKeys(const uint8_t* seq, int n, uint32_t holdMs);

    /* Layout-aware single character: modifiers + key as _kbdPress codes. */
    int           _charSeq(char c, uint8_t* seq);
    static bool   _singleChar(const char* s, char& ch);

    /* error info */
    std::string   _errMsg;
    size_t        _errLine    = 0;

    unsigned long _execStartTime = 0;  /* millis() when script started */
    unsigned long _execEndTime   = 0;  /* millis() when Done/Error (0 = still running) */

    /* HID transport mode (UI toggle) */
    BuMode _mode = BuMode::USB;

    /* BLE keyboard state */
    bool   _bleStarted = false;
    bool   _bleConnected() const;
    void   _bleBegin();

    /* Keyboard layout (256-byte Momentum .kl file: 128 × uint16_le, low=HID, high=mod) */
    uint16_t    _layout[128] = {0};
    bool        _layoutLoaded = false;
    std::string _layoutName   = "en-US";
    bool        _loadLayout(const char* name);

    /* Cached last-drawn dynamic values, so we only repaint when they change */
    unsigned long _lastClkMs   = 0xFFFFFFFFul;
    int           _lastPctInt  = -1;
    size_t        _lastLineIdx = (size_t)-1;
    ExecState     _lastDrawnState = ExecState::Idle;
    BuMode        _lastDrawnMode  = BuMode::USB;
    bool          _lastConnected  = false;

    /* BLE CCCD settling: host needs ~500-1000 ms after link-up to subscribe
     * to notifications.  Zero = not started, nonzero = millis() deadline. */
    unsigned long _bleReadyAt = 0;

    /* ── Script control ── */
    bool    _loadScript(const char* path);
    void    _startExec();
    void    _stopExec();

    /* ── DuckyScript parser (Momentum port) ── */
    int32_t _parseLine(const char* line);

    /* parser helpers */
    static const char* _skipWs(const char* p);
    static bool        _isEnd(char c);
    static uint32_t    _cmdLen(const char* line);
    static bool        _parseUint(const char* s, uint32_t& v);
    static bool        _parseInt(const char* s, int32_t& v);
    static uint8_t     _nextMod(const char** p);

    /* lookup */
    static uint8_t  _keyByName(const char* name);
    static uint8_t  _modByName(const char* name);
    static uint16_t _mediaByName(const char* name);
    static uint8_t  _mouseByName(const char* name);

    /* ── HID wrappers ── */
    void _kbdPress(uint8_t k);
    void _kbdRelease(uint8_t k);
    void _kbdReleaseAll();
    void _kbdPrint(const char* s);
    void _kbdWrite(uint8_t c);
    void _mouseClick(uint8_t b);
    void _mousePress(uint8_t b);
    void _mouseRelease(uint8_t b);
    void _mouseMove(int8_t x, int8_t y);
    void _mouseScroll(int8_t scroll);
    void _consumerPress(uint16_t k);
    void _consumerRelease();

    /* ALTCHAR helpers */
    void _numlockOn();
    bool _numpadPress(char digit);
    void _altChar(const char* code);
    bool _altString(const char* param);
};

} // namespace MOONCAKE::APPS
