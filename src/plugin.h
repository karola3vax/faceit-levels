#pragma once

#include <ISmmPlugin.h>
#include <engine2/isource2server.h>
#include <steam/isteamhttp.h>
#include <steam/steam_api_common.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

#include "faceit.h"

class CEntityInstance;

class FaceitLevels final : public ISmmPlugin, public IMetamodListener
{
public:
	FaceitLevels();

	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
	bool Unload(char *error, size_t max_length) override;
	void OnLevelInit(const char *, const char *, const char *, const char *, bool, bool) override;
	void OnLevelShutdown() override;

	const char *GetAuthor() override { return "karola3vax"; }
	const char *GetName() override { return "Faceit Levels"; }
	const char *GetDescription() override { return "Shows every player's FACEIT level on the CS2 scoreboard"; }
	const char *GetURL() override { return "https://github.com/karola3vax/faceit-levels"; }
	const char *GetLicense() override { return "AGPL-3.0-only"; }
	const char *GetVersion() override { return FACEIT_LEVELS_VERSION; }
	const char *GetDate() override { return __DATE__; }
	const char *GetLogTag() override { return "FACEIT_LEVELS"; }

private:
	using Clock = std::chrono::steady_clock;

	struct Player
	{
		std::uint64_t steam_id = 0;
		CEntityInstance *controller = nullptr;
		std::uint32_t applied_coin = 0;  // 0: this plugin has not changed the coin
		std::uint32_t original_coin = 0; // the coin before it did
	};

	// One FACEIT lookup. Lives in a std::list: Steam keeps the call result's address.
	struct Request
	{
		FaceitLevels *owner = nullptr;
		std::uint64_t steam_id = 0;
		HTTPRequestHandle handle = INVALID_HTTPREQUEST_HANDLE;
		bool done = false;
		CCallResult<Request, HTTPRequestCompleted_t> call;
		void Completed(HTTPRequestCompleted_t *result, bool io_failure) { owner->Completed(*this, result, io_failure); }
	};

	KHook::Return<void> OnGameFrame(IServerGameDLL *, bool simulating, bool first_tick, bool last_tick);
	void Tick();
	void UpdatePlayer(int slot);
	void RestoreCoin(Player &player);
	void StartRequest(std::uint64_t steam_id);
	void Completed(Request &request, HTTPRequestCompleted_t *result, bool io_failure);
	void ReadConfig();
	void SaveCache();

	KHook::Virtual<IServerGameDLL, void, bool, bool, bool> game_frame_;
	IServerGameDLL *server_ = nullptr;
	std::string directory_;
	faceit::Config config_;
	faceit::Cache cache_;
	bool cache_dirty_ = false;
	std::unordered_map<std::uint64_t, Clock::time_point> retry_at_;
	std::list<Request> requests_;
	std::array<Player, 64> players_ {};
	Clock::time_point next_tick_ {};
	Clock::time_point next_save_ {};
	bool key_warning_shown_ = false;
};

PLUGIN_GLOBALVARS();
