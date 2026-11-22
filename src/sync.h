// Other players' lights. Included inside main.cpp's namespace, after the player rows and the menu.
//
// Players who both run Radiance see each other's light. No server mod is needed: each client says what its light is
// in a line of hidden chat (entry type 255, common/hidden_chat.h: every server relays it to every player and the chat
// window never draws it; Town Portals and Embervale Teleport talk to their server parts the same way).
//
//   RAD1 S <entity> <x> <y> <z> <on> <r> <g> <b> <intensity %> <radius> <flicker %>     my light (x y z: where I stand)
//   RAD1 H <entity>                                                                    I just arrived: say yours
//
// Players without Radiance see every hidden line as an empty chat message (Neocybermario's report on 1.0.0, which
// sent one every 10 s), so Radiance says as little as it can: one H when it enters a world, and nothing more until
// another Radiance player is heard (an H or an S that is not its own). From then on: its light when it changes and
// in answer to an H; and an H again (at most once a minute) while a light heard of is on nobody and a player in view
// has none. Another player's light is kept until that player has been out of view and silent for 10 minutes.
// (1.0.0 sent H without an entity; it counts as another player's.)
//
// The server stamps each line with its sender's handle; that is the key a light is kept under. Which character to
// put it on: the sender's entity id when this game shows a player row with that id, else the player row nearest the
// place the sender said it stands (3 m). The row then moves the light every frame.
//
// A world played alone has no chat, so nothing is sent there.
namespace sync {

constexpr int kMaxRemotes = 16;
struct Remote {
    uint32_t sender = 0; // the sending player's handle (0: free)
    uint32_t claimedEntity = 0;
    double claimed[3]{};
    bool on = false;
    uint8_t r = 0, g = 0, b = 0;
    float intensity = 1, radius = 8, flicker = 1;
    ULONGLONG heard = 0;
    uint32_t bound = 0; // the entity (in this game's ECS) the light follows; 0: not found yet
    uint32_t logged = 0;
    ULONGLONG lastSeen = 0; // when its row was last in view
};
SRWLOCK lock = SRWLOCK_INIT;
Remote remotes[kMaxRemotes];
std::atomic<bool> helloHeard{false}; // another player asked: say mine
std::atomic<bool> peerHeard{false};  // another Radiance player is in this world
std::atomic<void*> gameClientSeen{nullptr};
std::atomic<unsigned> linesHeard{0};
#ifdef RADIANCE_DEV
std::atomic<bool> echoOwn{false}; // dev: my own relayed line counts as another player's (file greeneye\radiance\echo)
#endif

// Called by the chat append hook for every hidden line (network or UI thread, under a lease, guarded).
int OnLine(void* store, std::string_view line, uint32_t senderHandle, uint64_t, char*, size_t) {
    if (line.size() < 6 || line.size() > 150 || memcmp(line.data(), "RAD1 ", 5) != 0) return 0;
    void* gameClient = gameClientSeen.load(std::memory_order_relaxed);
    // On this player's own chat store the line is ours alone: taken out, so it fills no chat history. On a store
    // of the server part (this game hosting) it passes on, to be relayed.
    const int verdict = gameClient && hiddenchat::IsClientStore(gameClient, store) ? 2 : 0;
    char text[160]{};
    memcpy(text, line.data(), line.size());
    if (text[5] == 'H') {
        unsigned from = 0;
        sscanf_s(text + 6, "%u", &from);
        bool mine = from && from == g_localEntity.load(std::memory_order_relaxed);
#ifdef RADIANCE_DEV
        if (echoOwn.load()) mine = false;
#endif
        if (!mine) helloHeard = true, peerHeard = true;
        return verdict;
    }
    if (text[5] != 'S') return verdict;
    unsigned entity = 0, on = 0, r = 0, g = 0, b = 0, intensity = 0, radius = 0, flicker = 0;
    double x = 0, y = 0, z = 0;
    if (sscanf_s(text + 6, "%u %lf %lf %lf %u %u %u %u %u %u %u", &entity, &x, &y, &z, &on, &r, &g, &b, &intensity, &radius, &flicker) != 11)
        return verdict;
    linesHeard.fetch_add(1, std::memory_order_relaxed);
    if (!senderHandle) return verdict;
    bool own = entity && entity == g_localEntity.load(std::memory_order_relaxed);
#ifdef RADIANCE_DEV
    if (echoOwn.load()) own = false;
#endif
    if (own) return verdict; // my own line, relayed back
    peerHeard = true;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&lock);
    Remote* slot = nullptr;
    Remote* oldest = &remotes[0];
    for (Remote& remote : remotes) {
        if (remote.sender == senderHandle) slot = &remote;
        if (remote.heard < oldest->heard) oldest = &remote;
    }
    if (!slot) {
        slot = oldest;
        *slot = {};
        slot->sender = senderHandle;
    }
    if (slot->claimedEntity != entity) slot->bound = 0; // (else the light stays on the character it is on)
    slot->claimedEntity = entity;
    slot->claimed[0] = x, slot->claimed[1] = y, slot->claimed[2] = z;
    slot->on = on != 0;
    slot->r = uint8_t(r > 255 ? 255 : r), slot->g = uint8_t(g > 255 ? 255 : g), slot->b = uint8_t(b > 255 ? 255 : b);
    slot->intensity = float(intensity > 500 ? 500 : intensity) / 100.0f;
    slot->radius = float(radius < 1 ? 1 : radius > 300 ? 300 : radius);
    slot->flicker = float(flicker > 500 ? 500 : flicker) / 100.0f;
    slot->heard = now;
    ReleaseSRWLockExclusive(&lock);
    return verdict;
}

// The player row a light belongs on: the sender's entity id if this game has that row near the place, else the
// nearest row to the place. 0 when no player stands there (out of view).
uint32_t Bind(const Remote& remote, const Seen* seen, int count, ULONGLONG now) {
    uint32_t best = 0;
    double bestD2 = 3.0 * 3.0;
    for (int i = 0; i < count; ++i) {
        bool skip = seen[i].local;
#ifdef RADIANCE_DEV
        if (echoOwn.load()) skip = false;
#endif
        if (!seen[i].entity || now - seen[i].at > 500 || skip) continue;
        double d2 = 0;
        for (int k = 0; k < 3; ++k) {
            const double d = double(seen[i].transform.position[k]) / 4294967296.0 - remote.claimed[k];
            d2 += d * d;
        }
        if (seen[i].entity == remote.claimedEntity) return seen[i].entity;
        // by place only while the place is fresh: later someone else may stand there
        if (now - remote.heard < 2000 && d2 < bestD2) bestD2 = d2, best = seen[i].entity;
    }
    return best;
}

Settings lastSent;
bool sentOnce = false;
ULONGLONG nextHello = 0, changedAt = 0, nextAsk = 0;
void* helloFor = nullptr;

bool SendState(void* gameClient, const Settings& s, const WorldTransform& at) {
    char line[160];
    const bool on = s.enabled && s.share && s.intensity > 0;
    sprintf_s(line, "RAD1 S %u %.2f %.2f %.2f %u %u %u %u %u %u %u", g_localEntity.load(), double(at.position[0]) / 4294967296.0,
              double(at.position[1]) / 4294967296.0, double(at.position[2]) / 4294967296.0, on ? 1u : 0u, unsigned(s.red), unsigned(s.green),
              unsigned(s.blue), unsigned(s.intensity * 100.0f + 0.5f), unsigned(s.radius + 0.5f), unsigned(s.flicker * 100.0f + 0.5f));
    return hiddenchat::Send(gameClient, line);
}

bool installed = false;

// Once a frame on the game thread (the in-game UI pass): what to send, and which rows the lights belong on.
void Tick(void* uiClient) {
    if (!installed) return;
    const ULONGLONG now = GetTickCount64();
    void* gameClient = uiClient ? hiddenchat::GameClient(static_cast<uint8_t*>(uiClient)) : nullptr;
    gameClientSeen.store(gameClient, std::memory_order_relaxed);
#ifdef RADIANCE_DEV
    {
        static ULONGLONG nextLook = 0;
        if (now >= nextLook && !g_ini.empty()) {
            nextLook = now + 1000;
            const std::wstring flag = g_ini.substr(0, g_ini.rfind(L'\\') + 1) + L"echo";
            echoOwn = GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
        }
    }
#endif
    AcquireSRWLockShared(&g_settingsLock);
    const Settings s = g_settings;
    ReleaseSRWLockShared(&g_settingsLock);
    Seen seen[kMaxSeen];
    AcquireSRWLockShared(&g_playerLock);
    const WorldTransform at = g_player;
    const ULONGLONG playerAt = g_playerAt;
    memcpy(seen, g_seen, sizeof(seen));
    ReleaseSRWLockShared(&g_playerLock);

    // bind the lights heard of to the rows in view
    AcquireSRWLockExclusive(&lock);
    for (Remote& remote : remotes) {
        if (!remote.sender) continue;
        for (int i = 0; i < kMaxSeen; ++i)
            if (remote.bound && seen[i].entity == remote.bound && now - seen[i].at < 500) remote.lastSeen = now;
        if (now - (remote.heard > remote.lastSeen ? remote.heard : remote.lastSeen) > 600000) remote = {}; // its player left
        else if (!remote.bound && (remote.bound = Bind(remote, seen, kMaxSeen, now)) != 0 && remote.bound != remote.logged)
            remote.logged = remote.bound, Log("another player's light (sender %08x): on %d, colour %u,%u,%u, on the character with entity %08x", remote.sender, int(remote.on),
                remote.r, remote.g, remote.b, remote.bound);
    }
    // a light heard of but not on anyone yet, and a player in view who has none: ask again (at most once a minute;
    // only ever when another Radiance player is here)
    bool unplaced = false, unclaimed = false;
    for (const Remote& remote : remotes) unplaced |= remote.sender && !remote.bound;
    for (int i = 0; i < kMaxSeen && unplaced; ++i) {
        if (!seen[i].entity || seen[i].local || now - seen[i].at > 500) continue;
        bool claimed = false;
        for (const Remote& remote : remotes) claimed |= remote.bound == seen[i].entity;
        unclaimed |= !claimed;
    }
    ReleaseSRWLockExclusive(&lock);

    if (!gameClient || !playerAt || now - playerAt > 500 || !g_localEntity.load()) return;
    if (helloFor != gameClient) { // a world just entered: ask the others, say ours
        helloFor = gameClient;
        sentOnce = false;
        peerHeard = false;
        helloHeard = false;
        AcquireSRWLockExclusive(&lock); // lights heard in another world
        for (Remote& remote : remotes) remote = {};
        ReleaseSRWLockExclusive(&lock);
        nextHello = now + 3000;
    }
    if (nextHello && now >= nextHello) {
        nextHello = 0;
        char hello[32];
        sprintf_s(hello, "RAD1 H %u", g_localEntity.load());
        hiddenchat::Send(gameClient, hello);
    }
    if (!peerHeard.load()) return; // nobody here runs Radiance: say nothing more
    if (unplaced && unclaimed && now >= nextAsk) {
        nextAsk = now + 60000;
        char ask[32];
        sprintf_s(ask, "RAD1 H %u", g_localEntity.load());
        hiddenchat::Send(gameClient, ask);
    }
    const bool on = s.enabled && s.share && s.intensity > 0;
    const bool differs = !sentOnce || on != (lastSent.enabled && lastSent.share && lastSent.intensity > 0) ||
                         (on && (s.red != lastSent.red || s.green != lastSent.green || s.blue != lastSent.blue ||
                                 s.intensity != lastSent.intensity || s.radius != lastSent.radius || s.flicker != lastSent.flicker));
    if (differs && !changedAt) changedAt = now;
    // a change once the player has left it alone for a moment (the first one: the light as it is, once a peer is
    // heard), or an answer to a newcomer
    const bool asked = helloHeard.load();
    if (!((differs && now - changedAt >= 400) || (asked && !differs))) return;
    helloHeard = false;
    changedAt = 0;
    if (SendState(gameClient, s, at)) lastSent = s, sentOnce = true;
}

void Reset() {
    AcquireSRWLockExclusive(&lock);
    for (Remote& remote : remotes) remote = {};
    ReleaseSRWLockExclusive(&lock);
}

bool Init(const GreeneyeHost* host) {
    std::string why;
    bool allowed = false;
    if (!hiddenchat::InstallSend(host, allowed, why) || !hiddenchat::InstallAppend(host, &OnLine, why)) {
        Log("other players' lights are off: %s", why.c_str());
        return false;
    }
    if (!allowed) Log("the chat permission check was not found: lights are shared only on servers with text chat on");
    if (hiddenchat::g_clientStoreRead) {
        ++code::stats.found;
        if (hiddenchat::g_clientStore != hiddenchat::kClientStore) ++code::stats.moved;
    } else {
        ++code::stats.missing;
        Log("layout: the client's chat store was not read from the game's code (site %p, check %p), using 0x%zx", hiddenchat::g_storeSite,
            hiddenchat::g_storeCheck, hiddenchat::g_clientStore);
    }
    installed = true;
    return true;
}

} // namespace sync
