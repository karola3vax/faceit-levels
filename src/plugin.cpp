#include "plugin.h"

#include "faceit_parser.h"

#include <eiface.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <interfaces/interfaces.h>
#include <schemasystem/schemasystem.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

namespace
{
IServerGameDLL* g_server = nullptr;
IServerGameClients* g_clients = nullptr;
IVEngineServer* g_engine = nullptr;
ISchemaSystem* g_schemaSystem = nullptr;
IGameResourceService* g_gameResourceService = nullptr;

constexpr std::array<std::uint32_t, 11> kFaceitCoins = {
    0, 1088, 1087, 1032, 1055, 1041, 1074, 1039, 1067, 1061, 1017,
};

constexpr std::uint32_t kMaxResponseBytes = 1024 * 1024;
constexpr auto kRetryDelay = std::chrono::seconds(60);
constexpr auto kRateLimitRetryDelay = std::chrono::minutes(5);
constexpr auto kAuthRetryDelay = std::chrono::minutes(10);

std::uint64_t UnixNow()
{
    return static_cast<std::uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::string Trim(std::string value)
{
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::unordered_map<std::string, std::string> ReadIni(const std::string& path)
{
    std::unordered_map<std::string, std::string> values;
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        const std::size_t separator = line.find('=');
        if (separator != std::string::npos)
            values[Trim(line.substr(0, separator))] = Trim(line.substr(separator + 1));
    }

    return values;
}

int ReadInt(const std::unordered_map<std::string, std::string>& values, const char* key, int fallback, int minimum,
            int maximum)
{
    const auto value = values.find(key);
    if (value == values.end())
        return fallback;

    int parsed = 0;
    const auto result = std::from_chars(value->second.data(), value->second.data() + value->second.size(), parsed);
    return result.ec == std::errc() && result.ptr == value->second.data() + value->second.size() && parsed >= minimum &&
                   parsed <= maximum
               ? parsed
               : fallback;
}

std::optional<std::ptrdiff_t> FindSchemaOffset(const char* className, const char* fieldName)
{
#ifdef _WIN32
    constexpr const char* moduleName = "server.dll";
#else
    constexpr const char* moduleName = "libserver.so";
#endif

    CSchemaSystemTypeScope* scope = g_schemaSystem->FindTypeScopeForModule(moduleName);
    if (!scope)
        return std::nullopt;

    SchemaClassInfoData_t* classInfo = scope->FindDeclaredClass(className).Get();
    if (!classInfo)
        return std::nullopt;

    for (int i = 0; i < classInfo->m_nFieldCount; ++i)
    {
        const SchemaClassFieldData_t& field = classInfo->m_pFields[i];
        if (field.m_pszName && std::strcmp(field.m_pszName, fieldName) == 0)
            return field.m_nSingleInheritanceOffset;
    }

    return std::nullopt;
}

CEntityInstance* GetEntity(CGameEntitySystem* entitySystem, int index)
{
    if (!entitySystem || index < 0 || index >= MAX_TOTAL_ENTITIES - 1)
        return nullptr;

    CEntityIdentity* chunk = entitySystem->m_EntityList.m_pIdentityChunks[index / MAX_ENTITIES_IN_LIST];
    if (!chunk)
        return nullptr;

    CEntityIdentity* identity = &chunk[index % MAX_ENTITIES_IN_LIST];
    return identity->GetEntityIndex().Get() == index ? identity->m_pInstance : nullptr;
}
}

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientActive, SH_NOATTRIB, 0, CPlayerSlot, bool, const char*, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason,
                   const char*, uint64, const char*);
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIActivated, SH_NOATTRIB, 0);
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIDeactivated, SH_NOATTRIB, 0);

FaceitLevels g_FaceitLevels;
PLUGIN_EXPOSE(FaceitLevels, g_FaceitLevels);

bool FaceitLevels::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_ANY(GetServerFactory, g_server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
    GET_V_IFACE_ANY(GetServerFactory, g_clients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
    GET_V_IFACE_ANY(GetEngineFactory, g_schemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_gameResourceService, IGameResourceService,
                        GAMERESOURCESERVICESERVER_INTERFACE_VERSION);

    pluginDirectory_ = std::string(ismm->GetBaseDir()) + "/addons/faceit_levels";
    cachePath_ = pluginDirectory_ + "/cache.tsv";
    LoadConfig();

    if (!ResolveSchemaOffsets(error, maxlen))
        return false;

    LoadCache();
    g_SMAPI->AddListener(this, this);

    SH_ADD_HOOK(IServerGameDLL, GameFrame, g_server, SH_MEMBER(this, &FaceitLevels::HookGameFrame), true);
    SH_ADD_HOOK(IServerGameClients, ClientActive, g_clients, SH_MEMBER(this, &FaceitLevels::HookClientActive), true);
    SH_ADD_HOOK(IServerGameClients, ClientDisconnect, g_clients, SH_MEMBER(this, &FaceitLevels::HookClientDisconnect),
                true);
    SH_ADD_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_server,
                SH_MEMBER(this, &FaceitLevels::HookSteamActivated), true);
    SH_ADD_HOOK(IServerGameDLL, GameServerSteamAPIDeactivated, g_server,
                SH_MEMBER(this, &FaceitLevels::HookSteamDeactivated), false);

    RefreshSteamHttp();
    nextTick_ = Clock::now();
    nextSave_ = nextTick_ + std::chrono::seconds(5);

    META_CONPRINTF("[%s] %s %s loaded%s.\n", PLUGIN_LOGTAG, PLUGIN_DISPLAY_NAME, PLUGIN_FULL_VERSION,
                   late ? " (late)" : "");
    if (apiKey_.empty())
        META_CONPRINTF("[%s] Set api_key in addons/faceit_levels/config.ini, then reload the plugin.\n", PLUGIN_LOGTAG);

    return true;
}

bool FaceitLevels::Unload(char*, size_t)
{
    for (PlayerState& player : players_)
        ClearCoin(player);

    if (cacheDirty_)
        SaveCache();
    CancelRequests();

    SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_server, SH_MEMBER(this, &FaceitLevels::HookGameFrame), true);
    SH_REMOVE_HOOK(IServerGameClients, ClientActive, g_clients, SH_MEMBER(this, &FaceitLevels::HookClientActive), true);
    SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_clients,
                   SH_MEMBER(this, &FaceitLevels::HookClientDisconnect), true);
    SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_server,
                   SH_MEMBER(this, &FaceitLevels::HookSteamActivated), true);
    SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIDeactivated, g_server,
                   SH_MEMBER(this, &FaceitLevels::HookSteamDeactivated), false);

    return true;
}

void FaceitLevels::OnLevelShutdown()
{
    for (PlayerState& player : players_)
    {
        player.controller = nullptr;
        player.originalCoin = 0;
        player.appliedCoin = 0;
    }
}

void FaceitLevels::HookGameFrame(bool simulating, bool, bool)
{
    if (!simulating)
        return;

    const auto now = Clock::now();
    if (now >= nextTick_)
    {
        nextTick_ = now + std::chrono::seconds(1);
        Tick();
    }
}

void FaceitLevels::HookClientActive(CPlayerSlot slot, bool, const char*, uint64 xuid)
{
    if (!slot.IsValid() || slot.Get() >= static_cast<int>(players_.size()) || xuid == 0)
        return;

    PlayerState& player = players_[slot.Get()];
    player = {};
    player.steamId = xuid;
}

void FaceitLevels::HookClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason, const char*, uint64, const char*)
{
    if (slot.IsValid() && slot.Get() < static_cast<int>(players_.size()))
        players_[slot.Get()] = {};
}

void FaceitLevels::HookSteamActivated()
{
    RefreshSteamHttp();
}

void FaceitLevels::HookSteamDeactivated()
{
    CancelRequests();
    http_ = nullptr;
    steamContext_.Clear();
}

bool FaceitLevels::LoadConfig()
{
    const std::string path = pluginDirectory_ + "/config.ini";
    const auto values = ReadIni(path);
    const auto key = values.find("api_key");
    apiKey_ = key == values.end() ? "" : key->second;
    refreshInterval_ = std::chrono::minutes(ReadInt(values, "refresh_minutes", 60, 1, 1440));
    requestTimeoutSeconds_ = ReadInt(values, "request_timeout_seconds", 10, 1, 60);
    maxConcurrentRequests_ = ReadInt(values, "max_concurrent_requests", 4, 1, 16);
#ifdef _WIN32
    entitySystemOffset_ = ReadInt(values, "game_entity_system_offset_windows", 88, 8, 512);
#else
    entitySystemOffset_ = ReadInt(values, "game_entity_system_offset_linux", 80, 8, 512);
#endif

    if (values.empty())
        META_CONPRINTF("[%s] Could not read %s; defaults are active and API requests are disabled.\n", PLUGIN_LOGTAG,
                       path.c_str());
    return !values.empty();
}

void FaceitLevels::LoadCache()
{
    std::ifstream file(cachePath_);
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream row(line);
        std::uint64_t steamId = 0;
        std::uint64_t fetchedAt = 0;
        int level = 0;
        if (row >> steamId >> level >> fetchedAt && steamId != 0 && level >= 0 && level <= 10 && fetchedAt != 0)
            cache_[steamId] = {level, fetchedAt, {}};
    }
}

void FaceitLevels::SaveCache()
{
    std::ofstream file(cachePath_, std::ios::trunc);
    if (!file)
    {
        META_CONPRINTF("[%s] Could not save %s.\n", PLUGIN_LOGTAG, cachePath_.c_str());
        return;
    }

    file << "# steamid64 level fetched_at_unix\n";
    for (const auto& item : cache_)
        file << item.first << ' ' << item.second.level << ' ' << item.second.fetchedAt << '\n';

    if (file.good())
        cacheDirty_ = false;
}

bool FaceitLevels::ResolveSchemaOffsets(char* error, size_t maxlen)
{
    const auto inventory = FindSchemaOffset("CCSPlayerController", "m_pInventoryServices");
    const auto ranks = FindSchemaOffset("CCSPlayerController_InventoryServices", "m_rank");
    if (!inventory || !ranks)
    {
        g_SMAPI->Format(error, maxlen, "Could not resolve current CS2 inventory schema fields");
        return false;
    }

    inventoryServicesOffset_ = *inventory;
    ranksOffset_ = *ranks;
    return true;
}

void FaceitLevels::RefreshSteamHttp()
{
    if (http_ || !steamContext_.Init())
        return;
    http_ = steamContext_.SteamHTTP();
}

void FaceitLevels::CancelRequests()
{
    for (PendingRequest& request : requests_)
    {
        request.callback.Cancel();
        if (http_ && request.request != INVALID_HTTPREQUEST_HANDLE)
            http_->ReleaseHTTPRequest(request.request);
        request.request = INVALID_HTTPREQUEST_HANDLE;
    }
    requests_.clear();
}

void FaceitLevels::Tick()
{
    requests_.remove_if([](const PendingRequest& request) { return request.complete; });
    RefreshSteamHttp();
    RefreshPlayers();

    const auto now = Clock::now();
    if (cacheDirty_ && now >= nextSave_)
    {
        SaveCache();
        nextSave_ = now + std::chrono::seconds(5);
    }
}

void FaceitLevels::RefreshPlayers()
{
    const std::uint64_t now = UnixNow();
    for (int slot = 0; slot < static_cast<int>(players_.size()); ++slot)
    {
        const std::uint64_t xuid = g_engine->GetClientXUID(CPlayerSlot(slot));
        PlayerState& player = players_[slot];
        if (xuid == 0)
        {
            player = {};
            continue;
        }

        if (player.steamId != xuid)
        {
            player = {};
            player.steamId = xuid;
        }

        CEntityInstance* controller = FindController(slot);
        if (controller != player.controller)
        {
            player.controller = controller;
            player.originalCoin = 0;
            player.appliedCoin = 0;
        }

        const auto cached = cache_.find(xuid);
        if (cached != cache_.end())
            player.level = cached->second.level;
        else
            player.level = 0;

        if (cached == cache_.end() || !IsFresh(cached->second, now))
            StartRequest(xuid);

        ApplyCoin(player);
    }
}

void FaceitLevels::StartRequest(std::uint64_t steamId)
{
    if (!http_ || apiKey_.empty() || requests_.size() >= static_cast<std::size_t>(maxConcurrentRequests_) ||
        HasPendingRequest(steamId))
        return;

    CacheEntry& cache = cache_[steamId];
    if (Clock::now() < cache.retryAt)
        return;

    const std::string url =
        "https://open.faceit.com/data/v4/players?game=cs2&game_player_id=" + std::to_string(steamId);
    const HTTPRequestHandle handle = http_->CreateHTTPRequest(k_EHTTPMethodGET, url.c_str());
    if (handle == INVALID_HTTPREQUEST_HANDLE)
    {
        cache.retryAt = Clock::now() + kRetryDelay;
        return;
    }

    const std::string authorization = "Bearer " + apiKey_;
    http_->SetHTTPRequestHeaderValue(handle, "Authorization", authorization.c_str());
    http_->SetHTTPRequestHeaderValue(handle, "Accept", "application/json");
    http_->SetHTTPRequestRequiresVerifiedCertificate(handle, true);
    http_->SetHTTPRequestNetworkActivityTimeout(handle, static_cast<uint32>(requestTimeoutSeconds_));
    http_->SetHTTPRequestAbsoluteTimeoutMS(handle, static_cast<uint32>(requestTimeoutSeconds_ * 1000));

    SteamAPICall_t call = k_uAPICallInvalid;
    if (!http_->SendHTTPRequest(handle, &call) || call == k_uAPICallInvalid)
    {
        http_->ReleaseHTTPRequest(handle);
        cache.retryAt = Clock::now() + kRetryDelay;
        return;
    }

    requests_.emplace_back(this, steamId, handle);
    PendingRequest& request = requests_.back();
    request.callback.SetGameserverFlag();
    request.callback.Set(call, &request, &PendingRequest::OnComplete);
}

bool FaceitLevels::HasPendingRequest(std::uint64_t steamId) const
{
    return std::any_of(requests_.begin(), requests_.end(),
                       [steamId](const PendingRequest& request) { return !request.complete && request.steamId == steamId; });
}

void FaceitLevels::PendingRequest::OnComplete(HTTPRequestCompleted_t* result, bool failed)
{
    owner->HandleHttpResult(*this, result, failed);
    complete = true;
}

void FaceitLevels::HandleHttpResult(PendingRequest& request, HTTPRequestCompleted_t* result, bool failed)
{
    CacheEntry& cache = cache_[request.steamId];
    bool cacheResult = false;
    int level = 0;
    int status = result ? static_cast<int>(result->m_eStatusCode) : 0;

    if (!failed && result && result->m_bRequestSuccessful && status >= 200 && status <= 299 &&
        result->m_unBodySize > 0 && result->m_unBodySize <= kMaxResponseBytes)
    {
        std::vector<char> body(result->m_unBodySize + 1, '\0');
        if (http_->GetHTTPResponseBodyData(result->m_hRequest, reinterpret_cast<uint8*>(body.data()),
                                           result->m_unBodySize))
        {
            level = ParseFaceitLevel(std::string_view(body.data(), result->m_unBodySize));
            cacheResult = true;
        }
    }
    else if (!failed && result && result->m_bRequestSuccessful && status == 404)
    {
        cacheResult = true;
    }

    if (cacheResult)
    {
        cache.level = level;
        cache.fetchedAt = UnixNow();
        cache.retryAt = {};
        if (!cacheDirty_)
            nextSave_ = Clock::now() + std::chrono::seconds(5);
        cacheDirty_ = true;

        for (PlayerState& player : players_)
            if (player.steamId == request.steamId)
                player.level = level;
    }
    else
    {
        const auto delay = status == 401 || status == 403
                               ? kAuthRetryDelay
                               : (status == 429 ? kRateLimitRetryDelay : kRetryDelay);
        cache.retryAt = Clock::now() + delay;
        META_CONPRINTF("[%s] FACEIT request failed for %llu (HTTP %d); retry scheduled.\n", PLUGIN_LOGTAG,
                       static_cast<unsigned long long>(request.steamId), status);
    }

    if (http_ && request.request != INVALID_HTTPREQUEST_HANDLE)
        http_->ReleaseHTTPRequest(request.request);
    request.request = INVALID_HTTPREQUEST_HANDLE;
}

CEntityInstance* FaceitLevels::FindController(int slot) const
{
    auto* service = reinterpret_cast<std::byte*>(g_gameResourceService);
    auto* entitySystem = *reinterpret_cast<CGameEntitySystem**>(service + entitySystemOffset_);
    if (!entitySystem)
        return nullptr;

    CEntityInstance* controller = GetEntity(entitySystem, slot + 1);
    return controller && controller->GetEntityIndex().Get() == slot + 1 ? controller : nullptr;
}

std::uint32_t* FaceitLevels::GetRanks(CEntityInstance* controller) const
{
    if (!controller)
        return nullptr;

    auto* bytes = reinterpret_cast<std::byte*>(controller);
    auto* inventory = *reinterpret_cast<std::byte**>(bytes + inventoryServicesOffset_);
    return inventory ? reinterpret_cast<std::uint32_t*>(inventory + ranksOffset_) : nullptr;
}

void FaceitLevels::ApplyCoin(PlayerState& player)
{
    if (!player.controller || player.level < 1 || player.level > 10)
    {
        ClearCoin(player);
        return;
    }

    std::uint32_t* ranks = GetRanks(player.controller);
    if (!ranks)
        return;

    const std::uint32_t wanted = kFaceitCoins[player.level];
    if (player.appliedCoin == 0)
        player.originalCoin = ranks[5];

    if (ranks[5] != wanted)
    {
        ranks[5] = wanted;
        MarkInventoryChanged(player.controller);
    }
    player.appliedCoin = wanted;
}

void FaceitLevels::ClearCoin(PlayerState& player)
{
    if (!player.controller || player.appliedCoin == 0)
        return;

    std::uint32_t* ranks = GetRanks(player.controller);
    if (ranks && ranks[5] == player.appliedCoin)
    {
        ranks[5] = player.originalCoin;
        MarkInventoryChanged(player.controller);
    }
    player.originalCoin = 0;
    player.appliedCoin = 0;
}

void FaceitLevels::MarkInventoryChanged(CEntityInstance* controller) const
{
    controller->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(inventoryServicesOffset_)));
}

bool FaceitLevels::IsFresh(const CacheEntry& entry, std::uint64_t now) const
{
    return entry.fetchedAt != 0 && entry.fetchedAt <= now &&
           now - entry.fetchedAt < static_cast<std::uint64_t>(refreshInterval_.count());
}
