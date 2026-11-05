#include "player.h"
#include "networksystem/inetworkmessages.h"
#include "gameevents.pb.h"
#include "sdk/recipientfilters.h"

#include "steam/steam_gameserver.h"

CCSPlayerController *Player::GetController()
{
	if (!GameEntitySystem())
	{
		return nullptr;
	}
	CBaseEntity *ent = static_cast<CBaseEntity *>(GameEntitySystem()->GetEntityInstance(CEntityIndex(this->index)));
	if (!ent)
	{
		return nullptr;
	}
	return ent->IsController() ? static_cast<CCSPlayerController *>(ent) : nullptr;
}

void Player::Kick(const char *internalReason, ENetworkDisconnectionReason reason)
{
	interfaces::pEngine->KickClient(this->GetPlayerSlot(), internalReason, reason);
}

void Player::OnAuthorized()
{
	auto steamID = this->GetClient()->GetClientSteamID();
	this->hasPrime = SteamGameServer() && SteamGameServer()->UserHasLicenseForApp(steamID, 624820) == k_EUserHasLicenseResultHasLicense;
}

void Player::SetName(const char *name)
{
	if (this->GetClient())
	{
		this->GetClient()->m_pUserInfo->SetString("name", name);
		this->GetClient()->UpdateUserSettings();
		this->GetClient()->SetName(name);
	}
}

void Player::SetClan(const char *clan)
{
	if (this->GetController())
	{
		this->GetController()->SetClan(clan);
	}
}
