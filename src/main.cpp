// SteamFrameControls - Context-Aware Steam Frame Controller Plugin for Fallout 4 VR
// ==================================================================================
// Hooks IVRSystem::GetControllerState / GetControllerStateWithPose and tracks active
// Fallout 4 VR menus via RE::UI MenuOpenCloseEvent so extra Steam Frame inputs
// (Left D-Pad, Bumpers, Right X/Y, Right Menu) perform context-appropriate actions
// (e.g., D-Pad Left/Right switches category tabs in Container/Barter/Pip-Boy menus).

#include <windows.h>
#include <shlobj.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>
#include <algorithm>

// ============================================================================
// Minimal F4SEVR 0.6.21 ABI definitions
// ============================================================================
struct PluginInfo {
    enum { kInfoVersion = 1 };
    uint32_t infoVersion;
    const char* name;
    uint32_t version;
};

struct F4SEMessagingMessage {
    const char* sender;
    uint32_t type;
    uint32_t dataLen;
    void* data;
};

using F4SEEventCallback = void(*)(F4SEMessagingMessage* msg);

struct F4SEMessagingInterface {
    enum {
        kMessage_PostLoad = 0,
        kMessage_PostPostLoad = 1,
        kMessage_PreLoadGame = 2,
        kMessage_PostLoadGame = 3,
        kMessage_PreSaveGame = 4,
        kMessage_PostSaveGame = 5,
        kMessage_DeleteGame = 6,
        kMessage_InputLoaded = 7,
        kMessage_NewGame = 8,
        kMessage_GameLoaded = 9,
        kMessage_GameDataReady = 10
    };
    uint32_t interfaceVersion;
    bool (*RegisterListener)(uint32_t listener, const char* sender, F4SEEventCallback handler);
    bool (*Dispatch)(uint32_t sender, uint32_t messageType, void* data, uint32_t dataLen, const char* receiver);
};

struct F4SEInterface {
    uint32_t f4seVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    void* (*QueryInterface)(uint32_t id);
    uint32_t (*GetPluginHandle)(void);
    uint32_t (*GetReleaseIndex)(void);
};

constexpr uint32_t kInterface_Messaging = 1;

// ============================================================================
// Minimal OpenVR definitions (x64 Windows ABI)
// ============================================================================
#pragma pack(push, 8)
struct VRControllerAxis_t {
    float x;
    float y;
};

struct VRControllerState_t {
    uint32_t unPacketNum;
    uint64_t ulButtonPressed;
    uint64_t ulButtonTouched;
    VRControllerAxis_t rAxis[5];
};
#pragma pack(pop)

static_assert(sizeof(VRControllerState_t) == 64, "VRControllerState_t must be 64 bytes on x64");

constexpr uint64_t BitMask(int bit) { return 1ULL << bit; }

constexpr int kBtn_System          = 0;
constexpr int kBtn_ApplicationMenu = 1;
constexpr int kBtn_Grip            = 2;
constexpr int kBtn_A               = 7;
constexpr int kBtn_Axis0           = 32; // Thumbstick
constexpr int kBtn_Axis1           = 33; // Trigger
constexpr int kBtn_Axis2           = 34; // Grip / Carrier bit

constexpr int kRole_LeftHand  = 1;
constexpr int kRole_RightHand = 2;

// ============================================================================
// Logging
// ============================================================================
static HMODULE g_hModule = nullptr;
static std::mutex g_logMutex;
static FILE* g_logFile = nullptr;
static bool g_debugLog = false;

static void InitLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile) return;

    char path[MAX_PATH] = {};
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
        char docsA[MAX_PATH] = {};
        WideCharToMultiByte(CP_ACP, 0, docs, -1, docsA, MAX_PATH, nullptr, nullptr);
        CoTaskMemFree(docs);
        std::snprintf(path, sizeof(path), "%s\\My Games\\Fallout4VR\\F4SE\\SteamFrameControls.log", docsA);
        g_logFile = std::fopen(path, "w");
    }
    if (!g_logFile && g_hModule) {
        GetModuleFileNameA(g_hModule, path, MAX_PATH);
        char* dot = std::strrchr(path, '.');
        if (dot) std::strcpy(dot, ".log");
        g_logFile = std::fopen(path, "w");
    }
}

static void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_logFile, fmt, args);
    std::fputc('\n', g_logFile);
    std::fflush(g_logFile);
    va_end(args);
}

// ============================================================================
// Configuration (SteamFrameControls.ini)
// ============================================================================
enum class FrameButton : int {
    DpadLeft = 0,
    DpadRight,
    DpadUp,
    DpadDown,
    LeftBumper,
    RightX,
    RightY,
    RightBumper,
    RightMenu,
    Count
};

enum class TargetAction : int {
    None = 0,
    LTrigger,
    RTrigger,
    LGrip,
    RGrip,
    LX,
    LY,
    RA,
    RB,
    LStickClick,
    RStickClick,
    LStickUp,
    LStickDown,
    LStickLeft,
    LStickRight,
    RStickUp,
    RStickDown,
    RStickLeft,
    RStickRight
};

enum class MenuContext : int {
    Gameplay = 0,
    ContainerMenu,
    PipboyMenu,
    WorkshopMenu,
    OtherMenu,
    Count
};

struct ContextMapping {
    TargetAction map[static_cast<int>(FrameButton::Count)] = {};
};

static ContextMapping g_config[static_cast<int>(MenuContext::Count)];
static float g_stickMagnitude = 0.95f;

static TargetAction ParseAction(const char* str) {
    if (!str || !*str) return TargetAction::None;
    std::string s(str);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "l_trigger")     return TargetAction::LTrigger;
    if (s == "r_trigger")     return TargetAction::RTrigger;
    if (s == "l_grip")        return TargetAction::LGrip;
    if (s == "r_grip")        return TargetAction::RGrip;
    if (s == "l_x")           return TargetAction::LX;
    if (s == "l_y")           return TargetAction::LY;
    if (s == "r_a")           return TargetAction::RA;
    if (s == "r_b")           return TargetAction::RB;
    if (s == "l_stick_click") return TargetAction::LStickClick;
    if (s == "r_stick_click") return TargetAction::RStickClick;
    if (s == "l_stick_up")    return TargetAction::LStickUp;
    if (s == "l_stick_down")  return TargetAction::LStickDown;
    if (s == "l_stick_left")  return TargetAction::LStickLeft;
    if (s == "l_stick_right") return TargetAction::LStickRight;
    if (s == "r_stick_up")    return TargetAction::RStickUp;
    if (s == "r_stick_down")  return TargetAction::RStickDown;
    if (s == "r_stick_left")  return TargetAction::RStickLeft;
    if (s == "r_stick_right") return TargetAction::RStickRight;
    return TargetAction::None;
}

static void SetDefaultConfig() {
    // ContainerMenu / BarterMenu (r_a = Prev Tab, r_b = Next Tab in FO4VR Oculus Touch mode)
    auto& cm = g_config[static_cast<int>(MenuContext::ContainerMenu)].map;
    cm[static_cast<int>(FrameButton::DpadLeft)]    = TargetAction::RA;
    cm[static_cast<int>(FrameButton::DpadRight)]   = TargetAction::RB;
    cm[static_cast<int>(FrameButton::DpadUp)]      = TargetAction::LStickUp;
    cm[static_cast<int>(FrameButton::DpadDown)]    = TargetAction::LStickDown;
    cm[static_cast<int>(FrameButton::LeftBumper)]  = TargetAction::LStickLeft;
    cm[static_cast<int>(FrameButton::RightBumper)] = TargetAction::LStickRight;
    cm[static_cast<int>(FrameButton::RightX)]      = TargetAction::LX;
    cm[static_cast<int>(FrameButton::RightY)]      = TargetAction::LY;
    cm[static_cast<int>(FrameButton::RightMenu)]   = TargetAction::RGrip;

    // PipboyMenu (r_a = Prev Tab, r_b = Next Tab in FO4VR Oculus Touch mode)
    auto& pm = g_config[static_cast<int>(MenuContext::PipboyMenu)].map;
    pm[static_cast<int>(FrameButton::DpadLeft)]    = TargetAction::RA;
    pm[static_cast<int>(FrameButton::DpadRight)]   = TargetAction::RB;
    pm[static_cast<int>(FrameButton::DpadUp)]      = TargetAction::LStickUp;
    pm[static_cast<int>(FrameButton::DpadDown)]    = TargetAction::LStickDown;
    pm[static_cast<int>(FrameButton::LeftBumper)]  = TargetAction::LStickLeft;
    pm[static_cast<int>(FrameButton::RightBumper)] = TargetAction::LStickRight;
    pm[static_cast<int>(FrameButton::RightX)]      = TargetAction::LX;
    pm[static_cast<int>(FrameButton::RightY)]      = TargetAction::LY;
    pm[static_cast<int>(FrameButton::RightMenu)]   = TargetAction::RGrip;

    // WorkshopMenu / Crafting / Cooking
    auto& wm = g_config[static_cast<int>(MenuContext::WorkshopMenu)].map;
    wm[static_cast<int>(FrameButton::DpadLeft)]    = TargetAction::LStickLeft;
    wm[static_cast<int>(FrameButton::DpadRight)]   = TargetAction::LStickRight;
    wm[static_cast<int>(FrameButton::DpadUp)]      = TargetAction::LStickUp;
    wm[static_cast<int>(FrameButton::DpadDown)]    = TargetAction::LStickDown;
    wm[static_cast<int>(FrameButton::LeftBumper)]  = TargetAction::LTrigger;
    wm[static_cast<int>(FrameButton::RightBumper)] = TargetAction::RTrigger;
    wm[static_cast<int>(FrameButton::RightX)]      = TargetAction::LX;
    wm[static_cast<int>(FrameButton::RightY)]      = TargetAction::LY;
    wm[static_cast<int>(FrameButton::RightMenu)]   = TargetAction::RB;

    // OtherMenu
    auto& om = g_config[static_cast<int>(MenuContext::OtherMenu)].map;
    om[static_cast<int>(FrameButton::DpadLeft)]    = TargetAction::LStickLeft;
    om[static_cast<int>(FrameButton::DpadRight)]   = TargetAction::LStickRight;
    om[static_cast<int>(FrameButton::DpadUp)]      = TargetAction::LStickUp;
    om[static_cast<int>(FrameButton::DpadDown)]    = TargetAction::LStickDown;
    om[static_cast<int>(FrameButton::LeftBumper)]  = TargetAction::LTrigger;
    om[static_cast<int>(FrameButton::RightBumper)] = TargetAction::RTrigger;
    om[static_cast<int>(FrameButton::RightX)]      = TargetAction::LX;
    om[static_cast<int>(FrameButton::RightY)]      = TargetAction::LY;
    om[static_cast<int>(FrameButton::RightMenu)]   = TargetAction::RB;

    // Gameplay
    auto& gp = g_config[static_cast<int>(MenuContext::Gameplay)].map;
    gp[static_cast<int>(FrameButton::DpadLeft)]    = TargetAction::LX;
    gp[static_cast<int>(FrameButton::DpadRight)]   = TargetAction::RB;
    gp[static_cast<int>(FrameButton::DpadUp)]      = TargetAction::LTrigger;
    gp[static_cast<int>(FrameButton::DpadDown)]    = TargetAction::RStickDown;
    gp[static_cast<int>(FrameButton::LeftBumper)]  = TargetAction::LStickClick;
    gp[static_cast<int>(FrameButton::RightBumper)] = TargetAction::RStickClick;
    gp[static_cast<int>(FrameButton::RightX)]      = TargetAction::RGrip;
    gp[static_cast<int>(FrameButton::RightY)]      = TargetAction::RStickUp;
    gp[static_cast<int>(FrameButton::RightMenu)]   = TargetAction::LY;
}

static void LoadConfig() {
    SetDefaultConfig();
    if (!g_hModule) return;

    char iniPath[MAX_PATH] = {};
    GetModuleFileNameA(g_hModule, iniPath, MAX_PATH);
    char* dot = std::strrchr(iniPath, '.');
    if (dot) std::strcpy(dot, ".ini");

    if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
        Log("[Config] INI not found at %s; using built-in defaults", iniPath);
        return;
    }

    g_debugLog = GetPrivateProfileIntA("General", "bDebugLog", 0, iniPath) != 0;
    char buf[64] = {};
    if (GetPrivateProfileStringA("General", "fStickMagnitude", "0.95", buf, sizeof(buf), iniPath) > 0) {
        float v = static_cast<float>(std::atof(buf));
        if (v >= 0.3f && v <= 1.0f) g_stickMagnitude = v;
    }

    const char* sectionNames[static_cast<int>(MenuContext::Count)] = {
        "Gameplay", "ContainerMenu", "PipboyMenu", "WorkshopMenu", "OtherMenu"
    };
    const char* keyNames[static_cast<int>(FrameButton::Count)] = {
        "DpadLeft", "DpadRight", "DpadUp", "DpadDown", "LeftBumper",
        "RightX", "RightY", "RightBumper", "RightMenu"
    };

    for (int c = 0; c < static_cast<int>(MenuContext::Count); ++c) {
        for (int b = 0; b < static_cast<int>(FrameButton::Count); ++b) {
            if (GetPrivateProfileStringA(sectionNames[c], keyNames[b], "", buf, sizeof(buf), iniPath) > 0) {
                g_config[c].map[b] = ParseAction(buf);
            }
        }
    }

    Log("[Config] Loaded %s (bDebugLog=%d, fStickMagnitude=%.2f)", iniPath, g_debugLog ? 1 : 0, g_stickMagnitude);
}

// ============================================================================
// Fallout 4 VR 1.2.72.0 Menu Tracking (RE::UI + MenuOpenCloseEvent)
// ============================================================================
// Verified against VR Address Library version-1-2-72-0.csv:
//   RE::UI::GetSingleton()  -> ID 548587  -> RVA 0x05932320
//   RE::BSSpinLock::lock()  -> ID 1425657 -> RVA 0x00024E10
constexpr uintptr_t kRVA_UI_Singleton   = 0x05932320;
constexpr uintptr_t kRVA_BSSpinLockLock = 0x00024E10;

static std::mutex g_menuMutex;
static std::unordered_set<std::string> g_openMenus;
static std::atomic<int> g_activeContext{ static_cast<int>(MenuContext::Gameplay) };
static std::atomic<bool> g_menuSinkRegistered{ false };

static void UpdateActiveContextLocked() {
    MenuContext ctx = MenuContext::Gameplay;

    if (g_openMenus.count("ContainerMenu") || g_openMenus.count("BarterMenu")) {
        ctx = MenuContext::ContainerMenu;
    } else if (g_openMenus.count("PipboyMenu")) {
        ctx = MenuContext::PipboyMenu;
    } else if (g_openMenus.count("WorkshopMenu") || g_openMenus.count("CraftingMenu") || g_openMenus.count("CookingMenu")) {
        ctx = MenuContext::WorkshopMenu;
    } else if (g_openMenus.count("PauseMenu") || g_openMenus.count("DialogueMenu") ||
               g_openMenus.count("TerminalMenu") || g_openMenus.count("MessageBoxMenu") ||
               g_openMenus.count("MainMenu") || g_openMenus.count("VATSMenu") ||
               g_openMenus.count("FavoritesMenu") || g_openMenus.count("SleepWaitMenu") ||
               g_openMenus.count("LevelUpMenu") || g_openMenus.count("LooksMenu") ||
               g_openMenus.count("BookMenu") || g_openMenus.count("LockpickingMenu")) {
        ctx = MenuContext::OtherMenu;
    }

    g_activeContext.store(static_cast<int>(ctx), std::memory_order_release);
}

struct MenuOpenCloseEvent {
    void* menuNameEntry; // BSFixedString (Entry*)
    uint8_t opening;
};

static const char* ResolveBSFixedString(void* entryPtr) {
    uint8_t* entry = static_cast<uint8_t*>(entryPtr);
    int safety = 0;
    while (entry && safety++ < 8) {
        uint16_t flags = *reinterpret_cast<uint16_t*>(entry + 8);
        if ((flags >> 14) & 1) {
            entry = *reinterpret_cast<uint8_t**>(entry + 0x10);
        } else {
            break;
        }
    }
    return entry ? reinterpret_cast<const char*>(entry + 0x18) : "";
}

class MenuEventSink {
public:
    virtual ~MenuEventSink() = default;
    virtual uint32_t ProcessEvent(const MenuOpenCloseEvent& ev, void* /*source*/) {
        const char* name = ResolveBSFixedString(ev.menuNameEntry);
        if (name && *name) {
            std::lock_guard<std::mutex> lock(g_menuMutex);
            if (ev.opening) {
                g_openMenus.insert(name);
            } else {
                g_openMenus.erase(name);
            }
            UpdateActiveContextLocked();
            if (g_debugLog) {
                Log("[Menu] %s %s -> context=%d", name, ev.opening ? "OPEN" : "CLOSE", g_activeContext.load());
            }
        }
        return 0; // BSEventNotifyControl::kContinue
    }
};

static MenuEventSink g_menuSink;

struct BSTArrayRaw {
    void** data;
    uint32_t capacity;
    uint32_t pad;
    uint32_t size;
    uint32_t pad2;
};

struct BSTEventSourceRaw {
    uint32_t lockOwningThread;
    uint32_t lockCount;
    BSTArrayRaw sinks;
    BSTArrayRaw pendingRegisters;
    BSTArrayRaw pendingUnregisters;
    uint8_t notifying;
};

static void TryRegisterMenuSink() {
    if (g_menuSinkRegistered.load(std::memory_order_acquire)) return;

    auto exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!exeBase) return;

    void** uiSingletonPtr = reinterpret_cast<void**>(exeBase + kRVA_UI_Singleton);
    void* ui = *uiSingletonPtr;
    if (!ui) {
        Log("[Menu] UI singleton not ready yet");
        return;
    }

    auto* src = reinterpret_cast<BSTEventSourceRaw*>(reinterpret_cast<uint8_t*>(ui) + 0x18);
    using LockFn = void(__fastcall*)(void*, const char*);
    auto lockFn = reinterpret_cast<LockFn>(exeBase + kRVA_BSSpinLockLock);

    lockFn(src, nullptr);

    BSTArrayRaw* target = src->notifying ? &src->pendingRegisters : &src->sinks;
    bool alreadyPresent = false;
    if (target->data) {
        for (uint32_t i = 0; i < target->size; ++i) {
            if (target->data[i] == &g_menuSink) {
                alreadyPresent = true;
                break;
            }
        }
    }

    bool registered = alreadyPresent;
    if (!alreadyPresent && target->data && target->size < target->capacity) {
        target->data[target->size++] = &g_menuSink;
        registered = true;
    }

    // Unlock BSSpinLock (matches RE::BSSpinLock::unlock)
    if (src->lockCount == 1) {
        src->lockOwningThread = 0;
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&src->lockCount), 0, 1);
    } else {
        InterlockedDecrement(reinterpret_cast<volatile LONG*>(&src->lockCount));
    }

    if (registered) {
        g_menuSinkRegistered.store(true, std::memory_order_release);
        Log("[Menu] Registered MenuOpenCloseEvent sink (sinks=%u/%u)", target->size, target->capacity);
    } else {
        Log("[Menu] WARNING: Could not register MenuOpenCloseEvent sink (sinks=%u/%u, data=%p)",
            target->size, target->capacity, target->data);
    }
}

// ============================================================================
// Context-Aware Input Translation (supports cross-hand injection!)
// ============================================================================
static std::atomic<uint32_t> g_frameButtonsPressedMask{ 0 };

static bool IsLeftHandAction(TargetAction a) {
    switch (a) {
        case TargetAction::LTrigger:
        case TargetAction::LGrip:
        case TargetAction::LX:
        case TargetAction::LY:
        case TargetAction::LStickClick:
        case TargetAction::LStickUp:
        case TargetAction::LStickDown:
        case TargetAction::LStickLeft:
        case TargetAction::LStickRight:
            return true;
        default:
            return false;
    }
}

static bool ApplyActionToState(TargetAction a, bool isLeftHand, VRControllerState_t* st) {
    if (a == TargetAction::None) return false;
    if (IsLeftHandAction(a) != isLeftHand) return false;

    const float mag = g_stickMagnitude;
    switch (a) {
        case TargetAction::LTrigger:
        case TargetAction::RTrigger:
            st->ulButtonPressed |= BitMask(kBtn_Axis1);
            st->ulButtonTouched |= BitMask(kBtn_Axis1);
            st->rAxis[1].x = 1.0f;
            return true;
        case TargetAction::LGrip:
        case TargetAction::RGrip:
            st->ulButtonPressed |= BitMask(kBtn_Grip);
            st->ulButtonTouched |= BitMask(kBtn_Grip);
            st->rAxis[2].x = 1.0f;
            return true;
        case TargetAction::LX:
        case TargetAction::RA:
            st->ulButtonPressed |= BitMask(kBtn_A);
            st->ulButtonTouched |= BitMask(kBtn_A);
            return true;
        case TargetAction::LY:
        case TargetAction::RB:
            st->ulButtonPressed |= BitMask(kBtn_ApplicationMenu);
            st->ulButtonTouched |= BitMask(kBtn_ApplicationMenu);
            return true;
        case TargetAction::LStickClick:
        case TargetAction::RStickClick:
            st->ulButtonPressed |= BitMask(kBtn_Axis0);
            st->ulButtonTouched |= BitMask(kBtn_Axis0);
            return true;
        case TargetAction::LStickUp:
        case TargetAction::RStickUp:
            st->ulButtonTouched |= BitMask(kBtn_Axis0);
            if (std::fabs(st->rAxis[0].y) < mag) st->rAxis[0].y = mag;
            return true;
        case TargetAction::LStickDown:
        case TargetAction::RStickDown:
            st->ulButtonTouched |= BitMask(kBtn_Axis0);
            if (std::fabs(st->rAxis[0].y) < mag) st->rAxis[0].y = -mag;
            return true;
        case TargetAction::LStickLeft:
        case TargetAction::RStickLeft:
            st->ulButtonTouched |= BitMask(kBtn_Axis0);
            if (std::fabs(st->rAxis[0].x) < mag) st->rAxis[0].x = -mag;
            return true;
        case TargetAction::LStickRight:
        case TargetAction::RStickRight:
            st->ulButtonTouched |= BitMask(kBtn_Axis0);
            if (std::fabs(st->rAxis[0].x) < mag) st->rAxis[0].x = mag;
            return true;
        default:
            return false;
    }
}

static void ProcessControllerState(bool isLeftHand, VRControllerState_t* st) {
    if (!st) return;

    const uint64_t rawPressed = st->ulButtonPressed;
    const uint64_t rawTouched = st->ulButtonTouched;

    // 1. Extract our Steam Frame carrier bits (100% Oculus Touch-compatible!) and strip them
    uint32_t handBits = 0;
    uint32_t handMask = 0;

    if (isLeftHand) {
        handMask = (1u << static_cast<int>(FrameButton::DpadLeft))  |
                   (1u << static_cast<int>(FrameButton::DpadRight)) |
                   (1u << static_cast<int>(FrameButton::DpadUp))    |
                   (1u << static_cast<int>(FrameButton::DpadDown))  |
                   (1u << static_cast<int>(FrameButton::LeftBumper))|
                   (1u << static_cast<int>(FrameButton::RightMenu)); // Left View reuses RightMenu slot (Pause/Cancel)

        // SteamVR sets both pressed AND touched for a pressed button, so only treat
        // touch-without-press as DpadUp / DpadDown to avoid firing two directions at once.
        const bool pA   = (st->ulButtonPressed & BitMask(kBtn_A)) != 0;
        const bool pApp = (st->ulButtonPressed & BitMask(kBtn_ApplicationMenu)) != 0;
        const bool tA   = (st->ulButtonTouched & BitMask(kBtn_A)) != 0;
        const bool tApp = (st->ulButtonTouched & BitMask(kBtn_ApplicationMenu)) != 0;

        if (pA)        handBits |= (1u << static_cast<int>(FrameButton::DpadLeft));
        else if (tA)   handBits |= (1u << static_cast<int>(FrameButton::DpadUp));

        if (pApp)      handBits |= (1u << static_cast<int>(FrameButton::DpadRight));
        else if (tApp) handBits |= (1u << static_cast<int>(FrameButton::DpadDown));

        if (st->ulButtonPressed & BitMask(kBtn_Axis2))  handBits |= (1u << static_cast<int>(FrameButton::LeftBumper));
        if (st->ulButtonTouched & BitMask(kBtn_System)) handBits |= (1u << static_cast<int>(FrameButton::RightMenu));

        // Strip carrier bits before injecting context-aware actions
        st->ulButtonPressed &= ~(BitMask(kBtn_A) | BitMask(kBtn_ApplicationMenu) | BitMask(kBtn_Axis2));
        st->ulButtonTouched &= ~(BitMask(kBtn_A) | BitMask(kBtn_ApplicationMenu) | BitMask(kBtn_System));
    } else {
        handMask = (1u << static_cast<int>(FrameButton::RightX)) |
                   (1u << static_cast<int>(FrameButton::RightY));

        if (st->ulButtonPressed & BitMask(kBtn_Axis2))  handBits |= (1u << static_cast<int>(FrameButton::RightX));
        if (st->ulButtonTouched & BitMask(kBtn_System)) handBits |= (1u << static_cast<int>(FrameButton::RightY));

        // Strip carrier bits
        st->ulButtonPressed &= ~BitMask(kBtn_Axis2);
        st->ulButtonTouched &= ~BitMask(kBtn_System);
    }

    // Atomically update this hand's slice of the combined pressed bitmask
    uint32_t prev = g_frameButtonsPressedMask.load(std::memory_order_relaxed);
    uint32_t next = 0;
    do {
        next = (prev & ~handMask) | (handBits & handMask);
    } while (!g_frameButtonsPressedMask.compare_exchange_weak(prev, next, std::memory_order_acq_rel));

    int ctxIdx = g_activeContext.load(std::memory_order_acquire);
    if (ctxIdx < 0 || ctxIdx >= static_cast<int>(MenuContext::Count)) {
        ctxIdx = static_cast<int>(MenuContext::Gameplay);
    }

    bool modified = false;
    if (next != 0) {
        const auto& mapping = g_config[ctxIdx].map;
        for (int b = 0; b < static_cast<int>(FrameButton::Count); ++b) {
            if (next & (1u << b)) {
                if (ApplyActionToState(mapping[b], isLeftHand, st)) {
                    modified = true;
                }
            }
        }

        if (modified) {
            static std::atomic<uint32_t> s_synthPacketOffset{ 0 };
            st->unPacketNum += (++s_synthPacketOffset & 0xFFFFu);
        }
    }

    if (g_debugLog) {
        static uint64_t s_lastPressed[2] = { 0, 0 };
        static uint64_t s_lastTouched[2] = { 0, 0 };
        static uint32_t s_lastMask[2]    = { 0, 0 };
        int idx = isLeftHand ? 0 : 1;
        if (rawPressed != s_lastPressed[idx] || rawTouched != s_lastTouched[idx] || next != s_lastMask[idx]) {
            s_lastPressed[idx] = rawPressed;
            s_lastTouched[idx] = rawTouched;
            s_lastMask[idx]    = next;
            Log("[State %s] ctx=%d rawP=0x%016llX rawT=0x%016llX mask=0x%03X -> outP=0x%016llX ax0=(%.2f,%.2f) ax1=%.2f",
                isLeftHand ? "L" : "R", ctxIdx,
                static_cast<unsigned long long>(rawPressed),
                static_cast<unsigned long long>(rawTouched),
                next,
                static_cast<unsigned long long>(st->ulButtonPressed),
                st->rAxis[0].x, st->rAxis[0].y, st->rAxis[1].x);
        }
    }
}

// ============================================================================
// OpenVR IVRSystem Hooking (IAT + VTable, chains cleanly with other mods)
// ============================================================================
using VR_GetGenericInterface_t = void*(__cdecl*)(const char* pchInterfaceVersion, int* peError);
using GetControllerRole_t      = int(__fastcall*)(void* self, uint32_t unDeviceIndex);
using GetControllerState_t     = bool(__fastcall*)(void* self, uint32_t unDeviceIndex, VRControllerState_t* pState, uint32_t unSize);
using GetControllerStatePose_t = bool(__fastcall*)(void* self, int eOrigin, uint32_t unDeviceIndex, VRControllerState_t* pState, uint32_t unSize, void* pPose);

static VR_GetGenericInterface_t g_origGetGenericInterface = nullptr;
static GetControllerState_t     g_origGetControllerState = nullptr;
static GetControllerStatePose_t g_origGetControllerStateWithPose = nullptr;
static int                      g_roleSlot = 19; // IVRSystem_016..021
static std::mutex               g_hookMutex;
static bool                     g_vtableHooked = false;

static int QueryHandRole(void* self, uint32_t unDeviceIndex) {
    if (!self || g_roleSlot < 0) return 0;
    void** vtbl = *reinterpret_cast<void***>(self);
    auto fn = reinterpret_cast<GetControllerRole_t>(vtbl[g_roleSlot]);
    return fn ? fn(self, unDeviceIndex) : 0;
}

static bool __fastcall Hooked_GetControllerState(void* self, uint32_t unDeviceIndex, VRControllerState_t* pState, uint32_t unSize) {
    bool ok = g_origGetControllerState ? g_origGetControllerState(self, unDeviceIndex, pState, unSize) : false;
    if (ok && pState && unSize >= sizeof(VRControllerState_t)) {
        int role = QueryHandRole(self, unDeviceIndex);
        static std::atomic<bool> s_loggedFirst{ false };
        if (!s_loggedFirst.exchange(true)) {
            Log("[OpenVR] First GetControllerState call: dev=%u role=%d size=%u", unDeviceIndex, role, unSize);
        }
        if (role == kRole_LeftHand || role == kRole_RightHand) {
            ProcessControllerState(role == kRole_LeftHand, pState);
        }
    }
    return ok;
}

static bool __fastcall Hooked_GetControllerStateWithPose(void* self, int eOrigin, uint32_t unDeviceIndex, VRControllerState_t* pState, uint32_t unSize, void* pPose) {
    bool ok = g_origGetControllerStateWithPose ? g_origGetControllerStateWithPose(self, eOrigin, unDeviceIndex, pState, unSize, pPose) : false;
    if (ok && pState && unSize >= sizeof(VRControllerState_t)) {
        int role = QueryHandRole(self, unDeviceIndex);
        static std::atomic<bool> s_loggedFirstPose{ false };
        if (!s_loggedFirstPose.exchange(true)) {
            Log("[OpenVR] First GetControllerStateWithPose call: dev=%u role=%d size=%u", unDeviceIndex, role, unSize);
        }
        if (role == kRole_LeftHand || role == kRole_RightHand) {
            ProcessControllerState(role == kRole_LeftHand, pState);
        }
    }
    return ok;
}

static void HookIVRSystemVtable(void* iface, const char* versionStr) {
    if (!iface || !versionStr) return;
    std::lock_guard<std::mutex> lock(g_hookMutex);
    if (g_vtableHooked) return;

    int stateSlot = -1;
    int poseSlot  = -1;
    int roleSlot  = -1;

    if (std::strstr(versionStr, "IVRSystem_019") ||
        std::strstr(versionStr, "IVRSystem_020") ||
        std::strstr(versionStr, "IVRSystem_021") ||
        std::strstr(versionStr, "IVRSystem_023")) {
        stateSlot = 34; poseSlot = 35; roleSlot = 19;
    } else if (std::strstr(versionStr, "IVRSystem_016") ||
               std::strstr(versionStr, "IVRSystem_017") ||
               std::strstr(versionStr, "IVRSystem_022")) {
        stateSlot = 33; poseSlot = 34; roleSlot = 19;
    } else if (std::strstr(versionStr, "IVRSystem_014") ||
               std::strstr(versionStr, "IVRSystem_015")) {
        stateSlot = 32; poseSlot = 33; roleSlot = 18;
    } else {
        return;
    }

    void** vtbl = *reinterpret_cast<void***>(iface);
    if (!vtbl) return;

    if (vtbl[stateSlot] == reinterpret_cast<void*>(&Hooked_GetControllerState)) {
        g_vtableHooked = true;
        return;
    }

    DWORD oldProt = 0;
    if (VirtualProtect(&vtbl[stateSlot], sizeof(void*) * 2, PAGE_READWRITE, &oldProt)) {
        g_origGetControllerState         = reinterpret_cast<GetControllerState_t>(vtbl[stateSlot]);
        g_origGetControllerStateWithPose = reinterpret_cast<GetControllerStatePose_t>(vtbl[poseSlot]);
        g_roleSlot = roleSlot;

        vtbl[stateSlot] = reinterpret_cast<void*>(&Hooked_GetControllerState);
        vtbl[poseSlot]  = reinterpret_cast<void*>(&Hooked_GetControllerStateWithPose);

        VirtualProtect(&vtbl[stateSlot], sizeof(void*) * 2, oldProt, &oldProt);
        g_vtableHooked = true;
        Log("[OpenVR] Hooked %s vtable (slots %d/%d, role slot %d)", versionStr, stateSlot, poseSlot, roleSlot);
    } else {
        Log("[OpenVR] ERROR: VirtualProtect failed on %s vtable", versionStr);
    }
}

static void* __cdecl Hooked_VR_GetGenericInterface(const char* pchInterfaceVersion, int* peError) {
    void* iface = g_origGetGenericInterface ? g_origGetGenericInterface(pchInterfaceVersion, peError) : nullptr;
    if (iface && pchInterfaceVersion && std::strncmp(pchInterfaceVersion, "IVRSystem_", 10) == 0) {
        HookIVRSystemVtable(iface, pchInterfaceVersion);
    }
    return iface;
}

static bool PatchIAT(HMODULE targetMod, const char* dllName, const char* funcName, void* newFunc, void** outOrig) {
    if (!targetMod) return false;
    auto base = reinterpret_cast<uint8_t*>(targetMod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char* modName = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(modName, dllName) != 0) continue;

        auto* origThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        auto* thunk     = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);

        for (; origThunk->u1.AddressOfData; ++origThunk, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;
            auto* imp = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + origThunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(imp->Name), funcName) == 0) {
                DWORD oldProt = 0;
                if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProt)) {
                    if (outOrig) *outOrig = reinterpret_cast<void*>(thunk->u1.Function);
                    thunk->u1.Function = reinterpret_cast<ULONG_PTR>(newFunc);
                    VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProt, &oldProt);
                    return true;
                }
            }
        }
    }
    return false;
}

static void TryDirectHookOpenVR() {
    if (g_vtableHooked) return;
    HMODULE openvr = GetModuleHandleA("openvr_api.dll");
    if (!openvr) return;
    auto getIface = reinterpret_cast<VR_GetGenericInterface_t>(GetProcAddress(openvr, "VR_GetGenericInterface"));
    if (!getIface) return;

    const char* versions[] = { "IVRSystem_019", "IVRSystem_017", "IVRSystem_016", "IVRSystem_020", "IVRSystem_021" };
    for (const char* v : versions) {
        int err = 0;
        void* iface = getIface(v, &err);
        if (iface && err == 0) {
            HookIVRSystemVtable(iface, v);
            break;
        }
    }
}

static void OnF4SEMessage(F4SEMessagingMessage* msg) {
    if (!msg) return;
    if (msg->type == F4SEMessagingInterface::kMessage_GameDataReady ||
        msg->type == F4SEMessagingInterface::kMessage_InputLoaded ||
        msg->type == F4SEMessagingInterface::kMessage_PostLoadGame ||
        msg->type == F4SEMessagingInterface::kMessage_NewGame) {
        TryRegisterMenuSink();
        TryDirectHookOpenVR();
    }
}

// ============================================================================
// DLL Entry & F4SEVR Exports
// ============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

extern "C" {

bool F4SEPlugin_Query(const F4SEInterface* f4se, PluginInfo* info) {
    InitLog();
    Log("==== SteamFrameControls 1.0.0 ====");

    if (!info) return false;
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name        = "SteamFrameControls";
    info->version     = 1;

    if (!f4se) return false;
    if (f4se->isEditor) {
        Log("[Query] Editor detected; skipping");
        return false;
    }

    Log("[Query] F4SE version=0x%08X, runtime=0x%08X", f4se->f4seVersion, f4se->runtimeVersion);
    return true;
}

bool F4SEPlugin_Load(const F4SEInterface* f4se) {
    InitLog();
    LoadConfig();

    void* orig = nullptr;
    if (PatchIAT(GetModuleHandleA(nullptr), "openvr_api.dll", "VR_GetGenericInterface",
                 reinterpret_cast<void*>(&Hooked_VR_GetGenericInterface), &orig)) {
        g_origGetGenericInterface = reinterpret_cast<VR_GetGenericInterface_t>(orig);
        Log("[OpenVR] Installed IAT hook on Fallout4VR.exe -> VR_GetGenericInterface");
    } else {
        Log("[OpenVR] IAT hook not applied (will try direct hook when OpenVR initializes)");
    }

    if (f4se && f4se->QueryInterface && f4se->GetPluginHandle) {
        auto* messaging = static_cast<F4SEMessagingInterface*>(f4se->QueryInterface(kInterface_Messaging));
        if (messaging && messaging->RegisterListener) {
            messaging->RegisterListener(f4se->GetPluginHandle(), "F4SE", OnF4SEMessage);
            Log("[F4SE] Registered F4SE message listener");
        }
    }

    return true;
}

} // extern "C"
