# Faceit Levels

![FACEIT Level 10 displayed on the Counter-Strike 2 scoreboard](assets/scoreboard.png)

A Metamod:Source plugin for Counter-Strike 2 that shows every player's FACEIT level on the scoreboard, for everyone. It runs on its own: no commands, no CounterStrikeSharp or SwiftlyS2.

## Features

- FACEIT levels 1 to 10 on the scoreboard, as the player's coin
- Players without a FACEIT CS2 account keep their own coin
- Each level is looked up once and refreshed every hour (configurable), with a small cache that survives restarts
- Windows and Linux
- No offsets to update after CS2 updates: player fields are read from CS2's own schema at load

## Requirements

- Counter-Strike 2 dedicated server
- Metamod:Source 2.0.0 build 1469 or newer
- A FACEIT Data API key (server-side) from [developers.faceit.com](https://developers.faceit.com/)
- The FACEIT level images on the clients: the `server_assets` Workshop addon (or Workshop item 3724637448) through MultiAddonManager

## Installation

1. Extract the release into `game/csgo/`.
2. Open `game/csgo/addons/faceit_levels/config.ini` and set `api_key`.
3. Add the Workshop addon with the level images to MultiAddonManager (`mm_extra_addons`).
4. Restart the server.

The release contains:

```text
addons/
|-- faceit_levels/
|   |-- bin/faceit_levels.dll
|   |-- bin/faceit_levels.so
|   `-- config.ini
`-- metamod/faceit_levels.vdf
```

## Configuration

`addons/faceit_levels/config.ini` is read at server start and on every map change.

| Setting | Default | Meaning |
| --- | --- | --- |
| `api_key` | (empty) | Your FACEIT Data API key. Nothing is looked up without it. |
| `refresh_minutes` | `60` | How often a player's level is fetched again (1-1440). |
| `request_timeout_seconds` | `10` | How long one request may take (1-60). |
| `max_concurrent_requests` | `4` | How many requests may run at once (1-16). |

Levels are cached in `addons/faceit_levels/cache.tsv`. The file can be deleted at any time.

## Building

Dependencies are pinned in `build-dependencies.json` (Metamod:Source, s2sdk, hl2sdk-manifests, AMBuild) and fetched by the build scripts.

- Windows (Visual Studio 2022 C++ build tools, Python 3): `.\scripts\build-windows.ps1`
- Linux (Docker; builds in Valve's SteamRT3 "sniper" SDK image): `.\scripts\build-steamrt3.ps1`, or `scripts/build-linux.sh` on a Linux machine with gcc, git and Python 3

The release files are staged in `dist/`. Tests for the config, reply and cache parsing:

```bash
g++ -std=c++20 -Isrc tests/faceit_test.cpp src/faceit.cpp -o faceit_test && ./faceit_test
```

## Credits

- [Pisex/cs2-faceit-level](https://github.com/Pisex/cs2-faceit-level) for the scoreboard coin mapping and the idea
- [zhw1nq/swiftlyS2-faceit-scoreboard](https://github.com/zhw1nq/swiftlyS2-faceit-scoreboard) for the CS2 FACEIT lookup reference

FACEIT is a trademark of its respective owner. This project is not affiliated with or endorsed by FACEIT.

## License

Copyright (C) 2026 karola3vax. Licensed under the GNU Affero General Public License v3.0 only. See [LICENSE](LICENSE).
