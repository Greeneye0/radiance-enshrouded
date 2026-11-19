#pragma once
// dev only (RADIANCE_DEV): what effects the game has loaded and which ones play near the player, read-only.
// Trigger: create greeneye\radiance\vfxdump while in a world; the result is greeneye\radiance\vfxdump.txt.
// Layout (2026-11-04 build; Auto Loot's nativeflame.h and re/atlas/lights.md): manager = renderer+0x950,
// instances manager+0x50 / count +0x58 (0xC0 each: handle +8, WorldTransform +0x18, resource index +0x70, 64 bytes
// of parameters +0x74); system = manager+8 with the loaded-effect table (bitmap +0x1be0, capacity +0x1be8, 16-byte
// keys +0x1c00, values +0x1c18, records +0x1c38 of 0xC0 with the keen::VfxResource blob at +0x10).
namespace vfxdump {

template <class T> bool Peek(const void* at, T& out) { return SafeCopy(at, &out, sizeof(T)); }

inline void Guid(FILE* f, const uint8_t* g) {
    for (int i = 0; i < 16; ++i) fprintf(f, "%02x", g[i]);
}

// A blob array: {i32 offset from itself, u32 count}.
inline const uint8_t* BlobArray(const uint8_t* at, uint32_t& count) {
    int32_t rel = 0;
    count = 0;
    if (!Peek(at, rel) || !Peek(at + 4, count) || !rel) return nullptr;
    return at + rel;
}

inline void Resource(FILE* f, const uint8_t* blob) {
    uint32_t n = 0;
    float cullMin = 0, cullMax = 0;
    uint32_t emitters = 0;
    Peek(blob, cullMin), Peek(blob + 4, cullMax), Peek(blob + 12, emitters);
    fprintf(f, " cull %.0f-%.0f emitters %u particles[", cullMin, cullMax, emitters);
    if (const uint8_t* particles = BlobArray(blob + 20, n)) {
        for (uint32_t i = 0; i < n && i < 32; ++i) {
            const uint8_t* p = particles + i * 104;
            uint32_t length = 0, maxCount = 0;
            char name[64]{};
            if (const uint8_t* text = BlobArray(p, length)) SafeCopy(text, name, length < 63 ? length : 63);
            Peek(p + 20, maxCount);
            for (char& c : name)
                if (c && (c < 32 || c > 126)) c = '?';
            fprintf(f, "%s%s:%u", i ? " " : "", name, maxCount);
        }
    }
    fprintf(f, "] lightFilters ");
    BlobArray(blob + 84, n);
    fprintf(f, "%u sounds ", n);
    BlobArray(blob + 92, n);
    fprintf(f, "%u models ", n);
    BlobArray(blob + 60, n);
    fprintf(f, "%u meshes ", n);
    BlobArray(blob + 52, n);
    fprintf(f, "%u images ", n);
    BlobArray(blob + 76, n);
    fprintf(f, "%u decals ", n);
    BlobArray(blob + 68, n);
    fprintf(f, "%u params[", n);
    if (const uint8_t* fields = BlobArray(blob + 108, n)) {
        for (uint32_t i = 0; i < n && i < 64; ++i) {
            uint32_t hash = 0, type = 0;
            uint8_t range[2]{};
            Peek(fields + i * 12, hash), Peek(fields + i * 12 + 4, type), Peek(fields + i * 12 + 8, range);
            fprintf(f, "%s%08x:%08x@%u+%u", i ? " " : "", hash, type, range[0], range[1]);
        }
    }
    uint8_t defaults[64]{};
    Peek(blob + 116, defaults);
    fprintf(f, "] defaults ");
    for (uint8_t b : defaults) fprintf(f, "%02x", b);
}

inline void Run(void* renderer, const WorldTransform& player, const wchar_t* path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"w") || !f) return;
    uint8_t *manager = nullptr, *system = nullptr;
    Peek(static_cast<uint8_t*>(renderer) + 0x950, manager);
    if (manager) Peek(manager + 8, system);
    fprintf(f, "renderer %p manager %p system %p\n", renderer, manager, system);
    if (!manager || !system) {
        fclose(f);
        return;
    }
    uint64_t cap = 0;
    uint64_t* used = nullptr;
    uint8_t *keys = nullptr, *records = nullptr;
    uint32_t* values = nullptr;
    Peek(system + 0x1be8, cap), Peek(system + 0x1be0, used), Peek(system + 0x1c00, keys), Peek(system + 0x1c18, values),
        Peek(system + 0x1c38, records);
    if (!used || !keys || !values || !records || cap > 65536) {
        fprintf(f, "effect table not readable (cap %llu)\n", cap);
        fclose(f);
        return;
    }
    // record index -> key index
    static uint32_t keyOf[65536];
    std::memset(keyOf, 0xff, sizeof(keyOf));
    uint32_t loaded = 0;
    for (uint64_t i = 0; i < cap; ++i) {
        uint64_t word = 0;
        uint32_t value = 0;
        if (!Peek(used + i / 64, word) || !(word >> (i % 64) & 1) || !Peek(values + i, value) || value >= 65536) continue;
        keyOf[value] = static_cast<uint32_t>(i);
        ++loaded;
    }
    auto describe = [&](uint32_t index) {
        uint8_t guid[16]{};
        uint8_t* blob = nullptr;
        if (index < 65536 && keyOf[index] != 0xffffffffu) Peek(keys + keyOf[index] * 16ull, guid);
        Peek(records + index * 0xc0ull + 0x10, blob);
        fprintf(f, "res %u guid ", index);
        Guid(f, guid);
        if (blob) Resource(f, blob);
        fprintf(f, "\n");
    };

    uint8_t* instances = nullptr;
    uint64_t count = 0;
    Peek(manager + 0x50, instances), Peek(manager + 0x58, count);
    fprintf(f, "== instances within 12 m (of %llu slots) ==\n", count);
    for (uint64_t i = 0; instances && i < count && i < 65536; ++i) {
        uint8_t record[0xc0];
        if (!Peek(instances + i * 0xc0, record)) continue;
        uint32_t handle, resource;
        WorldTransform t;
        std::memcpy(&handle, record + 8, 4), std::memcpy(&resource, record + 0x70, 4), std::memcpy(&t, record + 0x18, sizeof(t));
        if (!handle) continue;
        double d2 = 0, d[3];
        for (int k = 0; k < 3; ++k) d[k] = static_cast<double>(t.position[k] - player.position[k]) / 4294967296.0, d2 += d[k] * d[k];
        if (d2 > 144.0) continue;
        fprintf(f, "inst slot %llu handle %08x dist %.2f (dx %.2f dy %.2f dz %.2f) scale %.2f %.2f %.2f record ", i, handle, std::sqrt(d2),
                d[0], d[1], d[2], t.scale[0], t.scale[1], t.scale[2]);
        for (uint8_t b : record) fprintf(f, "%02x", b);
        fprintf(f, "\n  ");
        describe(resource);
    }
    fprintf(f, "== loaded effects: %u ==\n", loaded);
    for (uint32_t index = 0; index < 65536; ++index)
        if (keyOf[index] != 0xffffffffu) describe(index);
    fclose(f);
}

} // namespace vfxdump
