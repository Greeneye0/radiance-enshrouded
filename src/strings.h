#pragma once
// The menu's words in every language Enshrouded supports. UTF-8; build with /utf-8.
// Where the game has the word itself (On, Off, Brightness, Custom, Select, Change, Close) its own text is shown
// instead (menu.h: GameText); these are the fallback and the rest.
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace radiance::text {

enum class Lang : uint8_t { en, fr, it, de, es, ja, ko, pl, pt, ru, zhHans, zhHant, tr, uk, th, count };
inline constexpr const char* kCodes[] = {"en", "fr", "it", "de", "es", "ja", "ko", "pl", "pt", "ru", "zh-hans", "zh-hant", "tr", "uk", "th"};

enum Id {
    Light, On, Off, Brightness, Range, Colour, Red, Green, Blue, Flicker, Shadows,
    Torch, Candle, WarmWhite, White, Moonlight, IceBlue, Emerald, Violet, Crimson, Custom,
    Select, Change, Close,
    IdCount
};

inline constexpr const char* kTexts[size_t(Lang::count)][IdCount] = {
    // en
    {"Light", "On", "Off", "Brightness", "Range", "Color", "Red", "Green", "Blue", "Flicker", "Shadows",
     "Torch", "Candle", "Warm White", "White", "Moonlight", "Ice Blue", "Emerald", "Violet", "Crimson", "Custom",
     "Select", "Change", "Close"},
    // fr
    {"Lumière", "Activé", "Désactivé", "Luminosité", "Portée", "Couleur", "Rouge", "Vert", "Bleu", "Scintillement", "Ombres",
     "Torche", "Bougie", "Blanc chaud", "Blanc", "Clair de lune", "Bleu glacier", "Émeraude", "Violet", "Cramoisi", "Personnalisé",
     "Sélectionner", "Modifier", "Fermer"},
    // it
    {"Luce", "Attivo", "Disattivo", "Luminosità", "Portata", "Colore", "Rosso", "Verde", "Blu", "Tremolio", "Ombre",
     "Torcia", "Candela", "Bianco caldo", "Bianco", "Chiaro di luna", "Azzurro ghiaccio", "Smeraldo", "Viola", "Cremisi", "Personalizzato",
     "Seleziona", "Modifica", "Chiudi"},
    // de
    {"Licht", "An", "Aus", "Helligkeit", "Reichweite", "Farbe", "Rot", "Grün", "Blau", "Flackern", "Schatten",
     "Fackel", "Kerze", "Warmweiß", "Weiß", "Mondlicht", "Eisblau", "Smaragd", "Violett", "Karmesin", "Benutzerdefiniert",
     "Auswählen", "Ändern", "Schließen"},
    // es
    {"Luz", "Activado", "Desactivado", "Brillo", "Alcance", "Color", "Rojo", "Verde", "Azul", "Parpadeo", "Sombras",
     "Antorcha", "Vela", "Blanco cálido", "Blanco", "Luz de luna", "Azul hielo", "Esmeralda", "Violeta", "Carmesí", "Personalizado",
     "Seleccionar", "Cambiar", "Cerrar"},
    // ja
    {"ライト", "オン", "オフ", "明るさ", "範囲", "色", "赤", "緑", "青", "ちらつき", "影",
     "たいまつ", "ろうそく", "電球色", "白", "月光", "アイスブルー", "エメラルド", "紫", "深紅", "カスタム",
     "選択", "変更", "閉じる"},
    // ko
    {"조명", "켜짐", "꺼짐", "밝기", "범위", "색상", "빨강", "초록", "파랑", "깜빡임", "그림자",
     "횃불", "양초", "따뜻한 흰색", "흰색", "달빛", "얼음빛", "에메랄드", "보라", "진홍", "사용자 지정",
     "선택", "변경", "닫기"},
    // pl
    {"Światło", "Wł.", "Wył.", "Jasność", "Zasięg", "Kolor", "Czerwony", "Zielony", "Niebieski", "Migotanie", "Cienie",
     "Pochodnia", "Świeca", "Ciepła biel", "Biały", "Światło księżyca", "Lodowy błękit", "Szmaragd", "Fiolet", "Karmazyn", "Własny",
     "Wybierz", "Zmień", "Zamknij"},
    // pt (Brazil)
    {"Luz", "Ativado", "Desativado", "Brilho", "Alcance", "Cor", "Vermelho", "Verde", "Azul", "Tremulação", "Sombras",
     "Tocha", "Vela", "Branco quente", "Branco", "Luar", "Azul gelo", "Esmeralda", "Violeta", "Carmesim", "Personalizado",
     "Selecionar", "Alterar", "Fechar"},
    // ru
    {"Свет", "Вкл.", "Выкл.", "Яркость", "Дальность", "Цвет", "Красный", "Зелёный", "Синий", "Мерцание", "Тени",
     "Факел", "Свеча", "Тёплый белый", "Белый", "Лунный свет", "Ледяной", "Изумруд", "Фиолетовый", "Багровый", "Свой",
     "Выбрать", "Изменить", "Закрыть"},
    // zh-hans
    {"光源", "开", "关", "亮度", "范围", "颜色", "红", "绿", "蓝", "闪烁", "阴影",
     "火把", "蜡烛", "暖白", "白", "月光", "冰蓝", "翡翠绿", "紫", "深红", "自定义",
     "选择", "更改", "关闭"},
    // zh-hant
    {"光源", "開", "關", "亮度", "範圍", "顏色", "紅", "綠", "藍", "閃爍", "陰影",
     "火把", "蠟燭", "暖白", "白", "月光", "冰藍", "翡翠綠", "紫", "深紅", "自訂",
     "選擇", "更改", "關閉"},
    // tr
    {"Işık", "Açık", "Kapalı", "Parlaklık", "Menzil", "Renk", "Kırmızı", "Yeşil", "Mavi", "Titreme", "Gölgeler",
     "Meşale", "Mum", "Sıcak beyaz", "Beyaz", "Ay ışığı", "Buz mavisi", "Zümrüt", "Mor", "Kızıl", "Özel",
     "Seç", "Değiştir", "Kapat"},
    // uk
    {"Світло", "Увімк.", "Вимк.", "Яскравість", "Дальність", "Колір", "Червоний", "Зелений", "Синій", "Мерехтіння", "Тіні",
     "Смолоскип", "Свічка", "Теплий білий", "Білий", "Місячне світло", "Крижаний", "Смарагд", "Фіолетовий", "Багряний", "Власний",
     "Вибрати", "Змінити", "Закрити"},
    // th
    {"แสง", "เปิด", "ปิด", "ความสว่าง", "ระยะ", "สี", "แดง", "เขียว", "น้ำเงิน", "การกะพริบ", "เงา",
     "คบเพลิง", "เทียน", "ขาวนวล", "ขาว", "แสงจันทร์", "ฟ้าน้ำแข็ง", "มรกต", "ม่วง", "แดงเข้ม", "กำหนดเอง",
     "เลือก", "เปลี่ยน", "ปิด"},
};

inline const char* Get(Lang lang, Id id) {
    const size_t l = size_t(lang) < size_t(Lang::count) ? size_t(lang) : 0;
    return kTexts[l][id];
}

// "auto" (or anything unknown) is -1; otherwise the language with that code ("de", "zh-hans").
inline int ParseCode(std::string_view code) {
    std::string lower;
    for (char c : code)
        if (c != ' ' && c != '\t') lower += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (lower == "zh" || lower == "zh-cn" || lower == "zh_cn" || lower == "zh_hans") lower = "zh-hans";
    if (lower == "zh-tw" || lower == "zh_tw" || lower == "zh_hant") lower = "zh-hant";
    if (lower == "pt-br" || lower == "pt_br") lower = "pt";
    for (size_t i = 0; i < size_t(Lang::count); ++i)
        if (lower == kCodes[i]) return int(i);
    return -1;
}

// The game's language from its own word for "Legendary" (the rarity), as Find and Embervale Teleport tell it.
// False when the text is empty or unknown; the caller keeps its previous language then.
inline bool Detect(std::string_view legendary, Lang& out) {
    auto has = [&](const char* part) { return legendary.find(part) != std::string_view::npos; };
    auto starts = [&](const char* prefix) {
        size_t n = strlen(prefix);
        if (legendary.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = legendary[i];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (a != prefix[i]) return false;
        }
        return true;
    };
    if (legendary.empty()) return false;
    if (starts("legendary")) out = Lang::en;
    else if (starts("legendär") || starts("legendÄr")) out = Lang::de;
    else if (starts("légend") || starts("lÉgend")) out = Lang::fr;
    else if (starts("legendari")) out = Lang::es;
    else if (starts("leggend")) out = Lang::it;
    else if (starts("legendá") || starts("legendÁ") || starts("lend")) out = Lang::pt;
    else if (starts("legendarn")) out = Lang::pl;
    else if (starts("efsane")) out = Lang::tr;
    else if (has("Леген") || has("ЛЕГЕН") || has("леген")) {
        // Russian or Ukrainian: the letters only Ukrainian has, else its endings (-ний, -на, -не).
        auto ends = [&](const char* suffix) {
            size_t n = strlen(suffix);
            return legendary.size() >= n && legendary.substr(legendary.size() - n) == suffix;
        };
        bool uk = has("і") || has("І") || has("ї") || has("Ї") || has("є") || has("Є") || has("ґ") || has("Ґ") || ends("ний") ||
                  ends("на") || ends("не") || ends("НИЙ") || ends("НА") || ends("НЕ");
        out = uk && !has("ы") && !has("Ы") ? Lang::uk : Lang::ru;
    } else if (has("전설")) out = Lang::ko;
    else if (has("レジェ") || has("伝説")) out = Lang::ja;
    else if (has("传") || has("说")) out = Lang::zhHans;
    else if (has("傳") || has("說")) out = Lang::zhHant;
    else if (legendary.size() >= 3 && uint8_t(legendary[0]) == 0xE0 && (uint8_t(legendary[1]) == 0xB8 || uint8_t(legendary[1]) == 0xB9))
        out = Lang::th;
    else return false;
    return true;
}

} // namespace radiance::text
