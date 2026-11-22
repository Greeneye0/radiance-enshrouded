#pragma once
// Field offsets by name, from the game's own reflection tables (the ones re/data/types.json was dumped from), so a
// patch that moves a field inside the UI bundle needs no mod update.
//
// A type descriptor in .rdata / .data: +0x00 name {char*, len}, +0x20 full name {char*, len} ("keen::FbUiBundle"),
// +0x48 member count (u32), kind (u32; low byte 0x0c / 0x0d: an enum), +0x58 members. A member is 0x30 bytes:
// name {char*, len}, its type's descriptor, offset (u64).
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace reflect {

struct Image {
    uint8_t* base = nullptr;
    size_t size = 0;
    bool Holds(const void* p, size_t n) const {
        const auto at = reinterpret_cast<uintptr_t>(p), from = reinterpret_cast<uintptr_t>(base);
        return at >= from && n <= size && at - from <= size - n;
    }
};

inline Image Exe() {
    Image image;
    image.base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image.base + reinterpret_cast<IMAGE_DOS_HEADER*>(image.base)->e_lfanew);
    image.size = nt->OptionalHeader.SizeOfImage;
    return image;
}

template <class T> T Read(const uint8_t* p, size_t offset) {
    T value;
    std::memcpy(&value, p + offset, sizeof(T));
    return value;
}

inline bool Named(const Image& image, const uint8_t* text, uint64_t length, const char* name) {
    const size_t n = std::strlen(name);
    return length == n && image.Holds(text, n) && std::memcmp(text, name, n) == 0;
}

// A struct's descriptor: plausible member list inside the image.
inline bool IsStruct(const Image& image, const uint8_t* type) {
    if (!image.Holds(type, 0x60)) return false;
    const uint32_t count = Read<uint32_t>(type, 0x48), kind = Read<uint32_t>(type, 0x4c) & 0xff;
    const auto* members = Read<const uint8_t*>(type, 0x58);
    return count > 0 && count <= 2000 && kind != 0x0c && kind != 0x0d && image.Holds(members, count * 0x30ull);
}

// The descriptor of the type with this full name, or null.
inline const uint8_t* Type(const Image& image, const char* fullName) {
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image.base + reinterpret_cast<IMAGE_DOS_HEADER*>(image.base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
        if (std::memcmp(sec->Name, ".rdata", 6) != 0 && std::memcmp(sec->Name, ".data", 6) != 0) continue;
        const uint8_t* start = image.base + sec->VirtualAddress;
        for (size_t off = 0; off + 0x60 <= sec->Misc.VirtualSize; off += 8) {
            const uint8_t* p = start + off;
            if (Named(image, Read<const uint8_t*>(p, 0x20), Read<uint64_t>(p, 0x28), fullName) && IsStruct(image, p)) return p;
        }
    }
    return nullptr;
}

// The offset of container.field, where container is a type somewhere inside root (members followed up to depth
// levels down; the first one found). -1 when there is none.
inline int64_t Find(const Image& image, const uint8_t* type, const char* container, const char* field, int depth) {
    if (!IsStruct(image, type)) return -1;
    const uint32_t count = Read<uint32_t>(type, 0x48);
    const auto* members = Read<const uint8_t*>(type, 0x58);
    const bool here = Named(image, Read<const uint8_t*>(type, 0x20), Read<uint64_t>(type, 0x28), container);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* m = members + i * 0x30ull;
        const uint64_t offset = Read<uint64_t>(m, 0x18);
        if (offset > 0x4000000) return -1;
        if (here) {
            if (Named(image, Read<const uint8_t*>(m, 0), Read<uint64_t>(m, 8), field)) return static_cast<int64_t>(offset);
            continue;
        }
        if (depth <= 0) continue;
        const int64_t inner = Find(image, Read<const uint8_t*>(m, 0x10), container, field, depth - 1);
        if (inner >= 0) return static_cast<int64_t>(offset) + inner;
    }
    return -1;
}

// The value of an enum's member by name ("keen::HudScreenType", "Collection"), or -1. An enum's descriptor has kind
// 0x0c / 0x0d and its members (0x28 each: name {char*, len}, value) at +0x60.
inline int64_t EnumValue(const char* fullName, const char* member) {
    const Image image = Exe();
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image.base + reinterpret_cast<IMAGE_DOS_HEADER*>(image.base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
        if (std::memcmp(sec->Name, ".rdata", 6) != 0 && std::memcmp(sec->Name, ".data", 6) != 0) continue;
        const uint8_t* start = image.base + sec->VirtualAddress;
        for (size_t off = 0; off + 0x68 <= sec->Misc.VirtualSize; off += 8) {
            const uint8_t* p = start + off;
            if (!Named(image, Read<const uint8_t*>(p, 0x20), Read<uint64_t>(p, 0x28), fullName)) continue;
            const uint32_t count = Read<uint32_t>(p, 0x48), kind = Read<uint32_t>(p, 0x4c) & 0xff;
            const auto* members = Read<const uint8_t*>(p, 0x60);
            if ((kind != 0x0c && kind != 0x0d) || !count || count > 1000 || !image.Holds(members, count * 0x28ull)) continue;
            for (uint32_t i = 0; i < count; ++i) {
                const uint8_t* m = members + i * 0x28ull;
                if (Named(image, Read<const uint8_t*>(m, 0), Read<uint64_t>(m, 8), member)) return static_cast<int64_t>(Read<uint64_t>(m, 0x10));
            }
        }
    }
    return -1;
}

// A struct's size (its descriptor's +0x40), or -1.
inline int64_t Size(const char* fullName) {
    const Image image = Exe();
    const uint8_t* type = Type(image, fullName);
    return type ? static_cast<int64_t>(Read<uint32_t>(type, 0x40)) : -1;
}

inline int64_t Offset(const char* root, const char* container, const char* field) {
    const Image image = Exe();
    const uint8_t* type = Type(image, root);
    return type ? Find(image, type, container, field, 6) : -1;
}

} // namespace reflect
