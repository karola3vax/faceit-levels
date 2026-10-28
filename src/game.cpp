#include "game.h"

#include <cstdio>
#include <cstring>

#include <entity2/entityidentity.h>
#include <entity2/entityinstance.h>
#include <gameentitysystem.h>
#include <schemalib/schemaclassinfo.h>
#include <schemasystem/schemasystem.h>

namespace game
{
namespace
{

// Byte offsets of the fields, from the schema.
std::ptrdiff_t steam_id_offset = -1;  // CBasePlayerController::m_steamID
std::ptrdiff_t inventory_offset = -1; // CCSPlayerController::m_pInventoryServices
std::ptrdiff_t rank_offset = -1;      // CCSPlayerController_InventoryServices::m_rank

// m_rank is MedalRank_t[6]; the scoreboard shows the last entry as the player's coin.
constexpr int kCoinRank = 5;

std::ptrdiff_t FieldOffset(CSchemaSystemTypeScope *scope, const char *class_name, const char *field)
{
	const CSchemaClassInfo *info = scope ? scope->FindDeclaredClass(class_name).Get() : nullptr;
	for (int i = 0; info && i < info->m_nNumFields; ++i)
		if (info->m_pFields[i].m_pName && std::strcmp(info->m_pFields[i].m_pName, field) == 0)
			return info->m_pFields[i].m_nSingleInheritanceOffset;
	return -1;
}

template <typename T>
T &Field(void *object, std::ptrdiff_t offset)
{
	return *reinterpret_cast<T *>(static_cast<std::byte *>(object) + offset);
}

} // namespace

bool Init(ISchemaSystem *schema, char *error, std::size_t error_size)
{
#ifdef _WIN32
	CSchemaSystemTypeScope *scope = schema->FindTypeScopeForModule("server.dll");
#else
	CSchemaSystemTypeScope *scope = schema->FindTypeScopeForModule("libserver.so");
#endif
	steam_id_offset = FieldOffset(scope, "CBasePlayerController", "m_steamID");
	inventory_offset = FieldOffset(scope, "CCSPlayerController", "m_pInventoryServices");
	rank_offset = FieldOffset(scope, "CCSPlayerController_InventoryServices", "m_rank");
	if (steam_id_offset >= 0 && inventory_offset >= 0 && rank_offset >= 0) return true;
	std::snprintf(error, error_size, "CS2's schema is missing a player field (m_steamID %d, m_pInventoryServices %d, m_rank %d)",
		static_cast<int>(steam_id_offset), static_cast<int>(inventory_offset), static_cast<int>(rank_offset));
	return false;
}

CEntityInstance *Controller(int slot)
{
	CGameEntitySystem *entities = GameEntitySystem();
	CEntityInstance *controller = entities ? entities->GetPlayerController(CPlayerSlot(slot)) : nullptr;
	return controller && std::strcmp(controller->GetClassname(), "cs_player_controller") == 0 ? controller : nullptr;
}

std::uint64_t SteamId(CEntityInstance *controller)
{
	return Field<std::uint64_t>(controller, steam_id_offset);
}

std::uint32_t *Coin(CEntityInstance *controller)
{
	void *inventory = Field<void *>(controller, inventory_offset);
	return inventory ? &Field<std::uint32_t>(inventory, rank_offset) + kCoinRank : nullptr;
}

void CoinChanged(CEntityInstance *controller)
{
	controller->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(inventory_offset)));
}

} // namespace game
