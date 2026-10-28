#pragma once

// Game-independent parts: config, the FACEIT reply, the level cache and the scoreboard
// coins. No SDK headers, so tests/faceit_test.cpp builds them on its own.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace faceit
{

struct Config
{
	std::string api_key;
	int refresh_minutes = 60;
	int request_timeout_seconds = 10;
	int max_concurrent_requests = 4;
};

// key=value lines; '#' and ';' start comments. Out-of-range numbers keep the default.
Config ParseConfig(std::string_view text);

// The cs2 skill level (1-10) from a /data/v4/players reply, or 0 when the player has none.
int ParseSkillLevel(std::string_view json);

std::string PlayerUrl(std::uint64_t steam_id);

struct CacheEntry
{
	int level = 0;               // 0: no FACEIT CS2 account
	std::uint64_t fetched_at = 0; // unix seconds
};
using Cache = std::unordered_map<std::uint64_t, CacheEntry>;

// "steamid64 level fetched_at_unix" per line (the 1.x cache file format).
Cache ParseCache(std::string_view text);
std::string FormatCache(const Cache &cache);

// The scoreboard coin (an item definition index from the server_assets Workshop addon)
// for a level, or 0 for none.
std::uint32_t LevelCoin(int level);

} // namespace faceit
