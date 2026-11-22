// Layout read from the game's own code at start, as Find does (its reflect::FromCode), so a patch that moves a
// field needs no mod update. Included in main.cpp's anonymous namespace after Log.
//
//   FromCode: a field offset taken from the displacement of two instructions that use it, each found by a
//   signature with the displacement wildcarded; used only when both are found once and agree.
//   Uses: true when a function still holds an instruction that reads a field the mod fills in (in its first
//   bytes). The game's widget settings structs are not in its reflection; a build that changes one
//   changes these instructions too, and the mod then draws without those widgets instead of filling a wrong layout.
//
// Every signature here was checked on the builds of 2026-10-20, 10-21, 11-02, 11-03 and 11-04.
namespace code {

struct Stats {
    int found = 0, moved = 0, missing = 0;
} stats;

struct Token {
    uint8_t value;
    bool any;
};

inline size_t Parse(const char* pattern, Token* out, size_t max) {
    size_t n = 0;
    for (const char* p = pattern; *p && n < max;) {
        if (*p == ' ') { ++p; continue; }
        if (p[0] == '?') {
            out[n++] = {0, true};
            p += p[1] == '?' ? 2 : 1;
            continue;
        }
        unsigned v = 0;
        if (sscanf_s(p, "%2x", &v) != 1) return 0;
        out[n++] = {uint8_t(v), false};
        p += 2;
    }
    return n;
}

// Hits of pattern in [from, from + size); stops counting at 2. first: the first hit.
inline int Count(const uint8_t* from, size_t size, const char* pattern, const uint8_t** first = nullptr) {
    Token t[128];
    const size_t n = Parse(pattern, t, 128);
    if (!n || size < n) return 0;
    int hits = 0;
    __try {
        for (size_t i = 0; i + n <= size && hits < 2; ++i) {
            size_t k = 0;
            while (k < n && (t[k].any || from[i + k] == t[k].value)) ++k;
            if (k == n) {
                if (!hits && first) *first = from + i;
                ++hits;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return hits;
}

inline bool Text(const uint8_t*& start, size_t& size) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec)
        if (memcmp(sec->Name, ".text", 5) == 0) {
            start = base + sec->VirtualAddress;
            size = sec->Misc.VirtualSize;
            return true;
        }
    return false;
}

// The one place in the game's code that matches pattern, or null.
inline const uint8_t* Unique(const char* pattern) {
    const uint8_t* start = nullptr;
    size_t size = 0;
    const uint8_t* at = nullptr;
    return Text(start, size) && Count(start, size, pattern, &at) == 1 ? at : nullptr;
}

inline size_t FromCode(const char* name, const char* pattern1, int index1, const char* pattern2, int index2, size_t fallback) {
    int32_t got[2] = {-1, -1};
    const char* patterns[2] = {pattern1, pattern2};
    const int index[2] = {index1, index2};
    for (int i = 0; i < 2; ++i)
        if (const uint8_t* at = Unique(patterns[i])) memcpy(&got[i], at + index[i], sizeof(got[i]));
    if (got[0] < 0 || got[1] < 0 || got[0] != got[1]) {
        ++stats.missing;
        Log("layout: %s not read from the game's code (0x%x / 0x%x), using 0x%zx", name, got[0], got[1], fallback);
        return fallback;
    }
    ++stats.found;
    if (size_t(got[0]) != fallback) {
        ++stats.moved;
        Log("layout: %s is at 0x%x in this build (was 0x%zx)", name, got[0], fallback);
    }
    return size_t(got[0]);
}

// Every pattern found in the function's first span bytes; logs the first that is not.
inline bool Uses(const char* what, const void* fn, std::initializer_list<const char*> patterns, size_t span = 0x2000) {
    if (!fn) return false;
    for (const char* p : patterns)
        if (Count(static_cast<const uint8_t*>(fn), span, p) < 1) {
            ++stats.missing;
            Log("layout: %s no longer reads its fields as expected (\"%s\"): not used", what, p);
            return false;
        }
    stats.found += int(patterns.size());
    return true;
}

// A reflected field, logged when it moved or is missing.
inline int64_t Field(const char* type, const char* field, int64_t fallback) {
    const int64_t at = reflect::Offset(type, type, field);
    if (at < 0) {
        ++stats.missing;
        Log("layout: %s.%s not in the game's types, using 0x%llx", type, field, fallback);
        return fallback;
    }
    ++stats.found;
    if (at != fallback) ++stats.moved, Log("layout: %s.%s is at 0x%llx in this build (was 0x%llx)", type, field, at, fallback);
    return at;
}

} // namespace code
