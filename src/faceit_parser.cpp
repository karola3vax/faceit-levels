#include "faceit_parser.h"

#include <charconv>
#include <cctype>
#include <string>

namespace
{
struct Range
{
    std::size_t begin;
    std::size_t end;
};

std::size_t SkipWhitespace(std::string_view text, std::size_t pos, std::size_t end)
{
    while (pos < end && std::isspace(static_cast<unsigned char>(text[pos])))
        ++pos;
    return pos;
}

std::size_t FindMatchingObjectEnd(std::string_view text, std::size_t open, std::size_t limit)
{
    int depth = 0;
    bool quoted = false;
    bool escaped = false;

    for (std::size_t i = open; i < limit; ++i)
    {
        const char ch = text[i];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                quoted = false;
            continue;
        }

        if (ch == '"')
            quoted = true;
        else if (ch == '{')
            ++depth;
        else if (ch == '}' && --depth == 0)
            return i + 1;
    }

    return std::string_view::npos;
}

Range FindObject(std::string_view text, std::string_view key, Range within)
{
    const std::string quotedKey = "\"" + std::string(key) + "\"";
    std::size_t pos = within.begin;

    while ((pos = text.find(quotedKey, pos)) != std::string_view::npos && pos < within.end)
    {
        std::size_t value = SkipWhitespace(text, pos + quotedKey.size(), within.end);
        if (value >= within.end || text[value] != ':')
        {
            pos += quotedKey.size();
            continue;
        }

        value = SkipWhitespace(text, value + 1, within.end);
        if (value < within.end && text[value] == '{')
        {
            const std::size_t end = FindMatchingObjectEnd(text, value, within.end);
            if (end != std::string_view::npos)
                return {value + 1, end - 1};
        }

        pos += quotedKey.size();
    }

    return {std::string_view::npos, std::string_view::npos};
}
}

int ParseFaceitLevel(std::string_view json)
{
    const Range whole{0, json.size()};
    const Range games = FindObject(json, "games", whole);
    if (games.begin == std::string_view::npos)
        return 0;

    const Range cs2 = FindObject(json, "cs2", games);
    if (cs2.begin == std::string_view::npos)
        return 0;

    constexpr std::string_view key = "\"skill_level\"";
    const std::size_t keyPos = json.find(key, cs2.begin);
    if (keyPos == std::string_view::npos || keyPos >= cs2.end)
        return 0;

    std::size_t value = SkipWhitespace(json, keyPos + key.size(), cs2.end);
    if (value >= cs2.end || json[value] != ':')
        return 0;

    value = SkipWhitespace(json, value + 1, cs2.end);
    int level = 0;
    const char* first = json.data() + value;
    const char* last = json.data() + cs2.end;
    const auto parsed = std::from_chars(first, last, level);
    return parsed.ec == std::errc() && level >= 1 && level <= 10 ? level : 0;
}
