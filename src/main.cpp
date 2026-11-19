// Radiance - Player Light Source: the player gives off light. Client only, no items: one of the game's own light-only
// effects, started at the local player and moved with them each frame. Greeneye Loader mod (API 2).
//
// Everything is found at start by byte signature or by name; nothing is a fixed address (re/atlas/lights.md):
//   - the effect system's create / destroy / move / set-parameters functions (signatures), and where the renderer keeps
//     its effect manager and the loaded-effect table (read out of the game's own code),
//   - the frame's staging call (redirected; it hands us the renderer once a frame, as the map mod's G G copies use),
//   - the local player's RenderTransform: the ECS system "player_camera_transform" by name, its row iterator calls
//     and its "is this the local player" call (redirected), the component's place in the row from the system's
//     own name lists,
//   - the effect's parameters by their names' hashes (light_intensity, light_radius, light_color...).
// If any of it is missing the mod stays off and says so in the loader's log.
#include "greeneye.h"
#include "input_binds.h"
#include "hidden_chat.h"
#include "reflect.h"
#include "strings.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr char kVersion[] = "1.0.0";

// ---- the game's types (reflected: keen::RenderLight 28 bytes, keen::WorldTransform 56 bytes) ----
#pragma pack(push, 4)
struct WorldTransform {
    int64_t position[3]; // 32.32 fixed point metres
    float orientation[4];
    float scale[3];
    uint32_t pad;
};
#pragma pack(pop)
static_assert(sizeof(WorldTransform) == 56);
struct Span {
    const void* data;
    uint64_t count;
};
struct Field {
    const char* text;
    uint64_t length;
};

using StageFn = void (*)(void* renderer);
using NextFn = bool (*)(void* system, void** row, size_t size);
using IsLocalFn = bool (*)(uint32_t entity, void* localPlayerData);

// The effect system (RVAs of the 2026-11-04 build; Auto Loot's flames and Town Portals' far view use the same calls).
// create (0x197de0): (out handle, manager, effect GUID, transform, settings: 64 parameter bytes, +0x40 "use these").
constexpr char kFxCreateSig[] = "48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 20 48 8B 6A 08 48 8B FA 48 8B 52 28 49 8B "
                                "F1 4D 8B D8 4C 8B F1";
constexpr char kFxDestroySig[] = "85 D2 0F 84 ?? ?? ?? ?? 48 8B C4 56 57 48 81 EC E8 00 00 00 48 8B 71 08 48 8B F9 0F B7 CA 48 3B 4F 58 "
                                 "0F 83 ?? ?? ?? ?? 48 89 58 08";
constexpr char kFxMoveSig[] = "48 83 EC 28 4D 8B C8 4C 8B D1 85 D2 0F 84 ?? ?? ?? ?? 44 0F B7 C2 4C 3B 41 58 0F 83 ?? ?? ?? ?? 4B 8D 0C "
                              "40 4D 8B 42 50 48 C1 E1 06";
constexpr char kFxParametersSig[] = "85 D2 0F 84 ?? ?? ?? ?? 48 89 74 24 20 57 48 83 EC 20 48 8B F9 49 8B F0 0F B7 CA 48 3B 4F 58 0F 83 "
                                    "?? ?? ?? ?? 48 89 5C 24 30 48 8D 1C 49 48 C1 E3 06 48 03 5F 50";
// The effect lookup by GUID (0x1a0200), called by create; its code names the loaded-effect table's fields.
constexpr char kFxLookupSig[] = "40 57 48 83 EC 20 4C 8B 5A 08 48 8B FA 4C 8B 12 49 8B C3 49 0B C2 4C 8B C9 0F 84 ?? ?? ?? ?? 83 B9 ?? ?? "
                                "00 00 00 48 89 5C 24 30 48 8B 99 ?? ?? 00 00";
// Bytes checked at fixed places inside create and the lookup; a displacement read from the game follows some of them.
struct Guard {
    size_t at;
    uint8_t bytes[5];
    size_t count;
};
constexpr Guard kCreateGuards[] = {
    {0xCF, {0x0F, 0x11, 0x43, 0x18}, 4},        // the transform goes to entry + 0x18
    {0x10A, {0xE8}, 1},                         // call lookup
    {0x118, {0x48, 0x8B, 0x85}, 3},             // records = [system + disp]
    {0x123, {0x80, 0x7E, 0x40, 0x00}, 4},       // settings + 0x40: use these parameters
    {0x127, {0x48, 0x8B, 0x54, 0x01, 0x10}, 5}, // the resource at record + 0x10
    {0x131, {0x0F, 0x11, 0x43, 0x74}, 4},       // parameters at entry + 0x74
    {0x15A, {0x0F, 0x10, 0x42, 0x74}, 4},       // the resource's defaults at + 0x74
};
constexpr Guard kLookupGuards[] = {
    {0x2B, {0x48, 0x8B, 0x99}, 3}, // capacity = [system + disp]
    {0x42, {0x4C, 0x8B, 0xB1}, 3}, // bitmap
    {0x6E, {0x49, 0x8B, 0xB1}, 3}, // keys
    {0xCF, {0x49, 0x8B, 0x81}, 3}, // values
    {0xE5, {0x49, 0x8B, 0xB9}, 3}, // records
};
// The frame's staging call (mov rcx, r12; call stage; cmp qword [r12+0xa38], 0): also the map mod's and Find's.
constexpr char kStageSig[] = "49 8B CC E8 ?? ?? ?? ?? 49 83 BC 24 38 0A 00 00 00 74";
// The ECS row iterator (map mod: g_next) and the one-compare "entity is the local player" function.
constexpr char kNextSig[] = "40 57 48 83 EC 20 4C 8B 01 48 8B FA 8B 41 08 4C 8B C9 49 3B 80 ?? 08 00 00";
constexpr char kIsLocalSig[] = "39 4A 04 0F 94 C0 C3";
constexpr char kSystem[] = "player_camera_transform";

// ---- settings -------------------------------------------------------------------------------------------------
// The defaults are a held Wax Torch: its effect runs with light_intensity 15 and light_radius 45; its colour is built
// into the effect, and 255,137,88 gave the same colour on lit walls in screenshots (green/red 0.40, blue/red 0.12).
struct Settings {
    bool enabled = true;
    float red = 255, green = 137, blue = 88; // 0-255
    float intensity = 1.0f;                  // 1 = a Wax Torch
    float radius = 45.0f;                    // metres
    float flicker = 1.0f;                    // 0 = steady
    float height = 1.3f;                     // metres above the feet
    bool shadows = false;
    bool share = true;      // tell the other players' Radiance what this light is
    bool showOthers = true; // draw theirs
};
constexpr float kTorchIntensity = 15.0f; // light_intensity of a held Wax Torch's effect (read in game 2026-11-19)

SRWLOCK g_settingsLock = SRWLOCK_INIT;
Settings g_settings;
std::wstring g_ini;
#ifdef RADIANCE_DEV
std::wstring g_vfxRequest, g_vfxOut;
std::atomic<bool> g_vfxDump{false};
#endif

void Log(const char* format, ...) {
    char line[512];
    int n = snprintf(line, sizeof(line), "Radiance: ");
    va_list args;
    va_start(args, format);
    vsnprintf(line + n, sizeof(line) - n, format, args);
    va_end(args);
    if (auto* host = greeneye::Host()) host->log(line);
}

bool ParseSwitch(const wchar_t* text, bool fallback) {
    while (*text == L' ' || *text == L'\t') ++text;
    if (!_wcsnicmp(text, L"true", 4) || !_wcsnicmp(text, L"on", 2) || !_wcsnicmp(text, L"yes", 3) || *text == L'1') return true;
    if (!_wcsnicmp(text, L"false", 5) || !_wcsnicmp(text, L"off", 3) || !_wcsnicmp(text, L"no", 2) || *text == L'0') return false;
    return fallback;
}

float ReadFloat(const wchar_t* key, float fallback, float low, float high) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(L"Light", key, L"", text, 64, g_ini.c_str());
    wchar_t* end = nullptr;
    const double value = wcstod(text, &end);
    if (end == text || !std::isfinite(value)) return fallback;
    return static_cast<float>(value < low ? low : value > high ? high : value);
}

// "255, 150, 70" or "#FF9646" / "FF9646".
void ReadColour(Settings& s) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(L"Light", L"Color", L"", text, 64, g_ini.c_str());
    const wchar_t* p = text;
    while (*p == L' ' || *p == L'\t') ++p;
    int r, g, b;
    if (swscanf_s(p, L"%d , %d , %d", &r, &g, &b) == 3 && wcschr(p, L',')) {
        auto clamp = [](int v) { return static_cast<float>(v < 0 ? 0 : v > 255 ? 255 : v); };
        s.red = clamp(r), s.green = clamp(g), s.blue = clamp(b);
        return;
    }
    if (*p == L'#') ++p;
    unsigned hex = 0;
    size_t digits = 0;
    while (iswxdigit(p[digits])) ++digits;
    if (digits == 6 && swscanf_s(p, L"%6x", &hex) == 1) {
        s.red = static_cast<float>(hex >> 16 & 255), s.green = static_cast<float>(hex >> 8 & 255), s.blue = static_cast<float>(hex & 255);
    }
}

void ReadSettings() {
    Settings s;
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"Light", L"Enabled", L"true", text, 32, g_ini.c_str());
    s.enabled = ParseSwitch(text, true);
    ReadColour(s);
    s.intensity = ReadFloat(L"Intensity", s.intensity, 0.0f, 100.0f);
    s.radius = ReadFloat(L"Radius", s.radius, 0.5f, 300.0f);
    s.height = ReadFloat(L"Height", s.height, -5.0f, 10.0f);
    s.flicker = ReadFloat(L"Flicker", s.flicker, 0.0f, 20.0f);
    GetPrivateProfileStringW(L"Light", L"Shadows", L"false", text, 32, g_ini.c_str());
    s.shadows = ParseSwitch(text, false);
    GetPrivateProfileStringW(L"Multiplayer", L"ShareMyLight", L"true", text, 32, g_ini.c_str());
    s.share = ParseSwitch(text, true);
    GetPrivateProfileStringW(L"Multiplayer", L"ShowOtherLights", L"true", text, 32, g_ini.c_str());
    s.showOthers = ParseSwitch(text, true);
    AcquireSRWLockExclusive(&g_settingsLock);
    g_settings = s;
    ReleaseSRWLockExclusive(&g_settingsLock);
}

// A change made in game (the menu, the on/off key): used at once, written to the ini by the watcher thread.
std::atomic<bool> g_unsaved{false}, g_keysChanged{false};
std::atomic<ULONGLONG> g_unsavedAt{0};
void SaveSettings(const Settings& s) {
    AcquireSRWLockExclusive(&g_settingsLock);
    g_settings = s;
    ReleaseSRWLockExclusive(&g_settingsLock);
    g_unsavedAt = GetTickCount64();
    g_unsaved = true;
}
void WriteSettings() {
    AcquireSRWLockShared(&g_settingsLock);
    const Settings s = g_settings;
    ReleaseSRWLockShared(&g_settingsLock);
    wchar_t text[64];
    auto put = [&](const wchar_t* key) { WritePrivateProfileStringW(L"Light", key, text, g_ini.c_str()); };
    wcscpy_s(text, s.enabled ? L"true" : L"false"), put(L"Enabled");
    swprintf_s(text, L"%d, %d, %d", int(s.red), int(s.green), int(s.blue)), put(L"Color");
    swprintf_s(text, L"%.2f", s.intensity), put(L"Intensity");
    swprintf_s(text, L"%.0f", s.radius), put(L"Radius");
    swprintf_s(text, L"%.2f", s.flicker), put(L"Flicker");
    wcscpy_s(text, s.shadows ? L"true" : L"false"), put(L"Shadows");
}

// The ini is watched on a thread of its own (a slow disk must not stall a frame): a saved change shows within a second.
HANDLE g_watchStop = nullptr, g_watchThread = nullptr;
SRWLOCK g_watchLock = SRWLOCK_INIT;
std::atomic<bool> g_off{false}; // a hot reload is coming: lights out, no new thread
DWORD WINAPI Watch(void*) {
    FILETIME last{};
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(g_ini.c_str(), GetFileExInfoStandard, &data)) last = data.ftLastWriteTime;
    while (WaitForSingleObject(g_watchStop, 250) == WAIT_TIMEOUT) {
        if (g_unsaved.load()) {
            // once the player has left the value alone for a moment; our own write is not a change to read back
            if (GetTickCount64() - g_unsavedAt.load() < 600) continue;
            g_unsaved = false;
            WriteSettings();
            if (GetFileAttributesExW(g_ini.c_str(), GetFileExInfoStandard, &data)) last = data.ftLastWriteTime;
            continue;
        }
        if (GetFileAttributesExW(g_ini.c_str(), GetFileExInfoStandard, &data) && CompareFileTime(&data.ftLastWriteTime, &last)) {
            last = data.ftLastWriteTime;
            ReadSettings();
            g_keysChanged = true;
        }
#ifdef RADIANCE_DEV
        if (DeleteFileW(g_vfxRequest.c_str())) g_vfxDump.store(true);
#endif
    }
    return 0;
}
void StartWatch() {
    if (g_watchThread || g_ini.empty()) return;
    AcquireSRWLockExclusive(&g_watchLock);
    if (!g_watchThread && !g_off.load()) {
        g_watchStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (g_watchStop) g_watchThread = CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr);
    }
    ReleaseSRWLockExclusive(&g_watchLock);
}
void StopWatch() {
    AcquireSRWLockExclusive(&g_watchLock);
    if (g_watchThread) {
        SetEvent(g_watchStop);
        WaitForSingleObject(g_watchThread, 3000);
        CloseHandle(g_watchThread);
        CloseHandle(g_watchStop);
        g_watchThread = g_watchStop = nullptr;
    }
    ReleaseSRWLockExclusive(&g_watchLock);
}

// ---- the local player's position ------------------------------------------------------------------------------
NextFn g_next = nullptr;
IsLocalFn g_isLocal = nullptr;
unsigned g_transformSlot = 0; // RenderTransform's place in the system's row
thread_local void** t_row = nullptr;

SRWLOCK g_playerLock = SRWLOCK_INIT;
WorldTransform g_player{};
ULONGLONG g_playerAt = 0;

bool SafeCopy(const void* from, void* to, size_t size) {
    __try {
        std::memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Every row the camera system walks this frame (the players in view): the local one is where our light goes, the
// others are where other players' lights go (sync.h).
constexpr int kMaxSeen = 48;
struct Seen {
    uint32_t entity;
    bool local;
    WorldTransform transform;
    ULONGLONG at;
};
Seen g_seen[kMaxSeen]{}; // under g_playerLock
std::atomic<uint32_t> g_localEntity{0};

void CaptureRow(void** row, uint32_t entity, bool local) {
    WorldTransform now;
    void* component = nullptr; // keen::ecs::RenderTransform: the transform the model is drawn with comes first
    if (!SafeCopy(row + g_transformSlot, &component, sizeof(component)) || !component) return;
    if (!SafeCopy(component, &now, sizeof(now))) return;
    // the world is 10240 m across; anything else is not a position
    for (int i = 0; i < 3; ++i) {
        const double metres = static_cast<double>(now.position[i]) / 4294967296.0;
        if (!(metres > -4096.0 && metres < 16384.0)) return;
    }
    const ULONGLONG tick = GetTickCount64();
    AcquireSRWLockExclusive(&g_playerLock);
    if (local) {
        g_player = now;
        g_playerAt = tick;
    }
    Seen* slot = nullptr;
    Seen* oldest = &g_seen[0];
    for (Seen& s : g_seen) {
        if (s.entity == entity) slot = &s;
        if (s.at < oldest->at) oldest = &s;
    }
    if (!slot) slot = oldest;
    slot->entity = entity, slot->local = local, slot->transform = now, slot->at = tick;
    ReleaseSRWLockExclusive(&g_playerLock);
    if (local) g_localEntity.store(entity, std::memory_order_relaxed);
}

bool NextHook(void* system, void** row, size_t size) {
    greeneye::Lease lease;
    const bool more = g_next(system, row, size);
    if (lease) t_row = more ? row : nullptr;
    return more;
}

bool IsLocalHook(uint32_t entity, void* localPlayerData) {
    greeneye::Lease lease;
    const bool local = g_isLocal(entity, localPlayerData);
    if (lease && t_row && entity) CaptureRow(t_row, entity, local);
    return local;
}

// ---- the light --------------------------------------------------------------------------------------------------
// The game's lights are all made by its effect system (a torch's flame effect appends its own point light on the
// GPU). So ours is an effect too: one of the game's small light-only effects (one emitter, no flame), started in the
// renderer's effect system with the game's own call, moved each frame, its light_* parameters set from the ini.
using FxCreateFn = uint32_t* (*)(uint32_t* handle, void* manager, const void* guid, const WorldTransform* transform, const void* settings);
using FxDestroyFn = void (*)(void* manager, uint32_t handle);
using FxMoveFn = void (*)(void* manager, uint32_t handle, const WorldTransform* transform);
using FxParametersFn = void (*)(void* manager, uint32_t handle, const void* parameters);
FxCreateFn g_fxCreate = nullptr;
FxDestroyFn g_fxDestroy = nullptr;
FxMoveFn g_fxMove = nullptr;
FxParametersFn g_fxParameters = nullptr;
StageFn g_stage = nullptr;

// Where things are, read out of the game's own code at start (GREENEYE_MOD_START).
struct Layout {
    uint32_t manager = 0;                                    // renderer -> effect manager
    uint32_t capacity = 0, bitmap = 0, keys = 0, values = 0; // effect system: the loaded-effect table
    uint32_t records = 0;                                    // effect system: 0xC0 records, the resource at +0x10
} g_layout;
// Fixed by the signatures (the bytes that use them are part of what is matched or checked at start):
constexpr size_t kManagerSystem = 8, kManagerFree = 0x28, kManagerEntries = 0x50, kManagerEntryCount = 0x58;
constexpr size_t kEntrySize = 0xC0, kEntryHandle = 8, kRecordSize = 0xC0, kRecordResource = 0x10;
// keen::VfxResource (reflected): particles +20, parameters +108, defaultParameterData +116 (64 bytes).
constexpr size_t kResourceParticles = 20, kResourceParameters = 108, kResourceDefaults = 116, kParameterBytes = 64;

constexpr uint32_t Fnv(const char* text) {
    uint32_t hash = 0x811c9dc5u;
    for (; *text; ++text) hash = (hash ^ static_cast<uint8_t>(*text)) * 0x01000193u;
    return hash;
}
constexpr uint32_t kFloat = 0x90b5085du, kBool = 0xa2746cb5u, kColour = 0x53039620u; // parameter type ids
constexpr uint32_t kLightIntensity = Fnv("light_intensity"), kLightRadius = Fnv("light_radius"), kLightColour = Fnv("light_color"),
                   kLightEnabled = Fnv("light_enabled"), kLightShadows = Fnv("light_shadows_enabled"),
                   kLightFlicker = Fnv("light_flicker_speed"), kLifetime = Fnv("lifetime"), kDuration = Fnv("duration");
static_assert(kLightIntensity == 0x0fd9ca11u && kLightShadows == 0x2e2a1debu);
// The light-only effect the defaults were chosen on (warm, flickering, 13 m). If a patch drops it, any loaded effect
// of the same shape is used instead.
constexpr uint8_t kPreferred[16] = {0xd6, 0xa0, 0x6e, 0x82, 0x81, 0x21, 0xea, 0x4f, 0x8a, 0xba, 0x34, 0x91, 0xcd, 0x40, 0xc3, 0x9d};

struct Effect {
    bool found = false;
    uint8_t guid[16]{};
    uint8_t defaults[kParameterBytes]{};
    int intensity = -1, radius = -1, colour = -1, enabled = -1, shadows = -1, flicker = -1; // byte offsets, -1: none
};
std::atomic<bool> g_broken{false};

template <class T> T At(const void* base, size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof(T));
    return value;
}
template <class T> bool Peek(const void* base, size_t offset, T& out) {
    return SafeCopy(static_cast<const uint8_t*>(base) + offset, &out, sizeof(T));
}

// A light-only effect: one emitter, colour + intensity + radius parameters, nothing that ends it.
bool Describe(const uint8_t* resource, Effect& out) {
    int32_t rel = 0;
    uint32_t count = 0, particles = 0;
    if (!Peek(resource, kResourceParticles + 4, particles) || particles != 1) return false;
    if (!Peek(resource, kResourceParameters, rel) || !Peek(resource, kResourceParameters + 4, count) || !rel || count > 32) return false;
    const uint8_t* fields = resource + kResourceParameters + rel;
    out = {};
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t name = 0, type = 0;
        uint8_t range[2]{};
        if (!Peek(fields, i * 12, name) || !Peek(fields, i * 12 + 4, type) || !Peek(fields, i * 12 + 8, range)) return false;
        if (name == kLifetime || name == kDuration) return false;
        if (range[1] != 4 || range[0] > kParameterBytes - 4) continue;
        if (name == kLightIntensity && type == kFloat) out.intensity = range[0];
        if (name == kLightRadius && type == kFloat) out.radius = range[0];
        if (name == kLightColour && type == kColour) out.colour = range[0];
        if (name == kLightEnabled && type == kBool) out.enabled = range[0];
        if (name == kLightShadows && type == kBool) out.shadows = range[0];
        if (name == kLightFlicker && type == kFloat) out.flicker = range[0];
    }
    if (out.intensity < 0 || out.radius < 0 || out.colour < 0) return false;
    return Peek(resource, kResourceDefaults, out.defaults);
}

// Looks through the effects the game has loaded (its own table; starting one that is not there stops the game).
bool FindEffect(const uint8_t* system, Effect& out) {
    uint64_t capacity = 0;
    const uint64_t* bitmap = nullptr;
    const uint8_t *keys = nullptr, *records = nullptr;
    const uint32_t* values = nullptr;
    if (!Peek(system, g_layout.capacity, capacity) || !Peek(system, g_layout.bitmap, bitmap) || !Peek(system, g_layout.keys, keys) ||
        !Peek(system, g_layout.values, values) || !Peek(system, g_layout.records, records))
        return false;
    if (!capacity || capacity > 65536 || !bitmap || !keys || !values || !records) return false;
    Effect other;
    bool haveOther = false;
    for (uint64_t slot = 0; slot < capacity; ++slot) {
        uint64_t word = 0;
        uint32_t index = 0;
        uint8_t guid[16];
        const uint8_t* resource = nullptr;
        if (!Peek(bitmap, slot / 64 * 8, word)) return false;
        if (!(word >> (slot % 64) & 1)) continue;
        if (!Peek(values, slot * 4, index) || index >= 65536 || !Peek(keys, slot * 16, guid)) return false;
        if (!Peek(records, index * kRecordSize + kRecordResource, resource) || !resource) continue;
        const bool preferred = std::memcmp(guid, kPreferred, 16) == 0;
        if (!preferred && haveOther) continue;
        Effect e;
        if (!Describe(resource, e)) continue;
        std::memcpy(e.guid, guid, 16);
        e.found = true;
        if (preferred) {
            out = e;
            return true;
        }
        // a stand-in must be able to do everything the ini offers
        if (e.enabled >= 0 && e.shadows >= 0 && e.flicker >= 0) other = e, haveOther = true;
    }
    if (haveOther) out = other;
    return haveOther;
}

void SyncTick();
#include "menu.h"
#include "sync.h"
void SyncTick() { sync::Tick(menu::UiClient()); }

// The lights of one renderer that stages frames (the world's; a second view of the world gets its own set): this
// player's and one for each other player heard of. Kept across hot reloads, so a new generation finds what the old one
// left.
struct Light {
    uint32_t handle;
    uint32_t owner; // a remote light: its sender's handle (0: free, or this player's own)
    uint8_t parameters[kParameterBytes];
    WorldTransform transform;
};
struct Slot {
    void* renderer;
    void* manager;
    ULONGLONG seen;
    Light own;
    Light others[sync::kMaxRemotes];
};
struct Kept {
    Slot slots[4];
};
Kept* g_kept = nullptr;
Effect g_effect; // chosen once per effect system
void* g_effectOf = nullptr;

// Our running effect's entry while the handle is still ours (the entry carries the handle, generation included).
bool Alive(const Slot& s, const Light& light) {
    if (!light.handle || !s.manager) return false;
    void* manager = nullptr;
    if (!Peek(s.renderer, g_layout.manager, manager) || manager != s.manager) return false;
    uint64_t count = 0;
    const uint8_t* entries = nullptr;
    uint32_t handle = 0;
    const uint64_t index = light.handle & 0xffff;
    if (!Peek(manager, kManagerEntryCount, count) || !Peek(manager, kManagerEntries, entries) || !entries || index >= count) return false;
    return Peek(entries, index * kEntrySize + kEntryHandle, handle) && handle == light.handle;
}

void Remove(Slot& s, Light& light) {
    if (Alive(s, light)) g_fxDestroy(s.manager, light.handle);
    light.handle = 0;
}
void RemoveAll(Slot& s) {
    Remove(s, s.own);
    for (Light& light : s.others) Remove(s, light), light.owner = 0;
}

struct Look {
    float red, green, blue, intensity, radius, flicker;
    bool shadows;
};

// Starts the light, or moves and restyles the one running.
void Place(Slot& slot, Light& light, uint8_t* manager, const Look& look, const WorldTransform& at, float height) {
    alignas(16) uint8_t parameters[kParameterBytes + 8]{}; // + "use these, not the defaults" and a flag byte
    std::memcpy(parameters, g_effect.defaults, kParameterBytes);
    auto put = [&](int offset, const void* value) {
        if (offset >= 0) std::memcpy(parameters + offset, value, 4);
    };
    const float intensity = look.intensity * kTorchIntensity;
    const uint32_t yes = 1, shadows = look.shadows ? 1u : 0u;
    const uint8_t colour[4] = {static_cast<uint8_t>(look.red), static_cast<uint8_t>(look.green), static_cast<uint8_t>(look.blue), 255};
    put(g_effect.intensity, &intensity), put(g_effect.radius, &look.radius), put(g_effect.colour, colour), put(g_effect.enabled, &yes);
    put(g_effect.shadows, &shadows), put(g_effect.flicker, &look.flicker);
    parameters[kParameterBytes] = 1;

    WorldTransform transform{};
    for (int i = 0; i < 3; ++i) transform.position[i] = at.position[i];
    transform.position[1] += static_cast<int64_t>(static_cast<double>(height) * 4294967296.0);
    transform.orientation[3] = 1.0f;
    transform.scale[0] = transform.scale[1] = transform.scale[2] = 1.0f;

    if (!Alive(slot, light)) {
        light.handle = 0;
        uint64_t free = 0;
        if (!Peek(manager, kManagerFree, free) || !free) return; // the effect system is full: try again next frame
        g_fxCreate(&light.handle, manager, g_effect.guid, &transform, parameters);
    } else {
        if (std::memcmp(&light.transform, &transform, sizeof(transform)) != 0) g_fxMove(slot.manager, light.handle, &transform);
        if (std::memcmp(light.parameters, parameters, kParameterBytes) != 0) g_fxParameters(slot.manager, light.handle, parameters);
    }
    light.transform = transform;
    std::memcpy(light.parameters, parameters, kParameterBytes);
}

#ifdef RADIANCE_DEV
#include "vfxdump.h"
#endif

void Update(void* renderer) {
    if (!renderer || !g_kept) return;
    const ULONGLONG now = GetTickCount64();
    Slot* slot = nullptr;
    Slot* oldest = &g_kept->slots[0];
    for (Slot& s : g_kept->slots) {
        if (s.renderer == renderer) slot = &s;
        if (s.seen < oldest->seen) oldest = &s;
    }
    if (!slot) {
        slot = oldest; // its renderer is long gone (or never was): nothing to stop there
        *slot = {};
        slot->renderer = renderer;
    }
    slot->seen = now;

    AcquireSRWLockShared(&g_settingsLock);
    const Settings settings = g_settings;
    ReleaseSRWLockShared(&g_settingsLock);
    static Seen seen[kMaxSeen]; // (only this function's thread uses it)
    AcquireSRWLockShared(&g_playerLock);
    const WorldTransform at = g_player;
    const ULONGLONG playerAt = g_playerAt;
    std::memcpy(seen, g_seen, sizeof(seen));
    ReleaseSRWLockShared(&g_playerLock);
    const bool inWorld = playerAt && now - playerAt < 500; // no fresh position: menu or loading, so no light
#ifdef RADIANCE_DEV
    if (inWorld && g_vfxDump.exchange(false)) vfxdump::Run(renderer, at, g_vfxOut.c_str());
#endif
    uint8_t *manager = nullptr, *system = nullptr;
    const bool running = !g_off.load(std::memory_order_relaxed) && inWorld && Peek(renderer, g_layout.manager, manager) && manager &&
                         Peek(manager, kManagerSystem, system) && system;
    if (!running) return RemoveAll(*slot);
    if (slot->manager != manager) { // a new effect manager: the handles kept were the old one's
        *slot = {};
        slot->renderer = renderer, slot->manager = manager, slot->seen = now;
    }

    if (g_effectOf != system) { // a new world's effect system: look the effect up again
        g_effectOf = system;
        g_effect = {};
        if (FindEffect(system, g_effect)) {
            Log("light effect %02x%02x%02x%02x... (intensity @%d, radius @%d, colour @%d, flicker @%d, shadows @%d)", g_effect.guid[0],
                g_effect.guid[1], g_effect.guid[2], g_effect.guid[3], g_effect.intensity, g_effect.radius, g_effect.colour,
                g_effect.flicker, g_effect.shadows);
        } else {
            Log("no light effect of the kind this mod uses is loaded in this game build; no light");
        }
    }
    if (!g_effect.found) return RemoveAll(*slot);

    // this player's own
    if (settings.enabled && settings.intensity > 0.0f)
        Place(*slot, slot->own, manager, Look{settings.red, settings.green, settings.blue, settings.intensity, settings.radius, settings.flicker, settings.shadows}, at,
              settings.height);
    else
        Remove(*slot, slot->own);

    // the other players': each on its player's row while that row is in view
    static sync::Remote remotes[sync::kMaxRemotes];
    AcquireSRWLockShared(&sync::lock);
    std::memcpy(remotes, sync::remotes, sizeof(remotes));
    ReleaseSRWLockShared(&sync::lock);
    for (Light& light : slot->others) { // lights whose player stopped, left view or turned it off
        bool keep = false;
        if (light.owner && settings.showOthers)
            for (const sync::Remote& remote : remotes) keep |= remote.sender == light.owner && remote.on && remote.bound;
        if (!keep) Remove(*slot, light), light.owner = 0;
    }
    if (!settings.showOthers) return;
    for (const sync::Remote& remote : remotes) {
        if (!remote.sender || !remote.on || !remote.bound) continue;
        const Seen* row = nullptr;
        for (const Seen& s : seen)
            if (s.entity == remote.bound && now - s.at < 500) row = &s;
        Light* light = nullptr;
        Light* spare = nullptr;
        for (Light& l : slot->others) {
            if (l.owner == remote.sender) light = &l;
            if (!l.owner && !spare) spare = &l;
        }
        if (!row) { // out of view for now
            if (light) Remove(*slot, *light), light->owner = 0;
            continue;
        }
        if (!light) light = spare;
        if (!light) continue;
        light->owner = remote.sender;
        Place(*slot, *light, manager, Look{float(remote.r), float(remote.g), float(remote.b), remote.intensity, remote.radius, remote.flicker, false},
              row->transform, settings.height);
    }
}

bool SafeUpdate(void* renderer) {
    __try {
        Update(renderer);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void StageHook(void* renderer) {
    {
        greeneye::Lease lease;
        if (lease && !g_broken.load(std::memory_order_relaxed)) {
            StartWatch();
            if (!SafeUpdate(renderer)) {
                g_broken.store(true);
                Log("stopped after a fault while placing the light");
            }
        }
    }
    g_stage(renderer);
}

// ---- finding the game's code ----------------------------------------------------------------------------------
bool Section(const char* name, uint8_t*& start, size_t& size) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (std::memcmp(sec->Name, name, strlen(name)) != 0) continue;
        start = base + sec->VirtualAddress;
        size = sec->Misc.VirtualSize;
        return true;
    }
    return false;
}

// The start of the function that contains code (any of its chunks), from the exe's unwind table.
uint8_t* FunctionStart(const uint8_t* code) {
    DWORD64 image = 0;
    const RUNTIME_FUNCTION* fn = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(code), &image, nullptr);
    for (int guard = 0; fn && guard < 16; ++guard) {
        auto* unwind = reinterpret_cast<const uint8_t*>(image + fn->UnwindInfoAddress);
        if (!(unwind[0] >> 3 & 0x4)) return reinterpret_cast<uint8_t*>(image + fn->BeginAddress); // no UNW_FLAG_CHAININFO
        fn = reinterpret_cast<const RUNTIME_FUNCTION*>(unwind + 4 + ((unwind[2] + 1) & ~1u) * 2);
    }
    return nullptr;
}

// The ECS system's descriptor in .rdata ({name, length}, function at +0x10, component name lists from +0x38):
// its function, and RenderTransform's place among the components it reads (the row holds the system first).
bool FindSystem(uint8_t*& function, unsigned& transformSlot) {
    uint8_t *rdata, *text;
    size_t rdataSize, textSize;
    if (!Section(".rdata", rdata, rdataSize) || !Section(".text", text, textSize)) return false;
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const size_t image = nt->OptionalHeader.SizeOfImage;
    auto inImage = [&](const void* p, size_t n) {
        const auto at = reinterpret_cast<uintptr_t>(p), from = reinterpret_cast<uintptr_t>(base);
        return at >= from && at - from <= image - n;
    };
    auto named = [&](const Field& f, const char* name) {
        return f.length == strlen(name) && inImage(f.text, f.length) && std::memcmp(f.text, name, f.length) == 0;
    };
    int found = 0;
    for (size_t off = 0; off + 0x48 <= rdataSize; off += 8) {
        const uint8_t* p = rdata + off;
        if (!named(At<Field>(p, 0), kSystem)) continue;
        auto* fn = At<uint8_t*>(p, 0x10);
        if (fn < text || fn >= text + textSize) continue;
        const Span reads = At<Span>(p, 0x38);
        if (reads.count == 0 || reads.count > 32 || !inImage(reads.data, reads.count * sizeof(Field))) continue;
        for (unsigned i = 0; i < reads.count; ++i) {
            if (!named(At<Field>(reads.data, i * sizeof(Field)), "RenderTransform")) continue;
            if (found++ && (function != fn || transformSlot != 1 + i)) return false; // two different definitions
            function = fn;
            transformSlot = 1 + i;
        }
    }
    return found > 0;
}

// Every "call rel32" to target from inside the function at owner (as the code was before any hook).
std::vector<uint8_t*> CallsFrom(const std::vector<uint8_t>& code, uint8_t* text, const uint8_t* owner, const uint8_t* target) {
    std::vector<uint8_t*> calls;
    for (size_t i = 0; i + 5 <= code.size(); ++i) {
        if (code[i] != 0xE8) continue;
        int32_t d;
        std::memcpy(&d, &code[i + 1], 4);
        if (text + i + 5 + d == target && FunctionStart(text + i) == owner) calls.push_back(text + i);
    }
    return calls;
}

// Every "call rel32" to target in the game's code, with the bytes in front of it.
uint32_t ManagerOffset(const std::vector<uint8_t>& code, uint8_t* text, const uint8_t* destroy) {
    // the renderer's own "stop this effect": mov rcx, [reg + disp32]; mov edx, reg; call destroy
    uint32_t found = 0;
    for (size_t i = 9; i + 5 <= code.size(); ++i) {
        if (code[i] != 0xE8) continue;
        int32_t d;
        std::memcpy(&d, &code[i + 1], 4);
        if (text + i + 5 + d != destroy) continue;
        const uint8_t* p = &code[i - 9];
        if ((p[0] & 0xFB) != 0x48 || p[1] != 0x8B || (p[2] & 0xF8) != 0x88 || p[7] != 0x8B) continue;
        uint32_t disp;
        std::memcpy(&disp, p + 3, 4);
        if (found && found != disp) return 0;
        found = disp;
    }
    return found;
}

bool Guards(const GreeneyeHost* host, const uint8_t* at, const Guard* guards, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        uint8_t bytes[5];
        host->read(at + guards[i].at, bytes, guards[i].count);
        if (std::memcmp(bytes, guards[i].bytes, guards[i].count) != 0) return false;
    }
    return true;
}
uint32_t Displacement(const GreeneyeHost* host, const uint8_t* at) {
    uint32_t value = 0;
    host->read(at + 3, &value, 4);
    return value;
}

bool Prepare() {
    g_off.store(true); // the next frame stops the light; the new generation starts its own
    menu::Shutdown();
    if (g_unsaved.exchange(false)) WriteSettings();
    StopWatch();
    if (!g_kept) return true;
    for (const Slot& s : g_kept->slots) {
        bool running = s.own.handle != 0;
        for (const Light& light : s.others) running |= light.handle != 0;
        if (running && GetTickCount64() - s.seen < 1000) return false; // not stopped yet: ask again next frame
    }
    return true;
}
bool Ready() { return true; }
void Resume() { g_off.store(false); }

} // namespace

GREENEYE_MOD_START {
    if (!greeneye::HostHasHooks(host) || !host->find || !host->read || !host->keep) return false;
    greeneye::UseHost(host);
    if (host->side != kGreeneyeClient) return false; // the server has nothing to light
    if (!greeneye::ClaimProcess(L"Radiance", host)) return false;

    uint8_t* create = host->find(kFxCreateSig, nullptr);
    uint8_t* destroy = host->find(kFxDestroySig, nullptr);
    uint8_t* move = host->find(kFxMoveSig, nullptr);
    uint8_t* parameters = host->find(kFxParametersSig, nullptr);
    uint8_t* lookup = host->find(kFxLookupSig, nullptr);
    uint8_t* stage = host->find(kStageSig, nullptr);
    uint8_t* next = host->find(kNextSig, nullptr);
    uint8_t* isLocal = host->find(kIsLocalSig, nullptr);
    uint8_t* system = nullptr;
    const bool haveSystem = FindSystem(system, g_transformSlot);
    if (!create || !destroy || !move || !parameters || !lookup || !stage || !next || !isLocal || !haveSystem) {
        Log("this game build is not supported (effect create %p, destroy %p, move %p, parameters %p, lookup %p, stage %p, next %p, "
            "local %p, system %p); no light",
            create, destroy, move, parameters, lookup, stage, next, isLocal, system);
        return false;
    }
    int32_t toLookup = 0;
    host->read(create + 0x10B, &toLookup, 4);
    if (!Guards(host, create, kCreateGuards, std::size(kCreateGuards)) || !Guards(host, lookup, kLookupGuards, std::size(kLookupGuards)) ||
        create + 0x10F + toLookup != lookup) {
        Log("the effect system is laid out differently in this game build; no light");
        return false;
    }
    g_layout.capacity = Displacement(host, lookup + 0x2B);
    g_layout.bitmap = Displacement(host, lookup + 0x42);
    g_layout.keys = Displacement(host, lookup + 0x6E);
    g_layout.values = Displacement(host, lookup + 0xCF);
    g_layout.records = Displacement(host, lookup + 0xE5);

    uint8_t* text;
    size_t textSize;
    if (!Section(".text", text, textSize)) return false;
    std::vector<uint8_t> code(textSize);
    host->read(text, code.data(), textSize);
    g_layout.manager = ManagerOffset(code, text, destroy);
    const auto nextCalls = CallsFrom(code, text, system, next);
    const auto localCalls = CallsFrom(code, text, system, isLocal);
    const uint32_t fields[] = {g_layout.manager, g_layout.capacity, g_layout.bitmap, g_layout.keys, g_layout.values, g_layout.records};
    bool sane = Displacement(host, create + 0x118) == g_layout.records && g_transformSlot <= 8;
    for (uint32_t f : fields) sane = sane && f && f < 0x10000;
    if (!sane) {
        Log("the effect system reads oddly in this game build (manager +0x%x, table +0x%x +0x%x +0x%x +0x%x, records +0x%x); no light",
            g_layout.manager, g_layout.capacity, g_layout.bitmap, g_layout.keys, g_layout.values, g_layout.records);
        return false;
    }
    if (nextCalls.empty() || nextCalls.size() > 8 || localCalls.size() != 1) {
        Log("the camera system reads differently in this game build (%zu row calls, %zu player checks); no light", nextCalls.size(),
            localCalls.size());
        return false;
    }

    g_kept = static_cast<Kept*>(host->keep("radiance.effects.2", sizeof(Kept)));
    if (!g_kept) return false;
    g_fxCreate = reinterpret_cast<FxCreateFn>(create);
    g_fxDestroy = reinterpret_cast<FxDestroyFn>(destroy);
    g_fxMove = reinterpret_cast<FxMoveFn>(move);
    g_fxParameters = reinterpret_cast<FxParametersFn>(parameters);

    if (host->modDir) {
        g_ini = std::wstring(host->modDir) + L"radiance.ini";
#ifdef RADIANCE_DEV
        g_vfxRequest = std::wstring(host->modDir) + L"vfxdump";
        g_vfxOut = std::wstring(host->modDir) + L"vfxdump.txt";
#endif
        ReadSettings();
    }

    for (uint8_t* call : nextCalls) {
        auto previous = reinterpret_cast<NextFn>(host->redirectCall(call, reinterpret_cast<void*>(&NextHook)));
        if (!previous) {
            Log("could not hook the camera system; no light");
            return false;
        }
        g_next = previous;
    }
    g_isLocal = reinterpret_cast<IsLocalFn>(host->redirectCall(localCalls[0], reinterpret_cast<void*>(&IsLocalHook)));
    g_stage = reinterpret_cast<StageFn>(host->redirectCall(stage + 3, reinterpret_cast<void*>(&StageHook)));
    if (!g_isLocal || !g_stage) {
        Log("could not hook the frame; no light");
        return false;
    }
    // optional: without the menu the ini alone sets the light; other players' lights need its frame hook
    if (menu::Init(host, code, text)) sync::Init(host);
    Log("%s started (effect manager at renderer +0x%x, effect table at +0x%x, player row slot %u)", kVersion, g_layout.manager,
        g_layout.keys, g_transformSlot);
    return true;
}
GREENEYE_MOD_HOT {
    static const GreeneyeHot api{sizeof(GreeneyeHot), Prepare, Ready, Resume};
    return &api;
}
