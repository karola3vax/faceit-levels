// Build and run: g++ -std=c++20 -Isrc tests/faceit_test.cpp src/faceit.cpp -o faceit_test && ./faceit_test
// (or cl /std:c++20 /EHsc /Isrc tests\faceit_test.cpp src\faceit.cpp)
#include "faceit.h"

#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { std::printf("FAILED line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

int main()
{
	using namespace faceit;

	// The cs2 level, not csgo's; keys are matched as members, not as text anywhere.
	CHECK(ParseSkillLevel(R"({"games":{"csgo":{"skill_level":2},"cs2":{"skill_level":10}}})") == 10);
	CHECK(ParseSkillLevel(R"({ "nickname":"a\"games\"", "games" : { "cs2" : { "region":"EU", "skill_level" : 1 } } })") == 1);
	CHECK(ParseSkillLevel(R"({"games":{"csgo":{"skill_level":9}}})") == 0);
	CHECK(ParseSkillLevel(R"({"games":{"cs2":{"skill_level":11}}})") == 0);
	CHECK(ParseSkillLevel(R"({"note":"escaped } brace","games":{"cs2":{"skill_level":7}}})") == 7);
	CHECK(ParseSkillLevel(R"({"x":[{"games":{"cs2":{"skill_level":5}}}],"games":{"cs2":{"skill_level":4}}})") == 4);
	CHECK(ParseSkillLevel(R"({"games":{"cs2":{"faceit_elo":2001,"skill_level":"8"}}})") == 0);
	CHECK(ParseSkillLevel(R"({"games":{"cs2":{"skill_level":8})") == 8); // truncated after the value: still read
	CHECK(ParseSkillLevel(R"({"games":{"cs2":{"skill)") == 0);
	CHECK(ParseSkillLevel("") == 0);

	const Config config = ParseConfig("# c\n api_key = abc \r\nrefresh_minutes=5\nrequest_timeout_seconds=99\nmax_concurrent_requests=x\n");
	CHECK(config.api_key == "abc");
	CHECK(config.refresh_minutes == 5);
	CHECK(config.request_timeout_seconds == 10);
	CHECK(config.max_concurrent_requests == 4);

	const Cache cache = ParseCache("# steamid64 level fetched_at_unix\n76561198000000001 10 1700000000\n76561198000000002 0 1700000001\nbad line\n76561198000000003 11 1\n76561198000000004 5 1 extra\n");
	CHECK(cache.size() == 2);
	CHECK(cache.at(76561198000000001).level == 10);
	CHECK(cache.at(76561198000000002).level == 0);
	CHECK(ParseCache(FormatCache(cache)).size() == 2);

	CHECK(LevelCoin(0) == 0 && LevelCoin(1) == 1088 && LevelCoin(5) == 1041 && LevelCoin(10) == 1017 && LevelCoin(11) == 0);
	CHECK(PlayerUrl(76561198000000001) == "https://open.faceit.com/data/v4/players?game=cs2&game_player_id=76561198000000001");
	std::printf("faceit tests passed\n");
}
