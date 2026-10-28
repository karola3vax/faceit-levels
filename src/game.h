#pragma once

// Reading and writing the scoreboard coin on player controllers. Field offsets come from
// CS2's own schema system at load, so nothing here needs updating after a CS2 update.

#include <cstddef>
#include <cstdint>

class CEntityInstance;
class ISchemaSystem;

namespace game
{

// Resolves the schema fields; false (and a message) when CS2 no longer has one of them.
bool Init(ISchemaSystem *schema, char *error, std::size_t error_size);

// The player controller in a slot (0-63), or nullptr.
CEntityInstance *Controller(int slot);

// 0 for bots and players without a Steam ID.
std::uint64_t SteamId(CEntityInstance *controller);

// The displayed scoreboard coin, or nullptr while the controller has no inventory.
std::uint32_t *Coin(CEntityInstance *controller);

// Sends a changed coin to the players.
void CoinChanged(CEntityInstance *controller);

} // namespace game
