// The Radiance menu drawn with the game's own Settings pieces. Included inside menu.h's namespace, after DrawPanel.
//
// The rows are the game's own settings widgets, the ones its Settings screen uses (Auto Loot's panel calls the same
// two): doSettingOptionWidget<bool> for a "< On >" row and doSettingSlider for a slider row (label, track, knob), with
// the value printed the way the Settings screen prints it. Around them: the game's fancy box (background shape and
// frame), its menu heading style, a separator line, its highlight gradient for the hovered or marked row and its
// small menu button for Close. Every asset is found by name in the UI bundle (reflect.h), the two widget functions by
// signature.
//
// The game's own mouse never reaches these widgets (the panel keeps the camera still by taking the raw mouse), so the
// widgets only draw: what the panel's cursor or the D-pad does is worked out here, and each widget is handed the
// value to show. The option and slider settings structs are not in the game's reflection; their layout is Auto
// Loot's, checked against build 26320373 and later. If either function is missing the panel falls back to
// DrawPanel above.
namespace native {

using SliderFn = uint64_t (*)(void* config, float* value, int* state);
using OptionFn = bool (*)(void* desc, uint8_t* value);
using NineSliceFn = void (*)(void* widget, const float* rect, const void* ref, const float* borders, uint64_t colour);
using UiCtxFn = void* (*)();
using ImageFn = uint32_t* (*)(void* ctx, void* tmp, const void* ref);
using FancyBoxFn = void (*)(void* widget, const void* params);
using ButtonFn = bool (*)(void* button, const void* label);

SliderFn slider{};
OptionFn option{};
NineSliceFn nineSlice{};
UiCtxFn uiCtx{};
ImageFn image{};
FancyBoxFn fancyBox{}; // the framed box the game's dialogs sit in (Find's search dialog is one)
ButtonFn doButton{};     // the dialogs' stone button (doButton)
bool ready = false;

// UI bundle fields, by name
struct Assets {
    int64_t boxBackground = -1, boxFrame = -1, boxFrameColour = -1, boxBackgroundColour = -1;
    int64_t heading = -1, buttonBackground = -1, buttonGlow = -1, buttonOrnament = -1, buttonOrnamentTint = -1, buttonLabel = -1;
    int64_t highlightColour = -1, gradient = -1, separator = -1, nineSliceBorder = -1;
    int64_t rowBackground = -1, rowColour = -1, outline = -1;
} at;

// Settings structs of the two widgets (Auto Loot's panel.cpp has the full field notes).
constexpr size_t kSliderLabel = 0x00, kSliderMin = 0x30, kSliderMax = 0x34, kSliderInteractive = 0x38, kSliderFlag = 0x3d,
                 kSliderSteps = 0x40, kSliderWidth = 0x44, kSliderValueFormat = 0x4c, kSliderValueText = 0x58;
constexpr size_t kOptionChoice = 0x30, kOptionChoiceLabel = 0x08, kOptionCount = 0xc00, kOptionLabel = 0xc08, kOptionArrows = 0xc3a,
                 kOptionInteractive = 0xc3c;

// The settings rows' key-hint list (*(G + 0xd0) + list, its count byte 0x480 on): a hovered settings row adds an
// entry there that only menu screens empty; with no hint bar on the play screen it would fill up (16) and stop the
// game, so the panel takes its rows' entries out again. The panel writes that count byte, so both places are read
// from the game's code (0x32e30 / 0x332b0 on 2026-11-04, 0x18 lower in October) and must agree, or the game's
// rows are not used.
size_t kHintList = 0, kHintCount = 0;
bool ReadHintList() {
    int32_t list = -1, count[2] = {-1, -2};
    if (const uint8_t* site = code::Unique("8D A8 ?? ?? ?? ?? 48 05 6C 5B 01 00")) memcpy(&list, site + 2, 4);
    if (const uint8_t* site = code::Unique("8D 86 ?? ?? ?? ?? 4D 85 FF 48 0F 45 C8")) memcpy(&count[0], site + 2, 4);
    if (const uint8_t* site = code::Unique("8D 81 ?? ?? ?? ?? 48 0F 45 D0 44 88 2A")) memcpy(&count[1], site + 2, 4);
    if (list <= 0 || count[0] != count[1] || count[0] != list + 0x480) {
        ++code::stats.missing;
        Log("layout: the key-hint list was not found in the game's code (list 0x%x, count 0x%x / 0x%x)", list, count[0], count[1]);
        return false;
    }
    kHintList = size_t(list), kHintCount = 0x480;
    code::stats.found += 3;
    if (kHintList != 0x32e30) ++code::stats.moved, Log("layout: the key-hint list is at 0x%zx in this build (was 0x32e30)", kHintList);
    return true;
}
uint8_t* HintCount() {
    uint8_t* ui = nullptr;
    void* first = nullptr;
    if (!kHintList || !root || !*root || !Peek(*root, 0xd0, ui) || !ui || !Peek(ui, 0, first) || !first) return nullptr;
    uint8_t count = 0;
    return Peek(ui, kHintList + kHintCount, count) && count <= 16 ? ui + kHintList + kHintCount : nullptr;
}

alignas(16) uint8_t lightRow[0xd00], colourRow[0xd00];
alignas(16) uint8_t brightnessRow[0x100], rangeRow[0x100];

uint64_t Colour(uint32_t rgba, float brightness = 1.0f) {
    struct {
        uint32_t rgba;
        float brightness;
    } c{rgba, brightness};
    uint64_t packed;
    memcpy(&packed, &c, 8);
    return packed;
}
uint64_t UiColour(const uint8_t* bundle, int64_t field, float alpha = 1.0f) {
    uint32_t rgba = 0xffffffffu;
    float intensity = 1.0f;
    if (field >= 0) memcpy(&rgba, bundle + field, 4), memcpy(&intensity, bundle + field + 4, 4);
    if (alpha < 1.0f) rgba = (rgba & 0x00ffffffu) | uint32_t((rgba >> 24) * Clamp(alpha, 0, 1) + 0.5f) << 24;
    return Colour(rgba, intensity);
}
const float* RectOf(Box& box) { return static_cast<const float*>(static_cast<void*>(static_cast<uint8_t*>(box.widget.handle()) + 0x114)); }

void Slice(float x, float y, float w, float h, const uint8_t* bundle, int64_t texture, uint64_t colour, float scale = 1.0f) {
    if (texture < 0) return;
    Box box(x, y, w, h);
    float borders[4];
    memcpy(borders, bundle + texture + at.nineSliceBorder, sizeof(borders));
    for (float& b : borders) b = Clamp(b * scale, 0, (w < h ? w : h) * 0.5f);
    nineSlice(box.widget.raw, RectOf(box), bundle + texture, borders, colour);
}
void Picture(float x, float y, float w, float h, const uint8_t* bundle, int64_t texture, uint64_t colour) {
    if (texture < 0) return;
    Box box(x, y, w, h);
    alignas(16) uint8_t tmp[16];
    drawImage(box.widget.raw, RectOf(box), image(uiCtx(), tmp, bundle + texture), &colour);
}
// Text in one of the game's styles, in that style's colour; centred inside the box with centre.
enum Align : uint16_t { Left = 0x0201, Centre = 0x0202, Right = 0x0203 };
void Styled(float x, float y, float w, float h, const std::string& value, uint8_t* bundle, int64_t style, Align align = Left,
            float alpha = 1.0f) {
    Box box(x, y, w, h);
    const Text t = MakeText(value.c_str());
    alignas(16) uint8_t buffer[0x400]{};
    beginLabel(buffer, &t, bundle + style, 0); // the style is only pointed at, never copied
    if (align != Left)
        if (auto* handle = Field<uint8_t*>(buffer, 8)) Field<uint16_t>(handle, 0x6c) = align, Field<uint64_t>(handle, 0x90) = Pack(1.0f, 0.0f);
    buffer[0x3d] = 1;
    uint32_t rgba = 0xffffffffu;
    memcpy(&rgba, bundle + style + 0x2c, 4); // UiCharacterStyle.color
    Field<uint32_t>(buffer, 0xb0) = rgba;
    Field<float>(buffer, 0xb4) = alpha;
    endLabel(buffer);
}
// A slider's value as the Settings screen prints it (bold, right aligned in its column).
void Value(float x, float y, float w, float h, const char* utf8, uint8_t* bundle) {
    Box box(x, y, w, h);
    const Text t = MakeText(utf8);
    alignas(16) uint8_t label[0x400]{};
    beginLabel(label, &t, bundle + styleAt, 3);
    if (uint8_t* handle = Field<uint8_t*>(label, 8)) Field<uint16_t>(handle, 0x6c) = 0x0203, Field<uint64_t>(handle, 0x90) = Pack(1.0f, 0.0f);
    label[0x3d] = 1;
    Field<uint16_t>(label, 0x38) = 0x203;
    Field<uint16_t>(label, 0x90) |= 0x42;
    Field<uint16_t>(label, 0x92) = 0;
    label[0x96] |= 1;
    Field<uint64_t>(label, 0xb0) = 0;
    label[0xd4] |= 1;
    endLabel(label);
}

void SetText(uint8_t* where, const std::string& value) {
    const Text t = MakeText(value.c_str());
    memcpy(where, &t, sizeof(t));
}
// An option row with two choices (a bool row): the shown one at value 0, the other at 1.
void OptionRow(uint8_t* desc, const std::string& label, const std::string& first, const std::string& second) {
    memset(desc, 0, 0xd00);
    *reinterpret_cast<uint32_t*>(desc + 0) = 0;
    SetText(desc + kOptionChoiceLabel, first);
    *reinterpret_cast<uint32_t*>(desc + kOptionChoice) = 1;
    SetText(desc + kOptionChoice + kOptionChoiceLabel, second);
    desc[kOptionCount] = 2;
    SetText(desc + kOptionLabel, label);
    desc[kOptionArrows] = 1;
    desc[kOptionInteractive] = 1;
}

// The panel's geometry, as Auto Loot's: rows 600 wide, the Settings screen's slider track from the selectors'
// arrows (half way) to 0.84 of the row, the value in the column after it. doSettingSlider's track starts 1.3 units
// into its box and stops 51.5 short of the row's end (measured in game).
constexpr float kWidth = 704, kRowX = 52, kRowW = 600, kRow = 56, kGap = 6, kTitle = 78;
// With a value to print (format 2: the text at +0x58) the slider keeps a 110 unit value column inside its track
// widget, before the widget's 51.5 unit end gap, all in the row's one band (measured in game). The track widget is the
// row's width times the factor, at the row's right end. The column is reserved with a blank and the number printed
// into it here (the slider's own value label drew a leading 3 as a box).
constexpr float kStartInset = 1.3f, kValueCol = 110.0f, kEndGap = 51.5f, kTrackStart = 0.45f;
constexpr float kSliderW = kRowW;
const float kSliderFactor = 1.0f - (kTrackStart * kRowW - kStartInset) / kRowW;

void SliderRow(uint8_t* config, const std::string& label, float low, float high, const char* value) {
    memset(config, 0, 0x100);
    SetText(config + kSliderLabel, label);
    *reinterpret_cast<float*>(config + kSliderMin) = low;
    *reinterpret_cast<float*>(config + kSliderMax) = high;
    config[kSliderInteractive] = 1;
    config[kSliderFlag] = 1;
    *reinterpret_cast<uint32_t*>(config + kSliderSteps) = 1; // one step: smooth (even steps keep at most 16 marks)
    *reinterpret_cast<float*>(config + kSliderWidth) = kSliderFactor;
    *reinterpret_cast<uint32_t*>(config + kSliderValueFormat) = 2;
    SetText(config + kSliderValueText, ""); // empty: the column stays, its label draws nothing (a blank drew a box)
    (void)value;
}

// Draws the panel and takes the mouse's part in it. Sets close when Close was clicked.
void DrawPanel(uint8_t* bundle, Settings& s, const Pointer& pointer, bool& close) {
    const float height = kTitle + float(RowCount) * (kRow + kGap) + 150;
    const float x = 70, y = 230;
    // the frame the game's dialogs use (style 8 of its fancy box: the full frame with its corner rings over the
    // patterned background), drawn by the game's own function as the text dialog does; the title as the dialog's
    if (fancyBox) {
        Box box(x, y, kWidth, height);
        alignas(16) uint8_t params[0xc0]{};
        *reinterpret_cast<uint32_t*>(params) = 8;
        *reinterpret_cast<float*>(params + 4) = 1.0f;
        fancyBox(box.widget.raw, params);
    } else {
        Slice(x, y, kWidth, height, bundle, at.boxBackground, UiColour(bundle, at.boxBackgroundColour));
        Slice(x, y, kWidth, height, bundle, at.boxFrame, UiColour(bundle, at.boxFrameColour));
    }
    Styled(x + 32, y + 28, kWidth - 64, 40, "RADIANCE", bundle, at.heading, Centre);

    static constexpr text::Id kNames[RowCount] = {text::Light, text::Brightness, text::Range, text::Colour};
    const int preset = PresetOf(s);
    if (!pointer.down) dragging = -1;
    uint8_t* hints = HintCount();
    if (hints && *hints >= 8) *hints = 0; // left over (any source): dropped once half full
    const uint8_t hintsBefore = hints ? *hints : 0;
    char number[32];
    for (int i = 0; i < RowCount; ++i) {
        const float top = y + kTitle + i * (kRow + kGap), rowX = x + kRowX;
        const bool hover = pointer.In(rowX, top, kRowW, kRow);
        if ((padShown && i == row) || hover || dragging == i)
            Picture(rowX, top, kRowW, kRow, bundle, at.gradient, UiColour(bundle, at.highlightColour, padShown && i == row ? 0.9f : 0.6f));
        const bool clickValue = pointer.pressed && pointer.In(rowX + kRowW * 0.47f, top, kRowW * 0.53f, kRow);
        if (i == RowLight) {
            OptionRow(lightRow, words[kNames[i]], words[s.enabled ? text::On : text::Off], words[s.enabled ? text::Off : text::On]);
            if (clickValue) s.enabled = !s.enabled;
            uint8_t value = 0;
            Box box(rowX, top, kRowW, kRow);
            option(lightRow, &value);
        } else if (i == RowColour) {
            // the game's row background and name, and the colours to pick from in it; the one in use outlined
            Slice(rowX, top, kRowW, kRow, bundle, at.rowBackground, UiColour(bundle, at.rowColour));
            Styled(rowX + 28, top + 14, 220, kRow - 20, words[kNames[i]], bundle, styleAt);
            constexpr float kChip = 26, kStep = 33;
            const float first = rowX + kRowW - 30 - kStep * (kPresetCount - 1) - kChip;
            for (int c = 0; c < kPresetCount; ++c) {
                const float cx = first + c * kStep, cy = top + (kRow - kChip) * 0.5f;
                const bool over = pointer.In(cx - 4, cy - 4, kChip + 8, kChip + 8);
                if (c == preset || over)
                    Slice(cx - 5, cy - 5, kChip + 10, kChip + 10, bundle, at.outline,
                          c == preset ? Colour(0xffffffffu) : UiColour(bundle, at.highlightColour));
                Rect(cx, cy, kChip, kChip, 0xff000000u | uint32_t(kPresets[c].b) << 16 | uint32_t(kPresets[c].g) << 8 | kPresets[c].r);
                if (over && pointer.pressed) s.red = kPresets[c].r, s.green = kPresets[c].g, s.blue = kPresets[c].b;
            }
        } else {
            const bool brightness = i == RowBrightness;
            const float low = brightness ? kMinBrightness : kMinRange, high = brightness ? kMaxBrightness : kMaxRange;
            float& value = brightness ? s.intensity : s.radius;
            const float left = rowX + kRowW * (1.0f - kSliderFactor) + kStartInset, right = rowX + kRowW - kEndGap - kValueCol - 3.0f;
            if (pointer.pressed && pointer.In(left - 10, top, right - left + 20, kRow)) dragging = i;
            if (dragging == i && pointer.have && right > left)
                value = Clamp(RoundTo(low + (high - low) * Clamp((pointer.x - left) / (right - left), 0, 1), brightness ? 0.05f : 1.0f), low, high);
            uint8_t* config = brightness ? brightnessRow : rangeRow;
            if (brightness) sprintf_s(number, "%.0f%%", value * 100.0f);
            else sprintf_s(number, "%.0f m", value);
            SliderRow(config, words[kNames[i]], low, high, number);
            float shown = value;
            int state = 0;
            {
                Box box(rowX, top, kSliderW, kRow);
                slider(config, &shown, &state);
            }
            Styled(rowX + kRowW - kEndGap - kValueCol, top + 14, kValueCol, kRow - 20, number, bundle, styleAt, Right);
        }
    }
    if (hints) *hints = hintsBefore;
    {
        // Close: the dialogs' stone button, centred under the rows (as Find's Cancel); Esc or B close as well
        const float bw = 220, bh = 52, bx = x + (kWidth - bw) * 0.5f, by = y + height - bh - 64;
        const bool over = pointer.In(bx, by, bw, bh);
        if (doButton) {
            alignas(16) uint8_t b[0x100]{};
            const Text label = MakeText(words[text::Close].c_str());
            *reinterpret_cast<uint32_t*>(b + 0x04) = 0x10202;
            *reinterpret_cast<uint16_t*>(b + 0x08) = 0x100;
            b[0x0b] = over ? 1 : 0; // shown highlighted: the game's own hover never sees the panel's cursor
            *reinterpret_cast<uint32_t*>(b + 0x10) = 3;
            *reinterpret_cast<float*>(b + 0x4c) = 10.0f;
            *reinterpret_cast<float*>(b + 0x50) = 5.0f;
            *reinterpret_cast<float*>(b + 0x54) = 1.0f;
            b[0x58] = 1;
            memcpy(b + 0x78, &label, sizeof(label));
            Box box(bx, by, bw, bh);
            doButton(b, b + 0x78);
        } else {
            if (over) Picture(bx, by, bw, bh, bundle, at.gradient, UiColour(bundle, at.highlightColour, 0.6f));
            Styled(bx, by + 10, bw, bh - 20, words[text::Close], bundle, styleAt, Centre);
        }
        if (over && pointer.pressed) close = true;
    }
}

template <class T> bool BindFn(const GreeneyeHost* host, T& out, const char* pattern) {
    out = reinterpret_cast<T>(host->find(pattern, nullptr));
    return out != nullptr;
}

bool Init(const GreeneyeHost* host) {
    bool ok = true;
    ok &= BindFn(host, slider, "40 55 53 56 57 41 55 41 56 48 8D AC 24 ?? FA FF FF 48 81 EC ?? 06 00 00");
    // doSettingOptionWidget<bool>: its prologue is shared by every copy, so it is found through a Settings screen's
    // call of it (the flag stores after the call tell that one from the others), as Auto Loot finds it.
    if (uint8_t* call = host->find("48 8D 8D ?? ?? 00 00 E8 ?? ?? ?? ?? 0F B6 8D ?? ?? 00 00 44 0A ?? 8B C1 80 E1 01 D1 E8 24 01 41 88 4E 15", nullptr)) {
        int32_t rel = 0;
        host->read(call + 8, &rel, 4);
        option = reinterpret_cast<OptionFn>(call + 12 + rel);
    } else ok = false;
    ok &= BindFn(host, nineSlice, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 20 57 48 83 EC 30 48 8B F2");
    ok &= BindFn(host, uiCtx, "48 8B 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 4C 8B 41 28");
    ok &= BindFn(host, image,
                 "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 60 41 0F 10 10 33 C0 C7 44 24 30 97 69 E8 F9 48 89 44 24 34 48 8B D9 49 8B 48 08 "
                 "48 8B FA 89 44 24 3C 66 48 0F 7E D0 48 0B C8 0F 10 44 24 30 0F 11 54 24 40 0F 11 44 24 50 75 ?? 48 83 C3 10 EB ?? 48 8B 43 "
                 "10 48 8D 54 24 40 48 83 C3 10 45 33 C0");
    const char* B = "keen::FbUiBundle";
    at.boxBackground = reflect::Offset(B, "keen::FbUiFancyBox", "backgroundShape");
    at.boxFrame = reflect::Offset(B, "keen::FbUiFancyBox", "frame");
    at.boxFrameColour = reflect::Offset(B, "keen::FbUiFancyBox", "frameColor");
    at.boxBackgroundColour = reflect::Offset(B, "keen::FbUiFancyBox", "backgroundColor");
    at.heading = reflect::Offset(B, "keen::FbUiMenuCharacterStyles", "header1");
    const int64_t button = reflect::Offset(B, "keen::FbUiMenuButton", "standaloneSmall");
    const char* T = "keen::FbUiMenuButtonTextures";
    const int64_t bg = reflect::Offset(T, T, "background"), glow = reflect::Offset(T, T, "backGlow"), orn = reflect::Offset(T, T, "ornament");
    if (button >= 0 && bg >= 0) at.buttonBackground = button + bg;
    if (button >= 0 && glow >= 0) at.buttonGlow = button + glow;
    if (button >= 0 && orn >= 0) at.buttonOrnament = button + orn;
    at.buttonOrnamentTint = reflect::Offset(B, "keen::FbUiMenuButton", "ornamentTint");
    at.buttonLabel = reflect::Offset(B, "keen::FbUiMenuButton", "labelStyle");
    at.highlightColour = reflect::Offset(B, "keen::FbUiMenu", "basicWidgetColorHighlighted");
    at.gradient = reflect::Offset(B, "keen::FbUiGradients", "alphaToLeftRight");
    at.separator = reflect::Offset(B, "keen::FbUiPrimitives", "separatorLineHorizontal");
    at.nineSliceBorder = reflect::Offset("keen::UiNineSliceTexture", "keen::UiNineSliceTexture", "border");
    at.rowBackground = reflect::Offset(B, "keen::FbUiMenu", "basicWidgetBackground");
    at.rowColour = reflect::Offset(B, "keen::FbUiMenu", "basicWidgetColor");
    at.outline = reflect::Offset(B, "keen::FbUiMenu", "basicWidgetHighlightOutline");
    // The widgets' settings structs are not reflected: each function must still read the fields the panel fills in
    // (instructions checked on the 10-20 to 11-04 builds), or the panel draws without it.
    ok = ok && code::Uses("doSettingSlider", reinterpret_cast<void*>(slider),
                          {"F3 41 0F 10 4E 34", "41 0F B6 4E 38", "41 38 7E 3D", "41 8B 46 40", "F3 41 0F 59 4E 44", "41 38 4E 48",
                           "41 8B 46 4C", "41 0F 10 46 58"});
    ok = ok && code::Uses("doSettingOptionWidget", reinterpret_cast<void*>(option),
                          {"44 0F B6 BF 00 0C 00 00", "80 BF 3C 0C 00 00 00"});
    ok = ok && ReadHintList();
    // optional: without them the box and Close are drawn from the bundle's pieces
    BindFn(host, fancyBox, "48 8B C4 55 41 56 48 8D A8 B8 FE FF FF 48 81 EC 38 02 00 00 48 89 58 20");
    BindFn(host, doButton, "48 8B C4 48 89 58 18 55 56 57 48 8D A8 08 FD FF FF 48 81 EC E0 03 00 00");
    if (fancyBox && !code::Uses("the dialog frame", reinterpret_cast<void*>(fancyBox),
                                {"F3 0F 10 46 04", "80 7E 60 00", "40 38 BE 98 00 00 00", "40 38 BE B4 00 00 00"}))
        fancyBox = nullptr;
    if (doButton && !code::Uses("the dialog button", reinterpret_cast<void*>(doButton),
                                {"80 7B 09 00", "80 7B 0A 00", "80 7B 0B 00", "8B 4B 0C", "48 8D 53 1C"}))
        doButton = nullptr;
    const int64_t needed[] = {at.boxBackground, at.boxFrame, at.boxFrameColour, at.boxBackgroundColour, at.heading, at.buttonBackground,
                              at.buttonLabel, at.highlightColour, at.gradient, at.separator, at.nineSliceBorder, at.rowBackground,
                              at.rowColour, at.outline};
    for (int64_t v : needed) ok = ok && v >= 0;
    ready = ok;
    Log("menu: %s (slider %p, option %p, dialog frame %p, button %p)", ok ? "the game's own Settings widgets" : "own drawing (the game's widgets were not all found)",
        slider, option, fancyBox, doButton);
    return ok;
}

} // namespace native
