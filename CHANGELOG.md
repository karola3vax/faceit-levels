# Changelog

## 2.0.0

- Rewritten from scratch.
- Built against the latest Metamod:Source 2.0.0 (build 1483, KHook) and s2sdk; loads on Metamod 2.0.0 build 1469 and newer.
- No offsets to maintain: the entity system comes from the SDK and player fields from CS2's schema. The `game_entity_system_offset_*` settings are gone (old config files still work).
- Steam IDs are read from the player controller instead of the engine, so engine interface changes in CS2 updates cannot break it.
- A rejected API key is reported once instead of on every request.
- The cache drops levels fetched more than a week ago.
- The config is read again on every map change.
