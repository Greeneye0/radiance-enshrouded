#pragma once
// The hidden chat channel that Town Portal (C:\mods\enshrouded\TownPortal\src, main.cpp
// SendHiddenLine, portal_travel.cpp AppendHook / SenderEntity) and Embervale Teleport
// (MapTeleport\src main.cpp SendLine, travel.cpp HostChat) use between a client mod and its
// server mod, for any mod here: a line of chat entry type 255, which every server relays and the
// chat window never draws. Include it in exactly one .cpp per mod. Every hook goes through the
// Greeneye Loader (chained with those mods' own hooks on the same functions).
//
//  - Client -> server: Send(gameClient, line) dispatches chat command 0x74 with type 255. The
//    game sends chat only while the server allows text chat (the user's server has it off), so
//    the client's "text chat allowed" byte is set to 1 around this one dispatch.
//  - Server: the chat store's append is hooked; the handler sees each type-255 line with the
//    sender's handle and account as the server stamped them (never from the text).
//    SenderEntity(account) is that player's entity, from the server's player slots.
//  - Server -> client: the handler answers by replacing the line (Handler returns 1). The server
//    then stores and relays the answer instead, still stamped with the asking player's handle,
//    so a client takes an answer only when its sender handle is its own (OwnHandle).
// Signatures (client exe build 1273043 RVA / server exe RVA): verified unique by
// ArchitectPlacer\tools\verify-signatures.bat and NativeBulk\tools\verify_signatures.py.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include "greeneye.h"

namespace hiddenchat {

inline constexpr uint8_t kType = 255;
inline constexpr size_t kMaxLine = 160;

// The chat store's append (0xE572A0 / 0x90B330), both exes: push rbp, rsi, r12, r13, r15;
// sub rsp, 30h = 13 bytes.
inline constexpr const char* kAppend = "40 55 56 41 54 41 55 41 57 48 83 EC 30 49 8B 41 08 33 ED";
inline constexpr size_t kAppendLen = 13;
// Client: the game client's chat command dispatch (0x852760).
inline constexpr const char* kSend = "4C 8B DC 55 56 57 49 8D AB 18 FF FF FF";
// Client: cmp byte [rdi+allowed], 0 / jne / cmp byte [rdx+1], 4 in the chat command (0x853F5C);
// the disp32 at +2 is the game client's "text chat allowed" byte.
inline constexpr const char* kChatAllowed = "80 BF ?? ?? ?? ?? 00 75 ?? 80 7A 01 04";
inline constexpr size_t kChatAllowedDisp = 2;
// Server: the session's receive (0x7B45B0, server exe only), 7 pushes = 13 bytes; its argument
// is the server object with the player slots.
inline constexpr const char* kServerReceive =
    "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 D8 F6 FF FF 48 81 EC 28 0A 00 00 4C 8B E9";
inline constexpr size_t kServerReceiveLen = 13;

// Game client layout (Town Portal / Embervale Teleport, build 1273043).
inline constexpr size_t kUiClient = 0x1f0;          // the UI client sits here in the game client
inline constexpr size_t kClientStore = 0xf8;        // the client's own chat store (Radiance reads it from the code)
// Radiance: the one place the chat command appends to the client's own store (mov rdx, [rdi + store] / ... / call),
// rdi the game client. It must sit just after the command's chat-permission check (kChatAllowed, same rdi; 0x42 bytes
// on every build 10-20 to 11-04), or the known offset stays.
inline constexpr const char* kStoreSite = "48 8B 97 ?? ?? ?? ?? 89 4C 24 20 48 8D 8D ?? ?? ?? ?? E8 ?? ?? ?? ??";
inline size_t g_clientStore = kClientStore;
inline bool g_clientStoreRead = false;
inline uint8_t* g_storeSite = nullptr; // where kStoreSite and kChatAllowed were found (for the log)
inline uint8_t* g_storeCheck = nullptr;
inline constexpr size_t kClientHandle = 0x466b44;   // the local player's handle
// Server player slots: 16 of 0x148 bytes at +0x1e0, +0 used, +0x18 entity, +0x48 account.
inline constexpr size_t kSlots = 0x1e0, kSlotSize = 0x148, kSlotCount = 16, kSlotEntity = 0x18, kSlotAccount = 0x48;

// One type-255 line. store: the chat store it is appended to (IsClientStore tells which).
// Return 0 to pass it on unchanged, 1 to replace it with answer (NUL-terminated, < capacity),
// 2 to drop it. Runs on the game's network or UI thread under a lease, guarded (__try): no C++
// objects with destructors across a fault.
using Handler = int (*)(void* store, std::string_view line, uint32_t senderHandle, uint64_t account, char* answer,
                        size_t capacity);

namespace detail {
using AppendFn = uint8_t* (*)(uint8_t*, void*, uint8_t, uint64_t*, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t*);
using DispatchFn = void (*)(void* gameClient, void* command);
using ReceiveFn = void (*)(void* server);

inline AppendFn g_nextAppend = nullptr;
inline DispatchFn g_dispatch = nullptr;
inline ReceiveFn g_nextReceive = nullptr;
inline Handler g_handler = nullptr;
inline int32_t g_allowed = 0;           // client: offset of the "text chat allowed" byte (0: unknown)
inline std::atomic<void*> g_server{nullptr}; // server: the session object (receive's argument)
inline std::atomic<unsigned> g_faults{0};

template <class T>
T& At(void* p, size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(p) + offset);
}

inline int Guarded(void* store, uint8_t type, const uint64_t* text, uint32_t handle, uint64_t account, char* answer,
                   size_t capacity)
{
    __try {
        if (type != kType || !text || !text[0] || !text[1] || text[1] > kMaxLine || !g_handler)
            return 0;
        return g_handler(store, std::string_view(reinterpret_cast<const char*>(text[0]), size_t(text[1])), handle,
                         account, answer, capacity);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
}

inline uint8_t* AppendHook(uint8_t* result, void* store, uint8_t type, uint64_t* text, uint32_t a, uint32_t b, uint32_t c,
                           uint64_t account, uint64_t* time)
{
    char answer[kMaxLine + 1]{};
    int action = 0;
    {
        greeneye::Lease lease;
        if (lease)
            action = Guarded(store, type, text, c, account, answer, sizeof(answer));
    }
    if (action == 2) {
        std::memset(result, 0, 8);
        return result;
    }
    if (action == 1 && answer[0]) {
        uint64_t replacement[] = {reinterpret_cast<uint64_t>(answer), std::strlen(answer)};
        return g_nextAppend(result, store, type, replacement, a, b, c, account, time);
    }
    return g_nextAppend(result, store, type, text, a, b, c, account, time);
}

inline void ReceiveHook(void* server)
{
    {
        greeneye::Lease lease;
        if (lease)
            g_server.store(server, std::memory_order_release);
    }
    g_nextReceive(server);
}

inline bool Dispatch(void* gameClient, void* command)
{
    __try {
        if (!g_allowed) {
            g_dispatch(gameClient, command);
            return true;
        }
        uint8_t& allowed = At<uint8_t>(gameClient, size_t(g_allowed));
        const uint8_t saved = allowed;
        allowed = 1;
        g_dispatch(gameClient, command);
        allowed = saved;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

inline uint32_t FindSender(uint64_t account)
{
    auto* s = static_cast<uint8_t*>(g_server.load(std::memory_order_acquire));
    if (!s || !account)
        return 0;
    __try {
        for (size_t i = 0; i < kSlotCount; ++i) {
            uint8_t* slot = s + kSlots + i * kSlotSize;
            if (At<uint32_t>(slot, 0) && At<uint64_t>(slot, kSlotAccount) == account)
                return At<uint32_t>(slot, kSlotEntity);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults.fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
}

} // namespace detail

// Both sides: the append hook, calling handler for every type-255 line. why: the reason when false.
inline bool InstallAppend(const GreeneyeHost* host, Handler handler, std::string& why)
{
    using namespace detail;
    uint8_t* append = host && host->find ? host->find(kAppend, nullptr) : nullptr;
    if (!append) {
        why = "the game's chat append was not found";
        return false;
    }
    g_storeSite = host->find(kStoreSite, nullptr);
    g_storeCheck = host->find(kChatAllowed, nullptr);
    if (g_storeSite && g_storeCheck && g_storeSite > g_storeCheck && g_storeSite - g_storeCheck < 0x100) {
        int32_t disp = 0;
        host->read(g_storeSite + 3, &disp, 4);
        if (disp > 0 && disp < 0x10000)
            g_clientStore = size_t(disp), g_clientStoreRead = true;
    }
    g_handler = handler;
    g_nextAppend = reinterpret_cast<AppendFn>(host->hook(append, reinterpret_cast<void*>(&AppendHook), kAppendLen, nullptr, 0));
    if (!g_nextAppend) {
        why = "the loader refused the chat append hook";
        return false;
    }
    return true;
}

// Client: the chat command dispatch and the "text chat allowed" byte. allowedFound: false when
// the byte was not found (lines then reach only servers with text chat on).
inline bool InstallSend(const GreeneyeHost* host, bool& allowedFound, std::string& why)
{
    using namespace detail;
    allowedFound = false;
    uint8_t* send = host && host->find ? host->find(kSend, nullptr) : nullptr;
    if (!send) {
        why = "the game's chat send was not found";
        return false;
    }
    g_dispatch = reinterpret_cast<DispatchFn>(send);
    if (uint8_t* check = host->find(kChatAllowed, nullptr)) {
        int32_t disp = 0;
        host->read(check + kChatAllowedDisp, &disp, 4);
        if (disp > 0 && disp <= 0x800000)
            g_allowed = disp;
    }
    allowedFound = g_allowed != 0;
    return true;
}

// Server: the receive hook, for SenderEntity.
inline bool InstallServerReceive(const GreeneyeHost* host, std::string& why)
{
    using namespace detail;
    uint8_t* receive = host && host->find ? host->find(kServerReceive, nullptr) : nullptr;
    if (!receive) {
        why = "the server's session receive was not found";
        return false;
    }
    g_nextReceive = reinterpret_cast<ReceiveFn>(host->hook(receive, reinterpret_cast<void*>(&ReceiveHook), kServerReceiveLen,
                                                           nullptr, 0));
    if (!g_nextReceive) {
        why = "the loader refused the session receive hook";
        return false;
    }
    return true;
}

// Client: the game client that owns this UI client (native UiClient()), or null.
inline uint8_t* GameClient(uint8_t* uiClient)
{
    return uiClient ? uiClient - kUiClient : nullptr;
}

// Client: sends one hidden line (at most kMaxLine bytes) to the server. False when it cannot.
inline bool Send(void* gameClient, std::string_view line)
{
    using namespace detail;
    if (!gameClient || !g_dispatch || line.empty() || line.size() > kMaxLine)
        return false;
    alignas(8) uint8_t command[0x410]{};
    command[0] = 0x74;
    command[1] = kType;
    std::memcpy(command + 2, line.data(), line.size());
    At<uint16_t>(command, 0x402) = static_cast<uint16_t>(line.size());
    return Dispatch(gameClient, command);
}

// Client: store is the game client's own chat store (what the server relayed to us).
inline bool IsClientStore(void* gameClient, void* store)
{
    if (!gameClient || !store)
        return false;
    __try {
        return detail::At<void*>(gameClient, g_clientStore) == store;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Client: the local player's full handle (generation included), 0 when unreadable.
inline uint32_t OwnHandle(void* gameClient)
{
    if (!gameClient)
        return 0;
    __try {
        return detail::At<uint32_t>(gameClient, kClientHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Server: the entity of the player whose connection carries account (0: none, or no session yet).
inline uint32_t SenderEntity(uint64_t account)
{
    return detail::FindSender(account);
}

// Server: the session object has been seen (SenderEntity can answer).
inline bool ServerKnown()
{
    return detail::g_server.load(std::memory_order_acquire) != nullptr;
}

inline unsigned Faults()
{
    return detail::g_faults.load(std::memory_order_relaxed);
}

} // namespace hiddenchat
