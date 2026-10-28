#include "faceit.h"

#include <array>
#include <cctype>
#include <charconv>
#include <optional>

namespace faceit
{
namespace
{

std::string_view Trim(std::string_view text)
{
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
	return text;
}

template <typename T>
std::optional<T> Number(std::string_view text)
{
	T value {};
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
	return value;
}

// A minimal JSON walker: enough to step over any value and to find an object's direct members.
class Json
{
public:
	explicit Json(std::string_view text) : text_(text) {}

	// The value of `key` among the direct members of the object starting at `at`, or nullopt.
	std::optional<std::size_t> Member(std::size_t at, std::string_view key) const
	{
		at = Skip(at);
		if (at >= text_.size() || text_[at] != '{') return std::nullopt;
		at = Skip(at + 1);
		while (at < text_.size() && text_[at] == '"')
		{
			const std::size_t name_end = StringEnd(at);
			if (name_end == npos) return std::nullopt;
			const std::string_view name = text_.substr(at + 1, name_end - at - 2);
			at = Skip(name_end);
			if (at >= text_.size() || text_[at] != ':') return std::nullopt;
			const std::size_t value = Skip(at + 1);
			if (name == key) return value;
			at = Skip(ValueEnd(value));
			if (at >= text_.size() || text_[at] != ',') return std::nullopt;
			at = Skip(at + 1);
		}
		return std::nullopt;
	}

	// The digits of a plain integer value at `at`.
	std::string_view Integer(std::size_t at) const
	{
		std::size_t end = at;
		while (end < text_.size() && std::isdigit(static_cast<unsigned char>(text_[end]))) ++end;
		return text_.substr(at, end - at);
	}

private:
	static constexpr std::size_t npos = std::string_view::npos;

	std::size_t Skip(std::size_t at) const
	{
		while (at < text_.size() && std::isspace(static_cast<unsigned char>(text_[at]))) ++at;
		return at;
	}

	// One past the closing quote of the string starting at `at`.
	std::size_t StringEnd(std::size_t at) const
	{
		for (++at; at < text_.size(); ++at)
		{
			if (text_[at] == '\\') ++at;
			else if (text_[at] == '"') return at + 1;
		}
		return npos;
	}

	// One past the end of any value starting at `at`; npos when the text ends first.
	std::size_t ValueEnd(std::size_t at) const
	{
		if (at >= text_.size()) return npos;
		if (text_[at] == '"') return StringEnd(at);
		if (text_[at] != '{' && text_[at] != '[')
		{
			while (at < text_.size() && text_[at] != ',' && text_[at] != '}' && text_[at] != ']') ++at;
			return at;
		}
		int depth = 0;
		for (; at < text_.size(); ++at)
		{
			const char c = text_[at];
			if (c == '"') { at = StringEnd(at); if (at == npos) return npos; --at; }
			else if (c == '{' || c == '[') ++depth;
			else if ((c == '}' || c == ']') && --depth == 0) return at + 1;
		}
		return npos;
	}

	std::string_view text_;
};

} // namespace

Config ParseConfig(std::string_view text)
{
	Config config;
	while (!text.empty())
	{
		const std::size_t line_end = text.find('\n');
		std::string_view line = Trim(text.substr(0, line_end));
		text.remove_prefix(line_end == std::string_view::npos ? text.size() : line_end + 1);
		const std::size_t equals = line.find('=');
		if (line.empty() || line[0] == '#' || line[0] == ';' || equals == std::string_view::npos) continue;
		const std::string_view key = Trim(line.substr(0, equals)), value = Trim(line.substr(equals + 1));
		const auto ranged = [&](int &field, int low, int high) {
			if (const auto parsed = Number<int>(value); parsed && *parsed >= low && *parsed <= high) field = *parsed;
		};
		if (key == "api_key") config.api_key = value;
		else if (key == "refresh_minutes") ranged(config.refresh_minutes, 1, 1440);
		else if (key == "request_timeout_seconds") ranged(config.request_timeout_seconds, 1, 60);
		else if (key == "max_concurrent_requests") ranged(config.max_concurrent_requests, 1, 16);
	}
	return config;
}

int ParseSkillLevel(std::string_view text)
{
	const Json json(text);
	const auto games = json.Member(0, "games");
	const auto cs2 = games ? json.Member(*games, "cs2") : std::nullopt;
	const auto level = cs2 ? json.Member(*cs2, "skill_level") : std::nullopt;
	const auto value = level ? Number<int>(json.Integer(*level)) : std::nullopt;
	return value && *value >= 1 && *value <= 10 ? *value : 0;
}

std::string PlayerUrl(std::uint64_t steam_id)
{
	return "https://open.faceit.com/data/v4/players?game=cs2&game_player_id=" + std::to_string(steam_id);
}

Cache ParseCache(std::string_view text)
{
	Cache cache;
	while (!text.empty())
	{
		const std::size_t line_end = text.find('\n');
		std::string_view line = Trim(text.substr(0, line_end));
		text.remove_prefix(line_end == std::string_view::npos ? text.size() : line_end + 1);
		if (line.empty() || line[0] == '#') continue;
		std::array<std::string_view, 3> fields;
		std::size_t count = 0;
		while (!line.empty() && count < fields.size())
		{
			const std::size_t space = line.find_first_of(" \t");
			fields[count++] = line.substr(0, space);
			line = Trim(space == std::string_view::npos ? std::string_view() : line.substr(space));
		}
		const auto steam_id = Number<std::uint64_t>(fields[0]);
		const auto level = Number<int>(fields[1]);
		const auto fetched_at = Number<std::uint64_t>(fields[2]);
		if (count == 3 && line.empty() && steam_id && *steam_id && level && *level >= 0 && *level <= 10 && fetched_at && *fetched_at)
			cache[*steam_id] = {*level, *fetched_at};
	}
	return cache;
}

std::string FormatCache(const Cache &cache)
{
	std::string text = "# steamid64 level fetched_at_unix\n";
	for (const auto &[steam_id, entry] : cache)
		if (entry.fetched_at)
			text += std::to_string(steam_id) + ' ' + std::to_string(entry.level) + ' ' + std::to_string(entry.fetched_at) + '\n';
	return text;
}

std::uint32_t LevelCoin(int level)
{
	// Coin item ids of the FACEIT level 1-10 images (mapping from Pisex/cs2-faceit-level).
	constexpr std::array<std::uint32_t, 11> coins = {0, 1088, 1087, 1032, 1055, 1041, 1074, 1039, 1067, 1061, 1017};
	return level >= 1 && level <= 10 ? coins[level] : 0;
}

} // namespace faceit
