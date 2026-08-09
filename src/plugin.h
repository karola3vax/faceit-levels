#pragma once

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>
#include <steam/isteamhttp.h>
#include <steam/steam_gameserver.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

#include "version_gen.h"

class CEntityInstance;

class FaceitLevels final : public ISmmPlugin, public IMetamodListener
{
public:
    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;
    void OnLevelShutdown() override;

    void HookGameFrame(bool simulating, bool firstTick, bool lastTick);
    void HookClientActive(CPlayerSlot slot, bool loadGame, const char* name, uint64 xuid);
    void HookClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char* name, uint64 xuid,
                              const char* networkId);
    void HookSteamActivated();
    void HookSteamDeactivated();

    const char* GetAuthor() override { return PLUGIN_AUTHOR; }
    const char* GetName() override { return PLUGIN_DISPLAY_NAME; }
    const char* GetDescription() override { return PLUGIN_DESCRIPTION; }
    const char* GetURL() override { return PLUGIN_URL; }
    const char* GetLicense() override { return PLUGIN_LICENSE; }
    const char* GetVersion() override { return PLUGIN_FULL_VERSION; }
    const char* GetDate() override { return __DATE__; }
    const char* GetLogTag() override { return PLUGIN_LOGTAG; }

private:
    using Clock = std::chrono::steady_clock;

    struct CacheEntry
    {
        int level = 0;
        std::uint64_t fetchedAt = 0;
        Clock::time_point retryAt{};
    };

    struct PlayerState
    {
        std::uint64_t steamId = 0;
        CEntityInstance* controller = nullptr;
        int level = 0;
        std::uint32_t originalCoin = 0;
        std::uint32_t appliedCoin = 0;
    };

    struct PendingRequest
    {
        PendingRequest(FaceitLevels* owner, std::uint64_t steamId, HTTPRequestHandle request)
            : owner(owner), steamId(steamId), request(request)
        {
        }

        void OnComplete(HTTPRequestCompleted_t* result, bool failed);

        FaceitLevels* owner;
        std::uint64_t steamId;
        HTTPRequestHandle request;
        bool complete = false;
        CCallResult<PendingRequest, HTTPRequestCompleted_t> callback;
    };

    bool LoadConfig();
    void LoadCache();
    void SaveCache();
    bool ResolveSchemaOffsets(char* error, size_t maxlen);
    void RefreshSteamHttp();
    void CancelRequests();
    void Tick();
    void RefreshPlayers();
    void StartRequest(std::uint64_t steamId);
    bool HasPendingRequest(std::uint64_t steamId) const;
    void HandleHttpResult(PendingRequest& request, HTTPRequestCompleted_t* result, bool failed);
    CEntityInstance* FindController(int slot) const;
    void ApplyCoin(PlayerState& player);
    void ClearCoin(PlayerState& player);
    std::uint32_t* GetRanks(CEntityInstance* controller) const;
    void MarkInventoryChanged(CEntityInstance* controller) const;
    bool IsFresh(const CacheEntry& entry, std::uint64_t now) const;

    std::array<PlayerState, 64> players_{};
    std::unordered_map<std::uint64_t, CacheEntry> cache_;
    std::list<PendingRequest> requests_;
    CSteamGameServerAPIContext steamContext_;
    ISteamHTTP* http_ = nullptr;
    std::string pluginDirectory_;
    std::string cachePath_;
    std::string apiKey_;
    std::chrono::seconds refreshInterval_{3600};
    int requestTimeoutSeconds_ = 10;
    int maxConcurrentRequests_ = 4;
    std::ptrdiff_t entitySystemOffset_ = 0;
    std::ptrdiff_t inventoryServicesOffset_ = 0;
    std::ptrdiff_t ranksOffset_ = 0;
    bool cacheDirty_ = false;
    Clock::time_point nextTick_{};
    Clock::time_point nextSave_{};
};

extern FaceitLevels g_FaceitLevels;

PLUGIN_GLOBALVARS();
