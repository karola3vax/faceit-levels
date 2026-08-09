#include "faceit_parser.h"

#include <cassert>

int main()
{
    assert(ParseFaceitLevel(R"({"games":{"csgo":{"skill_level":2},"cs2":{"skill_level":10}}})") == 10);
    assert(ParseFaceitLevel(R"({"games":{"cs2":{"region":"EU","skill_level":1}}})") == 1);
    assert(ParseFaceitLevel(R"({"games":{"csgo":{"skill_level":9}}})") == 0);
    assert(ParseFaceitLevel(R"({"games":{"cs2":{"skill_level":11}}})") == 0);
    assert(ParseFaceitLevel(R"({"note":"escaped } brace","games":{"cs2":{"skill_level":7}}})") == 7);
    assert(ParseFaceitLevel(R"({"games":{"cs2":{"skill_level":8})") == 0);
}
