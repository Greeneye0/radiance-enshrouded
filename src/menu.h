// The Radiance menu and hotkeys. Included inside main.cpp's namespace, after the settings and Peek.
//
// A small panel at the left of the screen, drawn with the game's own UI calls (widgets, labels, its text style and
// its words where it has them) inside the in-game UI pass, as Embervale Teleport's notices are. It is used with the
// mouse: click Light, drag Brightness and Range, click a colour, click Close (or press Esc). A controller moves
// through it with the D-pad. Every change is applied at once and written to radiance.ini.
//
// The mouse and Esc: while the panel is open, a window procedure in front of the game's takes the raw mouse (so the
// camera gets none of it) and moves the panel's own cursor with it; the mouse buttons and Esc are kept from the game
// (a click is no attack, Esc closes the panel and does not open the game's menu). The game stays on its play screen
// and the keyboard still moves the character. (Auto Loot's way, an empty HUD screen, turns the camera to face the
// character and lets the mouse orbit it in the 2026-11-04 build.)
//
// Found by signature (the same ones Auto Loot, the map mod and Embervale Teleport use); the UI bundle's fields (text
// style, the game's words) and the play screen's number by name through the game's reflection tables (reflect.h). If any
// of it is missing, the menu stays off and the on/off key still works.
namespace menu {

namespace inp = greeneye::input;
namespace text = radiance::text;

struct Text {
    const char* bytes;
    size_t length;
    uint64_t hash;
};
inline Text MakeText(const char* s) {
    Text t{s, strlen(s), 0xcbf29ce484222325ull};
    for (size_t i = 0; i < t.length; ++i) t.hash = (t.hash ^ uint8_t(s[i])) * 0x100000001b3ull;
    return t;
}

using BeginFn = void (*)(void*, uint64_t);
using EndFn = void (*)(void*);
using SizeFn = void (*)(void*, uint64_t);
using LayerFn = void* (*)(void*, const Text*, uint8_t, uint32_t, int32_t, uint32_t);
using NamedFn = void* (*)(void*, const Text*);
using OpaqueFn = void* (*)(void*);
using LabelFn = void (*)(uint8_t*, const Text*, const uint8_t*, uint8_t);
using DrawFn = void (*)(void*, const float*, const uint32_t*, const uint64_t*);
using LocaFn = Text* (*)(Text*, uint32_t);
using PlayFn = void (*)(void*);

BeginFn begin{};
EndFn end{}, endLayer{}, endLabel{};
SizeFn setSize{};
LayerFn beginLayer{};
NamedFn named{};
OpaqueFn opaque{};
LabelFn beginLabel{};
DrawFn drawImage{};
LocaFn loca{};
PlayFn nextPlay{};
void** root = nullptr; // the UI root (G); its +200 is the UI bundle (keen::FbUiBundle)
constexpr size_t kBundle = 200;
bool ready = false;                       // the panel can be drawn and can have the mouse
std::atomic<bool> faulted{false};         // a fault while drawing: the panel stays closed
int64_t styleAt = -1, legendaryAt = -1;   // fields of the bundle, by name
int64_t gameTextAt[text::IdCount];        // the game's own word for an id (a loca key in the bundle), or -1
int screenPlaying = -1; // keen::HudScreenType InGame, by name

// The UI root's client (as the mods' ui::Client): *(**(G + 0xd0) + 0x38), null outside a world.
uint8_t* UiClient() {
    uint8_t* client = nullptr;
    if (root && *root) {
        uint8_t** ui = nullptr;
        uint8_t* inner = nullptr;
        if (Peek(*root, 0xd0, ui) && ui && Peek(ui, 0, inner) && inner) Peek(inner, 0x38, client);
    }
    return client;
}
// The HUD screen shown (keen::HudScreenType), -1 when unreadable. The byte's place in the UI client is the one Auto
// Loot and Quick Deposit read. It only decides when the panel may open and when it closes by itself; a value that
// makes no sense (a build that moved the byte) is taken as "playing".
size_t kUiScreen = 0x1cd8; // read from the game's code in Init (the pair Find reads)
int Screen() {
    uint8_t* client = UiClient();
    uint8_t screen = 0;
    return client && Peek(client, kUiScreen, screen) ? screen : -1;
}
bool Playing() {
    const int shown = Screen();
    return shown == screenPlaying || shown <= 0 || shown > 64 || screenPlaying <= 0;
}

// ---- keys ------------------------------------------------------------------------------------------------------
struct Keys {
    inp::Binding toggle, open, up, down, left, right, close;
} keys;
inp::SharedBinding openPad, togglePad;
std::atomic<ULONGLONG> padPanelUntil{0}; // the panel has the pad: the game sees it idle

void LoadKeys() {
    if (g_ini.empty()) return;
    char ini[MAX_PATH * 3]{};
    WideCharToMultiByte(CP_ACP, 0, g_ini.c_str(), -1, ini, sizeof(ini), nullptr, nullptr);
    static void (*const log)(const char*) = [](const char* line) { Log("%s", line); };
    keys.toggle = inp::ReadIniBinding(ini, "Keys", "ToggleLight", "F8, NoPad", log);
    keys.open = inp::ReadIniBinding(ini, "Keys", "OpenMenu", "Shift+F8, Pad:LB+Left", log);
    keys.up = inp::ReadIniBinding(ini, "Keys", "MenuUp", "NoKeys, Pad:Up", log);
    keys.down = inp::ReadIniBinding(ini, "Keys", "MenuDown", "NoKeys, Pad:Down", log);
    keys.left = inp::ReadIniBinding(ini, "Keys", "MenuLeft", "NoKeys, Pad:Left", log);
    keys.right = inp::ReadIniBinding(ini, "Keys", "MenuRight", "NoKeys, Pad:Right", log);
    keys.close = inp::ReadIniBinding(ini, "Keys", "MenuClose", "NoKeys, Pad:B", log); // (Esc always closes too)
    openPad.Set(keys.open);
    togglePad.Set(keys.toggle);
}

// What the game reads from a pad: nothing while the panel has it; else the chords' own buttons are kept from it.
void PadFilter(XINPUT_GAMEPAD& pad, DWORD user) {
    static inp::PadHide hide;
    if (GetTickCount64() < padPanelUntil.load()) {
        pad.wButtons = hide.Apply(pad.wButtons, user, 0xFFFF);
        pad.bLeftTrigger = pad.bRightTrigger = 0;
        pad.sThumbLX = pad.sThumbLY = pad.sThumbRX = pad.sThumbRY = 0;
        return;
    }
    const inp::PadState state{pad.wButtons, pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD,
                              pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD};
    pad.wButtons = hide.Apply(pad.wButtons, user, uint16_t(inp::PadHideMask(openPad.Get(), state) | inp::PadHideMask(togglePad.Get(), state)));
}

// A press, repeating while held (for moving through the rows and changing a value).
struct Repeat {
    bool was = false;
    ULONGLONG at = 0;
    bool Update(bool down, ULONGLONG now) {
        bool fire = false;
        if (down && !was) fire = true, at = now + 380;
        else if (down && now >= at) fire = true, at = now + 90;
        was = down;
        return fire;
    }
};

// ---- the mouse and Esc -----------------------------------------------------------------------------------------
// While the panel is open a window procedure in front of the game's takes the mouse: raw mouse input (what turns the
// camera) never reaches the game and moves the panel's own cursor instead, and the mouse buttons and the wheel are
// dropped, so a click on the panel is no attack. Esc is taken too and closes the panel (it reaches the game as raw
// input as well as key messages; a press taken here is held to its release, so the game never sees half of one). The
// rest of the keyboard goes on to the game: the player can keep walking. Nothing of the game's is changed, so there
// is nothing to put back but the window procedure.
HWND filteredWindow = nullptr;
WNDPROC previousWindowProc = nullptr;
std::atomic<bool> capture{false};       // the panel is open: the mouse and Esc are ours
std::atomic<bool> escapeHeld{false};    // an Esc press we took is still down
std::atomic<bool> escapePressed{false}; // Esc went down while the panel was open
std::atomic<ULONGLONG> escapeHeldSince{0};
std::atomic<long> mouseDx{0}, mouseDy{0}; // raw movement since the last frame
std::atomic<bool> mouseDown{false};       // the left button, from raw input

// When the in-game UI pass last ran. If it stops (the game left play: a disconnect, the main menu) while the panel is
// open, nothing would close the panel, so the window procedure lets everything through again after a second.
std::atomic<ULONGLONG> lastFrame{0};

LRESULT CALLBACK PanelWindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    const bool panel = capture.load(std::memory_order_relaxed) && GetTickCount64() - lastFrame.load(std::memory_order_relaxed) < 1000;
    if (panel || escapeHeld.load(std::memory_order_relaxed)) {
        if (message == WM_INPUT) {
            RAWINPUT input{};
            UINT bytes = sizeof(input);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER)) != UINT(-1)) {
                if (panel && input.header.dwType == RIM_TYPEMOUSE) {
                    const RAWMOUSE& mouse = input.data.mouse;
                    if (!(mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) mouseDx += mouse.lLastX, mouseDy += mouse.lLastY;
                    if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) mouseDown = true;
                    if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) mouseDown = false;
                    return DefWindowProcW(hwnd, message, wp, lp); // (raw input must still be cleaned up)
                }
                if (input.header.dwType == RIM_TYPEKEYBOARD && input.data.keyboard.VKey == VK_ESCAPE) {
                    const bool down = (input.data.keyboard.Flags & RI_KEY_BREAK) == 0;
                    if (down && !panel) return CallWindowProcW(previousWindowProc, hwnd, message, wp, lp); // a new press after closing
                    if (down) escapePressed = true, escapeHeldSince = GetTickCount64();
                    escapeHeld = down;
                    return DefWindowProcW(hwnd, message, wp, lp);
                }
            }
        }
        if ((message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && wp == VK_ESCAPE) {
            const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            if (down && !panel) return CallWindowProcW(previousWindowProc, hwnd, message, wp, lp);
            if (down) escapePressed = true, escapeHeldSince = GetTickCount64();
            escapeHeld = down;
            return 0;
        }
    }
    if (panel) {
        switch (message) {
        case WM_LBUTTONDOWN: mouseDown = true; return 0;
        case WM_LBUTTONUP: mouseDown = false; return 0;
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            return 0;
        }
    }
    return CallWindowProcW(previousWindowProc, hwnd, message, wp, lp);
}

HWND GameWindow() {
    struct Find {
        DWORD pid;
        HWND best;
        LONG area;
    } f{GetCurrentProcessId(), nullptr, 0};
    EnumWindows(
        [](HWND w, LPARAM p) -> BOOL {
            auto* f = reinterpret_cast<Find*>(p);
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            RECT r;
            if (pid == f->pid && IsWindowVisible(w) && GetClientRect(w, &r) && (r.right - r.left) * (r.bottom - r.top) > f->area) {
                f->area = (r.right - r.left) * (r.bottom - r.top);
                f->best = w;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&f));
    return f.best;
}

void RestoreWindowProc() {
    if (filteredWindow && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(filteredWindow, GWLP_WNDPROC)) == &PanelWindowProc)
        SetWindowLongPtrW(filteredWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousWindowProc));
    filteredWindow = nullptr;
    escapeHeld = false;
}

// True when the mouse is the panel's.
bool TakeMouse(bool take) {
    if (take) {
        if (capture.load()) return true;
        HWND hwnd = GameWindow();
        if (!hwnd) return false;
        const auto previous = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
        if (!previous) return false;
        if (previous != &PanelWindowProc) {
            previousWindowProc = previous;
            SetLastError(0);
            if (!SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&PanelWindowProc)) && GetLastError()) return false;
        }
        filteredWindow = hwnd;
        mouseDx = 0, mouseDy = 0;
        mouseDown = false;
        escapePressed = false;
        capture = true;
        return true;
    }
    capture = false;
    if (!escapeHeld.load()) RestoreWindowProc(); // (else once the key is up: Frame)
    return false;
}

#ifdef RADIANCE_DEV
// dev: a cursor without the mouse ("x y", UI units, in the file greeneye\radiance\cursor)
std::atomic<int> testCursorX{-1}, testCursorY{-1};
std::atomic<ULONGLONG> testCursorUntil{0};
#endif

// ---- words -----------------------------------------------------------------------------------------------------
std::atomic<int> forcedLanguage{-1}, detectedLanguage{-1};
ULONGLONG nextWords = 0;
std::string words[text::IdCount];

uint32_t Fnv1a32(const uint8_t* p, size_t n) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x1000193u;
    return h;
}

// The game's text for a loca key in the bundle ("" when it has none).
std::string GameText(const uint8_t* bundle, int64_t at) {
    if (!loca || at < 0) return {};
    Text result = MakeText("");
    loca(&result, Fnv1a32(bundle + at, 16));
    if (!result.bytes || !result.length || result.length > 96) return {};
    return std::string(result.bytes, result.length);
}

// Once a second: the game's language (it can change while the game runs) and the words in it.
void RefreshWords(const uint8_t* bundle) {
    const ULONGLONG now = GetTickCount64();
    if (now < nextWords) return;
    nextWords = now + 1000;
    text::Lang lang;
    if (text::Detect(GameText(bundle, legendaryAt), lang)) detectedLanguage = int(lang);
    const int forced = forcedLanguage.load();
    const int use = forced >= 0 ? forced : detectedLanguage.load() >= 0 ? detectedLanguage.load() : 0;
    for (int id = 0; id < text::IdCount; ++id) {
        std::string word;
        // the game's own word, unless the ini asks for another language than the game's
        if (forced < 0 || forced == detectedLanguage.load()) word = GameText(bundle, gameTextAt[id]);
        words[id] = word.empty() ? text::Get(text::Lang(use), text::Id(id)) : word;
    }
}

// ---- drawing ---------------------------------------------------------------------------------------------------
template <class T> T& Field(void* p, size_t n) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(p) + n); }
uint64_t Pack(float x, float y) {
    float xy[] = {x, y};
    uint64_t value;
    memcpy(&value, xy, 8);
    return value;
}
struct Widget {
    alignas(16) uint8_t raw[0x80]{};
    void* handle() { return Field<void*>(raw, 8); }
};
// A positioned child of the current widget.
struct Box {
    Widget widget;
    Box(float x, float y, float w, float h) {
        begin(widget.raw, 0);
        setSize(widget.handle(), Pack(w, h));
        Field<uint64_t>(widget.handle(), 0x70) = Pack(x, y);
    }
    ~Box() { end(widget.raw); }
};
// colour: 0xAABBGGRR
void Fill(Box& box, uint32_t colour) {
    uint32_t image = 0;
    struct Colour {
        uint32_t rgba;
        float brightness;
    } c{colour, 1};
    uint64_t packed;
    memcpy(&packed, &c, 8);
    drawImage(box.widget.raw, static_cast<float*>(static_cast<void*>(static_cast<uint8_t*>(box.widget.handle()) + 0x114)), &image, &packed);
}
void Rect(float x, float y, float w, float h, uint32_t colour) {
    Box box(x, y, w, h);
    Fill(box, colour);
}
// (the label reads "<" as markup: text between < > draws nothing)
void Line(const std::string& value, uint8_t* bundle, uint32_t colour) {
    const Text t = MakeText(value.c_str());
    alignas(16) uint8_t buffer[0x400]{};
    beginLabel(buffer, &t, bundle + styleAt, 0); // the style is only pointed at, never copied
    buffer[0x3d] = 1;
    Field<uint32_t>(buffer, 0xb0) = colour;
    Field<float>(buffer, 0xb4) = 1;
    endLabel(buffer);
}
void LineAt(float x, float y, float w, float h, const std::string& value, uint8_t* bundle, uint32_t colour) {
    Box box(x, y, w, h);
    Line(value, bundle, colour);
}

// ---- the rows --------------------------------------------------------------------------------------------------
enum Row { RowLight, RowBrightness, RowRange, RowColour, RowCount };
struct Preset {
    text::Id name;
    uint8_t r, g, b;
};
constexpr Preset kPresets[] = {
    {text::Torch, 255, 137, 88},      {text::Candle, 255, 170, 90},   {text::WarmWhite, 255, 214, 170}, {text::White, 255, 255, 255},
    {text::Moonlight, 150, 190, 255}, {text::IceBlue, 120, 220, 255}, {text::Emerald, 80, 255, 140},    {text::Violet, 180, 110, 255},
    {text::Crimson, 255, 60, 50},
};
constexpr int kPresetCount = int(std::size(kPresets));
constexpr float kMinBrightness = 0.1f, kMaxBrightness = 3.0f, kMinRange = 2.0f, kMaxRange = 300.0f;

bool open = false;
int row = 0;           // the controller's marked row
bool padShown = false; // the controller is in use: its row is marked
int dragging = -1;     // the slider row the mouse holds
bool mouseWas = false;
float cursorX = 0, cursorY = 0; // the panel's own cursor, UI units
Repeat repeatUp, repeatDown, repeatLeft, repeatRight;
bool toggleWas = false, openWas = false, closeWas = false, acceptWas = false;

int PresetOf(const Settings& s) {
    for (int i = 0; i < kPresetCount; ++i)
        if (int(s.red) == kPresets[i].r && int(s.green) == kPresets[i].g && int(s.blue) == kPresets[i].b) return i;
    return -1;
}

float Clamp(float v, float low, float high) { return v < low ? low : v > high ? high : v; }
float RoundTo(float v, float step) { return floorf(v / step + 0.5f) * step; }

bool Same(const Settings& a, const Settings& b) {
    return a.enabled == b.enabled && a.red == b.red && a.green == b.green && a.blue == b.blue && a.intensity == b.intensity &&
           a.radius == b.radius;
}

// One controller step on a row.
void Step(Settings& s, int which, int direction) {
    switch (which) {
    case RowLight: s.enabled = !s.enabled; break;
    case RowBrightness: s.intensity = Clamp(RoundTo(s.intensity, 0.1f) + 0.1f * direction, kMinBrightness, kMaxBrightness); break;
    case RowRange: s.radius = Clamp(RoundTo(s.radius, 1.0f) + (s.radius >= 20 ? 5.0f : 1.0f) * direction, kMinRange, kMaxRange); break;
    case RowColour: {
        const int now = PresetOf(s);
        const int next = now < 0 ? (direction > 0 ? 0 : kPresetCount - 1) : (now + direction + kPresetCount) % kPresetCount;
        s.red = kPresets[next].r, s.green = kPresets[next].g, s.blue = kPresets[next].b;
        break;
    }
    }
}

constexpr uint32_t kBackground = 0xf0201711u, kAccent = 0xff68cce8u, kWhite = 0xffffffffu, kMuted = 0xffb0b0b0u, kTrack = 0xff40342cu,
                   kMark = 0x38ffffffu, kHover = 0x20ffffffu, kButton = 0xff40342cu, kButtonHover = 0xff5a4a3eu;

struct Pointer {
    bool have = false, down = false, pressed = false;
    float x = -1, y = -1;
    bool In(float left, float top, float w, float h) const { return have && x >= left && x < left + w && y >= top && y < top + h; }
};

// Draws the panel and takes the mouse's part in it. Sets close when Close was clicked.
void DrawPanel(uint8_t* bundle, Settings& s, const Pointer& pointer, bool& close) {
    constexpr float kWidth = 600, kRowHeight = 50, kPad = 24, kValueX = 230, kBar = 250;
    const float height = 66 + float(RowCount) * kRowHeight + 70;
    const float x = 70, y = 230;
    Rect(x, y, kWidth, height, kBackground);
    Rect(x, y, kWidth, 3, kAccent);
    LineAt(x + kPad, y + 18, kWidth - 2 * kPad, 34, "RADIANCE", bundle, kAccent);
    static constexpr text::Id kNames[RowCount] = {text::Light, text::Brightness, text::Range, text::Colour};
    const int preset = PresetOf(s);
    char number[32];
    if (!pointer.down) dragging = -1;
    for (int i = 0; i < RowCount; ++i) {
        const float top = y + 62 + i * kRowHeight;
        const bool hover = pointer.In(x + 8, top, kWidth - 16, kRowHeight - 4);
        if (padShown && i == row) Rect(x + 8, top, kWidth - 16, kRowHeight - 4, kMark);
        else if (hover || dragging == i) Rect(x + 8, top, kWidth - 16, kRowHeight - 4, kHover);
        const bool lit = hover || dragging == i || (padShown && i == row);
        LineAt(x + kPad, top + 10, kValueX - kPad - 8, kRowHeight - 14, words[kNames[i]], bundle, lit ? kWhite : kMuted);
        const float valueX = x + kValueX;
        if (i == RowLight) {
            // a switch: its knob at the right when on
            const float w = 64, h = 26, sy = top + 10;
            Rect(valueX, sy, w, h, s.enabled ? kAccent : kTrack);
            Rect(s.enabled ? valueX + w - h + 3 : valueX + 3, sy + 3, h - 6, h - 6, kWhite);
            LineAt(valueX + w + 16, top + 10, 200, kRowHeight - 14, words[s.enabled ? text::On : text::Off], bundle, lit ? kWhite : kMuted);
            if (hover && pointer.pressed) s.enabled = !s.enabled;
        } else if (i == RowColour) {
            // the colours to pick from; the one in use framed
            constexpr float kChip = 28, kGap = 8;
            for (int c = 0; c < kPresetCount; ++c) {
                const float cx = valueX + c * (kChip + kGap), cy = top + 9;
                const bool over = pointer.In(cx - 3, cy - 3, kChip + 6, kChip + 6);
                if (c == preset || over) Rect(cx - 3, cy - 3, kChip + 6, kChip + 6, c == preset ? kWhite : kMuted);
                Rect(cx, cy, kChip, kChip, 0xff000000u | uint32_t(kPresets[c].b) << 16 | uint32_t(kPresets[c].g) << 8 | kPresets[c].r);
                if (over && pointer.pressed) s.red = kPresets[c].r, s.green = kPresets[c].g, s.blue = kPresets[c].b;
            }
        } else {
            const bool brightness = i == RowBrightness;
            const float low = brightness ? kMinBrightness : kMinRange, high = brightness ? kMaxBrightness : kMaxRange;
            float& value = brightness ? s.intensity : s.radius;
            if (pointer.pressed && pointer.In(valueX - 10, top, kBar + 20, kRowHeight - 4)) dragging = i;
            if (dragging == i && pointer.have)
                value = Clamp(RoundTo(low + (high - low) * Clamp((pointer.x - valueX) / kBar, 0, 1), brightness ? 0.05f : 1.0f), low, high);
            const float fraction = Clamp((value - low) / (high - low), 0, 1);
            Rect(valueX, top + 19, kBar, 8, kTrack);
            if (fraction > 0.004f) Rect(valueX, top + 19, kBar * fraction, 8, kAccent);
            Rect(valueX + kBar * fraction - 4, top + 12, 8, 22, lit ? kWhite : kMuted);
            if (brightness) sprintf_s(number, "%.0f%%", value * 100.0f);
            else sprintf_s(number, "%.0f m", value);
            LineAt(valueX + kBar + 18, top + 10, kWidth - kValueX - kBar - kPad - 18, kRowHeight - 14, number, bundle, lit ? kWhite : kMuted);
        }
    }
    {
        // Close, with the key that also closes
        const float bw = 190, bh = 40, bx = x + kWidth - kPad - bw, by = y + height - 56;
        const bool over = pointer.In(bx, by, bw, bh);
        Rect(bx, by, bw, bh, over ? kButtonHover : kButton);
        LineAt(bx + 18, by + 7, bw - 24, bh - 10, words[text::Close], bundle, over ? kWhite : kMuted);
        LineAt(x + kPad, by + 7, kWidth - bw - 3 * kPad, bh - 10, "Esc", bundle, kMuted);
        if (over && pointer.pressed) close = true;
    }
}

#include "native_panel.h"

// The layer and the parent widget close in their destructors, so a fault while drawing (caught by SafeDraw) never
// leaves them open: an open layer stops the game a few frames later.
struct Layer {
    alignas(16) uint8_t frame[0x400]{};
    explicit Layer(const Text* name) { beginLayer(frame, name, 1, 0x16, -1, 0); }
    ~Layer() { endLayer(frame); }
};
struct Parent {
    Widget widget;
    explicit Parent(const Text* name) {
        named(widget.raw, name);
        opaque(widget.raw);
    }
    ~Parent() { end(widget.raw); }
};

void Draw(Settings& s, bool& close) {
    auto* bundle = Field<uint8_t*>(*root, kBundle);
    if (!bundle) return;
    RefreshWords(bundle);
    static const Text name = MakeText("radianceMenu");
    Layer layer(&name);
    {
        Parent top(&name);
        Widget& parent = top.widget;
        if (parent.handle()) {
            const auto* screen = static_cast<const float*>(static_cast<void*>(static_cast<uint8_t*>(parent.handle()) + 0x114));
            // the panel's own cursor: the raw mouse moves it (one count = one pixel of the game's window)
            Pointer pointer;
            RECT client{};
            const float scale = filteredWindow && GetClientRect(filteredWindow, &client) && client.right > 0 ? screen[2] / float(client.right) : 1.0f;
            cursorX = Clamp(cursorX + float(mouseDx.exchange(0)) * scale, screen[0], screen[0] + screen[2] - 4);
            cursorY = Clamp(cursorY + float(mouseDy.exchange(0)) * scale, screen[1], screen[1] + screen[3] - 4);
            pointer.have = true;
            pointer.x = cursorX, pointer.y = cursorY;
            pointer.down = mouseDown.load();
#ifdef RADIANCE_DEV
            if (GetTickCount64() < testCursorUntil.load()) {
                cursorX = pointer.x = float(testCursorX.load()), cursorY = pointer.y = float(testCursorY.load());
                pointer.down = inp::KeyDown(VK_LBUTTON);
            }
#endif
            pointer.pressed = pointer.down && !mouseWas;
            mouseWas = pointer.down;
            if (pointer.pressed) padShown = false;
            if (native::ready) native::DrawPanel(bundle, s, pointer, close);
            else DrawPanel(bundle, s, pointer, close);
            // the cursor, on top: a white arrow head with a dark edge
            for (int i = 0; i < 9; ++i) Rect(cursorX - 1, cursorY + i * 2.0f - 1, 4 + i * 1.6f + 2, 3, 0xff000000u);
            for (int i = 0; i < 9; ++i) Rect(cursorX, cursorY + i * 2.0f, 4 + i * 1.6f, 2, kWhite);
        }
    }
}

bool SafeDraw(Settings& s, bool& close) {
    __try {
        Draw(s, close);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Close() {
    open = false;
    dragging = -1;
    padPanelUntil = 0;
    TakeMouse(false);
}

void Open() {
    if (!Playing()) return; // only from normal play: any other screen has the mouse and the keys itself
    if (!TakeMouse(true)) {
        Log("the menu could not take the mouse (the game's window was not found)");
        return;
    }
    open = true;
    mouseWas = true; // a button held now is not a click on the panel
    padShown = false;
    cursorX = 70 + 300, cursorY = 230 + 40; // on the panel
    // the keys and buttons held right now are not presses for the panel
    repeatUp.was = repeatDown.was = repeatLeft.was = repeatRight.was = closeWas = acceptWas = true;
}

// Once a frame in the in-game UI pass: the hotkeys, then the panel.
void Frame() {
    const ULONGLONG now = GetTickCount64();
    if (open && now - lastFrame.load() > 1000) Close(); // the UI pass was away (the game left play): not open any more
    lastFrame = now;
    if (g_keysChanged.exchange(false)) LoadKeys(); // the ini was saved: its [Keys] may have changed
    SyncTick(); // other players' lights (sync.h)
#ifdef RADIANCE_DEV
    {
        // dev: "x y" (UI units) in greeneye\\radiance\\cursor is the cursor for the next three seconds
        static ULONGLONG nextLook = 0;
        if (now >= nextLook && !g_ini.empty()) {
            nextLook = now + 100;
            const std::wstring path = g_ini.substr(0, g_ini.rfind(L'\\') + 1) + L"cursor";
            FILE* f = nullptr;
            if (!_wfopen_s(&f, path.c_str(), L"r") && f) {
                int cx = -1, cy = -1;
                if (fscanf_s(f, "%d %d", &cx, &cy) == 2) testCursorX = cx, testCursorY = cy, testCursorUntil = now + 3000;
                fclose(f);
                DeleteFileW(path.c_str());
            }
        }
    }
#endif
    const bool focused = inp::GameHasFocus();
    const inp::PadState pad = focused ? inp::ReadPad() : inp::PadState{};
    auto held = [&](const inp::Binding& b) {
        return focused && (inp::KeysHeld(b, [](int vk) { return inp::KeyDown(vk); }) || inp::PadHeld(b, pad));
    };
    AcquireSRWLockShared(&g_settingsLock);
    Settings s = g_settings;
    ReleaseSRWLockShared(&g_settingsLock);
    const Settings before = s;

    // an Esc press taken for the panel is given back to the game's window once it is up (or stuck: a missed release)
    if (!open && filteredWindow && (!escapeHeld.load() || now - escapeHeldSince.load() > 3000)) RestoreWindowProc();

    const bool playing = Playing();
    const bool toggleDown = held(keys.toggle), openDown = held(keys.open);
    // the on/off key during play or with the panel open (other screens may be using the key)
    if (toggleDown && !toggleWas && (playing || open)) s.enabled = !s.enabled;
    toggleWas = toggleDown;
    const bool canDraw = ready && !faulted.load() && root && *root;
    if (openDown && !openWas && canDraw) {
        if (open) Close();
        else Open();
    }
    openWas = openDown;
    // the game opened a screen of its own (the backpack, the map, a dialog), or lost focus: the panel gives way
    if (open && (!canDraw || !playing || !focused)) Close();

    if (open) {
        padPanelUntil = now + 250;
        const bool chord = openDown; // while the opening chord is still held, its buttons are not the panel's
        const bool closeDown = held(keys.close);
        bool close = (closeDown && !closeWas) || escapePressed.exchange(false);
        closeWas = closeDown;
        const bool up = repeatUp.Update(!chord && held(keys.up), now), down = repeatDown.Update(!chord && held(keys.down), now);
        const bool left = repeatLeft.Update(!chord && held(keys.left), now), right = repeatRight.Update(!chord && held(keys.right), now);
        const bool acceptDown = focused && (pad.buttons & inp::kPadA) != 0; // A flips the switch
        const bool accept = acceptDown && !acceptWas;
        acceptWas = acceptDown;
#ifdef RADIANCE_DEV
        if (up || down || left || right || accept)
            Log("dev: pad nav up %d down %d left %d right %d accept %d (shown %d, row %d, buttons %04x)", up, down, left, right, accept, padShown, row, pad.buttons);
#endif
        if (up || down || left || right || accept) {
            if (!padShown) padShown = true; // the first press only shows where the mark is
            else {
                if (up) row = (row + RowCount - 1) % RowCount;
                if (down) row = (row + 1) % RowCount;
                if (left) Step(s, row, -1);
                if (right) Step(s, row, +1);
                if (accept && row == RowLight) Step(s, row, +1);
            }
        }
        if (!SafeDraw(s, close)) {
            faulted = true;
            close = true;
            Log("the menu stopped after a fault while drawing; the light and its on/off key go on");
        }
        if (close) Close();
    }
    if (!Same(before, s)) SaveSettings(s);
}

std::atomic<bool> frameFaulted{false};
bool SafeFrame() {
    __try {
        Frame();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void PlayHook(void* self) {
    nextPlay(self);
    greeneye::Lease lease; // an older generation after a hot reload only calls on
    if (lease && !g_off.load(std::memory_order_relaxed) && !frameFaulted.load(std::memory_order_relaxed) && !SafeFrame()) {
        frameFaulted = true;
        TakeMouse(false);
        Log("the hotkeys and the menu stopped after a fault; the light goes on as the ini says");
    }
}

// Hot reload: the window procedure is this generation's code, so the game's window gets its own back now.
void Shutdown() {
    Close();
    capture = false;
    RestoreWindowProc();
}

template <class T> bool Bind(const GreeneyeHost* host, T& out, const char* pattern) {
    out = reinterpret_cast<T>(host->find(pattern, nullptr));
    return out != nullptr;
}

// The UI root: every "mov rax, [rip+disp]; mov rcx, [rax+0xd0]" in the game loads the same pointer.
void** FindRoot(const std::vector<uint8_t>& code, uint8_t* textStart) {
    static const uint8_t kTail[] = {0x48, 0x8B, 0x88, 0xD0, 0x00, 0x00, 0x00};
    uint8_t* found = nullptr;
    int matches = 0;
    for (size_t i = 0; i + 14 <= code.size(); ++i) {
        if (code[i] != 0x48 || code[i + 1] != 0x8B || code[i + 2] != 0x05 || memcmp(&code[i + 7], kTail, sizeof(kTail)) != 0) continue;
        int32_t disp;
        memcpy(&disp, &code[i + 3], 4);
        uint8_t* target = textStart + i + 7 + disp;
        if (found && target != found) return nullptr;
        found = target;
        ++matches;
    }
    return matches >= 2 ? reinterpret_cast<void**>(found) : nullptr;
}

// The hotkeys always; the panel when everything it draws with is found.
bool Init(const GreeneyeHost* host, const std::vector<uint8_t>& image, uint8_t* textStart) {
    uint8_t* play = host->find("4C 8B DC 55 49 8D AB E8 FD FF FF 48 81 EC 10 03 00 00", nullptr); // doPlayStateUi
    if (!play) {
        Log("the in-game UI pass was not found in this game build; no hotkeys and no menu (the ini still works)");
        return false;
    }
    kUiScreen = code::FromCode("UiScreen", "80 BB ?? ?? ?? ?? 30 74 ?? 45 32 F6", 2, "80 BF ?? ?? ?? ?? 01 75 ?? 4D 85 C0", 2, 0x1cd8);
    bool ok = true;
    ok &= Bind(host, begin, "40 53 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B D9 45 33 C0");
    ok &= Bind(host, end, "48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 60 80 79 20 00");
    ok &= Bind(host, setSize, "48 89 54 24 08 0F 57 C0 F3 0F 10 54 24 08");
    ok &= Bind(host, beginLayer, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 45 8B D9 44 8B 8C 24 80 00 00 00 41 0F B6 D8");
    ok &= Bind(host, endLayer, "40 53 48 83 EC 20 48 89 74 24 38 48 8B D9 48 8B 35 ?? ?? ?? ?? 48 89 7C 24 40 48 8B 79 10 48 8B 09 E8");
    ok &= Bind(host, named, "48 89 5C 24 08 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B F9 48 8B DA 45 33 C0 48 8B 52 10 48 8B 48 18 48 89 0F 48 C7 47");
    ok &= Bind(host, opaque, "40 53 48 83 EC 20 F3 0F 10 0D ?? ?? ?? ?? 48 8B D9 48 8B 49 08 0F 28 C1 0F 14 C1 66 48 0F 7E C2 E8");
    ok &= Bind(host, beginLabel, "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 50 48 8B DA 41 0F B6 F9");
    ok &= Bind(host, endLabel, "40 55 56 57 48 8D 6C 24 B9 48 81 EC F0 00 00 00 0F 29 BC 24 B0 00 00 00");
    ok &= Bind(host, drawImage, "4D 8B 09 45 8B 00 E9 ?? ?? ?? ?? CC");
    root = FindRoot(image, textStart);
    styleAt = reflect::Offset("keen::FbUiBundle", "keen::FbUiCharacterStyles", "defaultStyle");
    screenPlaying = static_cast<int>(reflect::EnumValue("keen::HudScreenType", "InGame"));
    ok = ok && root && styleAt > 0;
    // Optional: the game's own words. Without them the menu uses its own table (English until the ini says otherwise).
    for (int64_t& at : gameTextAt) at = -1;
    if (Bind(host, loca, "48 89 5C 24 10 48 89 74 24 18 55 57 41 55 41 56 41 57 48 8D AC 24 F0 F8 FF FF")) {
        legendaryAt = reflect::Offset("keen::FbUiBundle", "keen::FbUiLocaRpgTerms", "rarityLegendary");
        gameTextAt[text::On] = reflect::Offset("keen::FbUiBundle", "keen::FbUiLocaMenuSettings", "settingOn");
        gameTextAt[text::Off] = reflect::Offset("keen::FbUiBundle", "keen::FbUiLocaMenuSettings", "settingOff");
        gameTextAt[text::Brightness] = reflect::Offset("keen::FbUiBundle", "keen::FbUiLocaMenuSettings", "brightness");
        gameTextAt[text::Close] = reflect::Offset("keen::FbUiBundle", "keen::FbUiLocaUiActionLabels", "close");
    }
    if (!g_ini.empty()) {
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Menu", L"Language", L"auto", value, 32, g_ini.c_str());
        std::string code8;
        for (const wchar_t* c = value; *c; ++c) code8 += *c < 128 ? char(*c) : '?';
        forcedLanguage = text::ParseCode(code8);
    }
    for (int id = 0; id < text::IdCount; ++id) words[id] = text::Get(text::Lang::en, text::Id(id));
    LoadKeys();
    if (ok) native::Init(host); // optional: without it the panel draws itself
    ready = ok;
    if (!ok)
        Log("the menu's parts were not all found in this game build (root %p, style %lld); the on/off key still works", root, styleAt);
    nextPlay = reinterpret_cast<PlayFn>(host->hook(play, reinterpret_cast<void*>(&PlayHook), 18, nullptr, 0));
    if (!nextPlay) {
        Log("could not hook the in-game UI pass; no hotkeys and no menu (the ini still works)");
        return false;
    }
    if (!inp::InstallPadFilter(host, &PadFilter)) Log("XInput is not as expected; the game also sees the controller's Radiance buttons");
    Log("%s turns the light on or off, %s opens the menu%s (style +0x%llx, play screen %d, game words %s)", inp::BindingToText(keys.toggle).c_str(),
        inp::BindingToText(keys.open).c_str(), ok ? "" : " (menu unavailable)", styleAt, screenPlaying, loca ? "yes" : "no");
    return true;
}

} // namespace menu
