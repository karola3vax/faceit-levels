#include "plugin.h"

#include "game.h"

#include <engine2/igameresourceservice.h>
#include <interfaces/interfaces.h>
#include <schemasystem/ischemasystem.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace
{

constexpr std::uint32_t kMaxReplyBytes = 1024 * 1024;
constexpr auto kTickInterval = std::chrono::seconds(1);
constexpr auto kSaveDelay = std::chrono::seconds(5);
constexpr auto kRetryDelay = std::chrono::minutes(1);
constexpr auto kRateLimitDelay = std::chrono::minutes(5);
constexpr auto kKeyRejectedDelay = std::chrono::minutes(10);
// Cache entries nobody has needed for a week are dropped when the cache is saved.
constexpr std::uint64_t kCacheKeepSeconds = 7 * 24 * 60 * 60;

std::uint64_t UnixNow()
{
	return static_cast<std::uint64_t>(std::time(nullptr));
}

std::string ReadFile(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

FaceitLevels g_FaceitLevels;
PLUGIN_EXPOSE(FaceitLevels, g_FaceitLevels);

FaceitLevels::FaceitLevels()
	: game_frame_(&IServerGameDLL::GameFrame, this, nullptr, &FaceitLevels::OnGameFrame)
{
}

bool FaceitLevels::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	ISchemaSystem *schema = nullptr;
	GET_V_IFACE_ANY(GetServerFactory, server_, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_CURRENT(GetEngineFactory, schema, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService,
		GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	if (!game::Init(schema, error, maxlen)) return false;

	directory_ = std::string(ismm->GetBaseDir()) + "/addons/faceit_levels";
	ReadConfig();
	cache_ = faceit::ParseCache(ReadFile(directory_ + "/cache.tsv"));
	g_SMAPI->AddListener(this, this);
	game_frame_.Add(server_);
	META_CONPRINTF("[FACEIT_LEVELS] Faceit Levels %s loaded%s.\n", FACEIT_LEVELS_VERSION, late ? " late" : "");
	return true;
}

bool FaceitLevels::Unload(char *, size_t)
{
	game_frame_.Remove(server_);
	for (int slot = 0; slot < static_cast<int>(players_.size()); ++slot)
		if (players_[slot].controller == game::Controller(slot)) RestoreCoin(players_[slot]);
	ISteamHTTP *http = SteamGameServerHTTP();
	for (Request &request : requests_)
	{
		request.call.Cancel();
		if (http && request.handle != INVALID_HTTPREQUEST_HANDLE) http->ReleaseHTTPRequest(request.handle);
	}
	requests_.clear();
	if (cache_dirty_) SaveCache();
	return true;
}

void FaceitLevels::OnLevelInit(const char *, const char *, const char *, const char *, bool, bool)
{
	ReadConfig();
}

void FaceitLevels::OnLevelShutdown()
{
	// The controllers are destroyed with the map; their coins go with them.
	players_.fill({});
	if (cache_dirty_) SaveCache();
}

KHook::Return<void> FaceitLevels::OnGameFrame(IServerGameDLL *, bool simulating, bool, bool)
{
	const auto now = Clock::now();
	if (simulating && now >= next_tick_)
	{
		next_tick_ = now + kTickInterval;
		Tick();
	}
	return {KHook::Action::Ignore};
}

void FaceitLevels::Tick()
{
	requests_.remove_if([](const Request &request) { return request.done; });
	for (int slot = 0; slot < static_cast<int>(players_.size()); ++slot) UpdatePlayer(slot);
	if (cache_dirty_ && Clock::now() >= next_save_) SaveCache();
}

void FaceitLevels::UpdatePlayer(int slot)
{
	Player &player = players_[slot];
	CEntityInstance *controller = game::Controller(slot);
	const std::uint64_t steam_id = controller ? game::SteamId(controller) : 0;
	if (controller != player.controller || steam_id != player.steam_id) player = {steam_id, controller};
	if (steam_id == 0) return;

	const auto cached = cache_.find(steam_id);
	if (cached == cache_.end()
		|| UnixNow() - cached->second.fetched_at >= static_cast<std::uint64_t>(config_.refresh_minutes) * 60)
		StartRequest(steam_id);

	std::uint32_t *coin = game::Coin(controller);
	const std::uint32_t wanted = cached == cache_.end() ? 0 : faceit::LevelCoin(cached->second.level);
	if (!coin) return;
	if (wanted == 0)
	{
		RestoreCoin(player);
		return;
	}
	if (player.applied_coin == 0) player.original_coin = *coin;
	player.applied_coin = wanted;
	if (*coin != wanted)
	{
		*coin = wanted;
		game::CoinChanged(controller);
	}
}

void FaceitLevels::RestoreCoin(Player &player)
{
	std::uint32_t *coin = player.controller && player.applied_coin ? game::Coin(player.controller) : nullptr;
	if (coin && *coin == player.applied_coin)
	{
		*coin = player.original_coin;
		game::CoinChanged(player.controller);
	}
	player.applied_coin = 0;
}

void FaceitLevels::StartRequest(std::uint64_t steam_id)
{
	ISteamHTTP *http = SteamGameServerHTTP();
	if (!http || config_.api_key.empty() || requests_.size() >= static_cast<std::size_t>(config_.max_concurrent_requests)
		|| std::any_of(requests_.begin(), requests_.end(), [&](const Request &r) { return !r.done && r.steam_id == steam_id; }))
		return;
	if (const auto retry = retry_at_.find(steam_id); retry != retry_at_.end() && Clock::now() < retry->second) return;

	const HTTPRequestHandle handle = http->CreateHTTPRequest(k_EHTTPMethodGET, faceit::PlayerUrl(steam_id).c_str());
	SteamAPICall_t call = k_uAPICallInvalid;
	const auto timeout = static_cast<uint32>(config_.request_timeout_seconds);
	if (handle == INVALID_HTTPREQUEST_HANDLE
		|| !http->SetHTTPRequestHeaderValue(handle, "Authorization", ("Bearer " + config_.api_key).c_str())
		|| !http->SetHTTPRequestHeaderValue(handle, "Accept", "application/json")
		|| !http->SetHTTPRequestRequiresVerifiedCertificate(handle, true)
		|| !http->SetHTTPRequestNetworkActivityTimeout(handle, timeout)
		|| !http->SetHTTPRequestAbsoluteTimeoutMS(handle, timeout * 1000)
		|| !http->SendHTTPRequest(handle, &call) || call == k_uAPICallInvalid)
	{
		if (handle != INVALID_HTTPREQUEST_HANDLE) http->ReleaseHTTPRequest(handle);
		retry_at_[steam_id] = Clock::now() + kRetryDelay;
		return;
	}
	Request &request = requests_.emplace_back();
	request.owner = this;
	request.steam_id = steam_id;
	request.handle = handle;
	request.call.SetGameserverFlag();
	request.call.Set(call, &request, &Request::Completed);
}

void FaceitLevels::Completed(Request &request, HTTPRequestCompleted_t *result, bool io_failure)
{
	request.done = true;
	ISteamHTTP *http = SteamGameServerHTTP();
	const int status = result && !io_failure && result->m_bRequestSuccessful ? static_cast<int>(result->m_eStatusCode) : 0;
	std::vector<char> body;
	if (http && status == 200 && result->m_unBodySize > 0 && result->m_unBodySize <= kMaxReplyBytes)
	{
		body.resize(result->m_unBodySize);
		if (!http->GetHTTPResponseBodyData(request.handle, reinterpret_cast<uint8 *>(body.data()), result->m_unBodySize))
			body.clear();
	}
	if (http) http->ReleaseHTTPRequest(request.handle);
	request.handle = INVALID_HTTPREQUEST_HANDLE;

	// 404: FACEIT has no CS2 account for this Steam ID, which is cached like a level.
	if (!body.empty() || status == 404)
	{
		const int level = body.empty() ? 0 : faceit::ParseSkillLevel(std::string_view(body.data(), body.size()));
		cache_[request.steam_id] = {level, UnixNow()};
		retry_at_.erase(request.steam_id);
		if (!cache_dirty_) next_save_ = Clock::now() + kSaveDelay;
		cache_dirty_ = true;
		return;
	}
	if ((status == 401 || status == 403) && !key_warning_shown_)
	{
		key_warning_shown_ = true;
		META_CONPRINTF("[FACEIT_LEVELS] FACEIT rejected the api_key in addons/faceit_levels/config.ini (HTTP %d).\n", status);
	}
	retry_at_[request.steam_id] = Clock::now()
		+ (status == 401 || status == 403 ? kKeyRejectedDelay : status == 429 ? kRateLimitDelay : kRetryDelay);
}

void FaceitLevels::ReadConfig()
{
	const std::string path = directory_ + "/config.ini";
	const std::string text = ReadFile(path);
	config_ = faceit::ParseConfig(text);
	key_warning_shown_ = false;
	if (text.empty()) META_CONPRINTF("[FACEIT_LEVELS] Could not read %s.\n", path.c_str());
	else if (config_.api_key.empty()) META_CONPRINTF("[FACEIT_LEVELS] Set api_key in %s to show FACEIT levels.\n", path.c_str());
}

void FaceitLevels::SaveCache()
{
	const std::uint64_t now = UnixNow();
	std::erase_if(cache_, [&](const auto &entry) { return now - entry.second.fetched_at > kCacheKeepSeconds; });
	const std::string path = directory_ + "/cache.tsv";
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file << faceit::FormatCache(cache_);
	if (file.flush()) cache_dirty_ = false;
	else META_CONPRINTF("[FACEIT_LEVELS] Could not save %s.\n", path.c_str());
	next_save_ = Clock::now() + kSaveDelay;
}
