/**
 * @file infrared.h
 * @author Mingo
 * @brief App09 — Infrared (Learn / Saved Remotes / Universal Remote)
 *        Flipper-style TUI  •  翠绿 + 白色色调
 * @version 1.0
 * @date 2025-08-05
 * @copyright Copyright (c) 2025
 */
#pragma once
#include <mooncake.h>
#include "../../bsp/devices.h"
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <SD_MMC.h>
#include <FS.h>
#include <vector>
#include <cstdio>
#include <algorithm>
#include "../app_common/hp_ui.h"
#include "../app_common/sd_guard.h"
#include "infrared_stream.h"

using namespace mooncake;

namespace MOONCAKE::APPS
{
    /* ── Flipper-compatible .ir signal storage ── */
    struct IrSignal {
        char     name[32];
        bool     isRaw;
        /* decoded */
        decode_type_t protocol;
        uint64_t value;
        uint16_t bits;
        uint32_t address;
        uint32_t command;
        /* raw */
        std::vector<uint16_t> rawData;   /* µs timings */
        uint32_t frequency;              /* carrier, default 38000 */
    };

    struct IrRemote {
        char     filename[64];
        char     path[128];
        std::vector<IrSignal> signals;
    };

    /* ── Scene IDs ── */
    enum class IrScene : uint8_t {
        MainMenu,
        LearnWait,
        LearnResult,
        LearnSaveLocation,
        LearnSaveFolderPicker,
        LearnSaveFolderName,
        LearnSaveName,
        LearnSaveExistingConfirm,
        RemoteList,
        DeleteConfirm,
        RemoteView,
        UniversalMenu,
        UniversalTV,
        TVBGone,        /* Iterates all signals in tv.ir like TV-B-Gone */
        Loading,
        Sending,
    };

    /* ── TUI colors ── */
    static constexpr uint16_t COL_BG        = hp::COL_BG;
    static constexpr uint16_t COL_FG        = hp::COL_FG;
    static constexpr uint16_t COL_FG_DIM    = hp::COL_DIM;
    static constexpr uint16_t COL_ACCENT    = hp::COL_ACCENT;
    static constexpr uint16_t COL_HIGHLIGHT = hp::COL_HL;
    static constexpr uint16_t COL_ERR       = hp::COL_ERR;

    /* ── App class ── */
    class App09 : public AppAbility {
    public:
        App09(DEVICES* device);
        void onOpen() override;
        void onRunning() override;
        void onClose() override;

    private:
        DEVICES* _device = nullptr;

        /* ── Scene stack ── */
        IrScene _scene = IrScene::MainMenu;
        IrScene _prevScene = IrScene::MainMenu;
        IrScene _loadOrigin = IrScene::MainMenu;
        bool    _sceneDirty = true;

        void _switchScene(IrScene s);

        /* ── TUI helpers ── */
        void _drawHeader(const char* title);
        void _drawMenuItem(int y, int index, const char* text, bool selected);
        void _drawMenuItem2(int y, int index, const char* title, const char* sub, bool selected);
        void _drawFooter3(const char* dirHint, const char* aHint, const char* bHint);
        void _drawMsgBox(const char* line1, const char* line2 = nullptr);
        void _drawNameEditor();

        /* ── Scene handlers ── */
        void _enterMainMenu();
        void _runMainMenu();

        void _enterLearnWait();
        void _runLearnWait();

        void _enterLearnResult();
        void _runLearnResult();

        void _enterLearnSaveName();
        void _runLearnSaveName();
        void _enterLearnSaveLocation();
        void _runLearnSaveLocation();
        void _enterLearnSaveFolderPicker();
        void _runLearnSaveFolderPicker();
        void _enterLearnSaveFolderName();
        void _runLearnSaveFolderName();
        void _enterLearnSaveExistingConfirm();
        void _runLearnSaveExistingConfirm();

        void _enterRemoteList();
        void _runRemoteList();
        void _goUpRemoteDir();
        void _enterDeleteConfirm();
        void _runDeleteConfirm();

        void _enterRemoteView();
        void _runRemoteView();

        void _enterUniversalMenu();
        void _runUniversalMenu();

        void _enterUniversalTV();
        void _runUniversalTV();
        void _runUniversalBlast(const char* triggerLabel);

        void _enterTVBGone();
        void _runTVBGone();
        void _drawTVBGoneFrame();
        void _drawTVBGoneFooter();

        void _enterSending();
        void _runSending();

        /* ── File I/O (Flipper .ir format) ── */
        void _beginLoad(const char* path, IrRemote& remote, int maxSignals, const char* filterName,
                        IrScene successScene, const char* purpose);
        bool _stepLoad();
        void _cancelLoad();
        void _finishLoad(bool success);
        void _parseLoadLine(const char* line);
        void _commitLoadSignal();
        void _resetLoadSignal(IrSignal& s);
        bool _saveSignalToFile(const char* dir, const char* remoteName, const IrSignal& sig);
        bool _appendSignalToFile(const char* path, const IrSignal& sig);
        void _listIrFiles(const char* dir, std::vector<String>& out, bool includeFolders = false);
        bool _isDirectoryEmpty(const char* path);

        /* ── IR hardware ── */
        IRrecv*  _irRecv  = nullptr;
        IRsend*  _irSend  = nullptr;

        bool _txSignal(const IrSignal& sig);
        void _startRx();
        void _stopRx();

        /* ── State ── */
        int  _menuSel      = 0;
        int  _menuCount    = 0;
        int  _scrollOffset = 0;

        IrSignal  _learnedSig;
        bool      _learnTruncated = false;   /* raw capture overflowed the RX buffer */
        sd_guard::Status _sdStatus = sd_guard::Status::Ok;
        IrRemote  _currentRemote;
        std::vector<String> _fileList;
        char      _remoteDir[96];   /* current folder being browsed in Saved Remotes, e.g. "/infrared" or "/infrared/tv" */
        char      _saveDir[96];     /* destination for a learned signal */
        char      _savePickerDir[96];
        char      _savePath[128];
        char      _deletePath[128];
        bool      _deleteFolder = false;
        bool      _saveFolderPending = false;
        bool      _restoreFolderName = false;
        bool      _preserveSignalName = false;

        /* Incremental loader.  The parser owns a stdio stream so EOF and
         * read errors are distinguishable on SD/FAT.  _loadResult is never
         * published to the requested destination until the whole file passes. */
        FILE* _loadFile = nullptr;
        infrared_stream::Reader* _loadReader = nullptr;
        IrRemote* _loadDestination = nullptr;
        IrRemote _loadResult;
        IrSignal _loadSignal;
        char _loadPath[128] = {};
        char _loadFilter[32] = {};
        char _loadProto[24] = {};
        char _loadPurpose[24] = {};
        size_t _loadBytes = 0;
        size_t _loadRawBytes = 0;
        uint32_t _loadRawUsec = 0;
        int _loadMaxSignals = 0;
        uint16_t _loadCurrentRawCount = 0;
        bool _loadInSignal = false;
        bool _loadValidSignal = true;
        bool _loadSawValue = false;
        bool _loadSawBits = false;
        bool _loadEOF = false;
        bool _loadFailed = false;
        bool _loadCancelled = false;
        IrScene _loadSuccessScene = IrScene::RemoteView;
        const char* _loadError = nullptr;

        /* save-name editor */
        char _editBuf[24];
        int  _editPos;
        int  _editCharIdx;
        int  _vkSel = 0;

        /* Learn page icon cache (PNG bytes loaded from SD once) */
        uint8_t* _learnIconPng = nullptr;
        size_t   _learnIconLen = 0;
        bool     _learnIconTried = false;

        /* universal remote */
        std::vector<String>  _univActions;      /* unique signal names from loaded .ir */
        std::vector<int>     _univBlastQ;       /* indices into _currentRemote.signals to blast */
        int                  _univBlastIdx = 0; /* current position in _univBlastQ */
        bool                 _univBlasting = false;
        char                 _univBlastName[32]; /* action being blasted */
        char                 _univCatTitle[32];  /* category label for header */

        /* virtual-remote grid navigation (directional, not touch) */
        int  _vbSel = 0;            /* selected button index in current grid */

        /* TV-B-Gone state */
        enum class TvbgState : uint8_t { Idle, Running, Paused };
        TvbgState     _tvbgState    = TvbgState::Idle;
        int           _tvbgIdx       = 0;     /* next signal index to send / currently displayed */
        int           _tvbgTotal     = 0;     /* total signals in tv.ir */
        bool          _tvbgPaused    = false; /* deprecated, kept for ABI; mirrors state==Paused */
        unsigned long _tvbgLastSend  = 0;     /* millis() of last send */
        unsigned long _tvbgGapMs     = 250;   /* gap between sends */

        /* sending overlay */
        const char* _sendLabel = nullptr;
        uint32_t    _sendStart = 0;
    };
}
