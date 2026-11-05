#include "surf_spec.h"
#include "../timer/surf_timer.h"
#include "surf/language/surf_language.h"
#include "utils/simplecmds.h"
#include "utils/ctimer.h"

static_global class SurfTimerServiceEventListener_Spec : public SurfTimerServiceEventListener
{
	virtual void OnTimerStartPost(SurfPlayer *player, u32 courseGUID) override;
} timerEventListener;

void SurfSpecService::Reset()
{
	this->ResetSavedPosition();
}

void SurfSpecService::Init()
{
	SurfTimerService::RegisterEventListener(&timerEventListener);
}

bool SurfSpecService::HasSavedPosition()
{
	return this->savedPosition;
}

void SurfSpecService::SavePosition()
{
	this->player->GetOrigin(&this->savedOrigin);
	this->player->GetAngles(&this->savedAngles);
	this->savedOnLadder = this->player->GetMoveType() == MOVETYPE_LADDER;
	this->savedPosition = true;
}

void SurfSpecService::LoadPosition()
{
	if (!this->HasSavedPosition())
	{
		return;
	}
	this->player->Teleport(&this->savedOrigin, &this->savedAngles, nullptr);
	if (this->savedOnLadder)
	{
		this->player->SetMoveType(MOVETYPE_LADDER);
	}
}

void SurfSpecService::ResetSavedPosition()
{
	this->savedOrigin = vec3_origin;
	this->savedAngles = vec3_angle;
	this->savedOnLadder = false;
	this->savedPosition = false;
}

bool SurfSpecService::IsSpectating(SurfPlayer *target)
{
	return this->GetSpectatedPlayer() == target;
}

bool SurfSpecService::SpectatePlayer(const char *playerName)
{
	CCSPlayerController *controller = this->player->GetController();
	SurfPlayer *targetPlayer = nullptr;
	if (SURF_STREQI(playerName, "@me"))
	{
		if (!this->player->IsAlive())
		{
			this->player->languageService->PrintChat(true, false, "Spectate Failure (Dead)");
			return false;
		}
		targetPlayer = this->player;
	}
	else
	{
		// Prefer exact matches over partial matches.
		for (i32 i = 0; i <= MAX_PLAYERS; i++)
		{
			CBasePlayerController *controller = g_pSurfPlayerManager->players[i]->GetController();
			SurfPlayer *otherPlayer = g_pSurfPlayerManager->ToPlayer(i);

			if (!controller || this->player == otherPlayer)
			{
				continue;
			}

			if (SURF_STREQI(otherPlayer->GetName(), playerName))
			{
				if (otherPlayer->GetController()->GetTeam() == CS_TEAM_SPECTATOR)
				{
					continue;
				}
				targetPlayer = otherPlayer;
				break;
			}
		}
		// If no exact match was found, try partial matches.
		if (!targetPlayer)
		{
			for (i32 i = 0; i <= MAX_PLAYERS; i++)
			{
				CBasePlayerController *controller = g_pSurfPlayerManager->players[i]->GetController();
				SurfPlayer *otherPlayer = g_pSurfPlayerManager->ToPlayer(i);

				if (!controller || this->player == otherPlayer)
				{
					continue;
				}

				if (V_strstr(V_strlower((char *)otherPlayer->GetName()), V_strlower((char *)playerName)))
				{
					if (otherPlayer->GetController()->GetTeam() == CS_TEAM_SPECTATOR)
					{
						player->languageService->PrintChat(true, false, "Spectate Failure (Dead)");
						return true;
					}
					targetPlayer = otherPlayer;
					break;
				}
			}
		}
	}

	if (!targetPlayer)
	{
		player->languageService->PrintChat(true, false, "Spectate Failure (Player Not Found)", playerName);
		return true;
	}

	this->SpectatePlayer(targetPlayer);
	return true;
}

static_function f64 TeleportObserver(CPlayerUserId userID, Vector origin, QAngle angles)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);
	if (player && player->GetObserverPawn())
	{
		player->GetObserverPawn()->Teleport(&origin, &angles, nullptr);
	}
	return 0.0f;
};

bool SurfSpecService::SpectatePlayer(SurfPlayer *target)
{
	if (!target || !this->CanSpectate())
	{
		return false;
	}
	// Join spectator team if not already in it.
	CCSPlayerController *controller = this->player->GetController();
	if (controller->GetTeam() != CS_TEAM_SPECTATOR)
	{
		Surf::misc::JoinTeam(this->player, CS_TEAM_SPECTATOR, true);
	}

	CPlayer_ObserverServices *obsService = player->GetController()->m_hObserverPawn()->m_pObserverServices;
	if (!obsService)
	{
		player->languageService->PrintChat(true, false, "Spectate Failure (Generic)");
		return false;
	}

	obsService->m_iObserverMode(OBS_MODE_IN_EYE);
	obsService->m_iObserverLastMode(OBS_MODE_NONE);
	obsService->m_hObserverTarget(target->GetPlayerPawn());
	controller->m_DesiredObserverMode(OBS_MODE_IN_EYE);
	controller->m_hDesiredObserverTarget(target->GetPlayerPawn());

	if (target == this->player)
	{
		controller->m_DesiredObserverMode(OBS_MODE_ROAMING);
		obsService->m_iObserverMode(OBS_MODE_ROAMING);
		obsService->m_hObserverTarget(target->GetPlayerPawn());
		Vector origin;
		QAngle angles;
		this->player->GetEyeOrigin(&origin);
		this->player->GetAngles(&angles);
		StartTimer<CPlayerUserId, Vector, QAngle>(TeleportObserver, player->GetClient()->GetUserID(), std::move(origin), std::move(angles), 0.0f,
												  false, false);
	}
	return true;
}

bool SurfSpecService::CanSpectate()
{
	return !this->player->IsAlive() || this->player->timerService->GetPaused() || this->player->timerService->CanPause();
}

void SurfSpecService::GetSpectatorList(CUtlVector<std::string> &spectatorList)
{
	SurfPlayer *spectator = this->player->specService->GetNextSpectator(nullptr);
	while (spectator)
	{
		spectatorList.AddToTail(spectator->GetName());
		spectator = this->player->specService->GetNextSpectator(spectator);
	}
}

SurfPlayer *SurfSpecService::GetSpectatedPlayer()
{
	if (!player || player->IsAlive())
	{
		return NULL;
	}
	if (!player->GetController() || !player->GetController()->m_hObserverPawn())
	{
		return NULL;
	}
	CPlayer_ObserverServices *obsService = player->GetController()->m_hObserverPawn()->m_pObserverServices;
	if (!obsService)
	{
		return NULL;
	}
	if (!obsService->m_hObserverTarget().IsValid())
	{
		return NULL;
	}
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	CBasePlayerPawn *target = (CBasePlayerPawn *)obsService->m_hObserverTarget().Get();
	// If the player is spectating their own corpse, consider that as not spectating anyone.
	return target == pawn ? nullptr : g_pSurfPlayerManager->ToPlayer(target);
}

SurfPlayer *SurfSpecService::GetNextSpectator(SurfPlayer *current)
{
	for (int i = current ? current->index + 1 : 0; i < MAX_PLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		if (player->specService->IsSpectating(this->player))
		{
			return player;
		}
	}
	return nullptr;
}

void SurfTimerServiceEventListener_Spec::OnTimerStartPost(SurfPlayer *player, u32 courseGUID)
{
	player->specService->Reset();
}

SCMD(surf_spec, SCFL_SPEC)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!player->specService->CanSpectate())
	{
		player->languageService->PrintChat(true, false, "Spectate Failure (Generic)");
		return true;
	}
	// Count alive players and find first alive player
	u32 numAlivePlayers = 0;
	SurfPlayer *firstAlivePlayer = nullptr;
	for (i32 i = 0; i < MAX_PLAYERS + 1; i++)
	{
		SurfPlayer *otherPlayer = g_pSurfPlayerManager->ToPlayer(i);
		if (otherPlayer && otherPlayer->IsAlive() && otherPlayer != player)
		{
			numAlivePlayers++;
			if (!firstAlivePlayer)
			{
				firstAlivePlayer = otherPlayer;
			}
		}
	}

	// Handle automatic spectating
	if (numAlivePlayers == 0 && args->ArgC() == 1)
	{
		player->specService->SpectatePlayer("@me");
		return true;
	}
	if (numAlivePlayers == 1)
	{
		player->specService->SpectatePlayer(firstAlivePlayer);
		return true;
	}

	// If no target is provided, default to the first alive player
	if (args->ArgC() < 2)
	{
		player->specService->SpectatePlayer(firstAlivePlayer);
		return true;
	}

	// Handle explicit target
	player->specService->SpectatePlayer(args->Arg(1));
	return true;
}

SCMD(surf_specs, SCFL_SPEC)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	SurfPlayer *targetPlayer = player->IsAlive() ? player : player->specService->GetSpectatedPlayer();

	if (!targetPlayer)
	{
		player->languageService->PrintChat(true, false, "Spectator List (None)");
		return true;
	}
	CUtlVector<std::string> spectatorList;
	targetPlayer->specService->GetSpectatorList(spectatorList);
	if (spectatorList.Count() == 0)
	{
		if (targetPlayer == player)
		{
			player->languageService->PrintChat(true, false, "Spectator List (None)");
		}
		else
		{
			player->languageService->PrintChat(true, false, "Target Spectator List (None)", targetPlayer->GetName());
		}
	}
	else
	{
		std::string spectatorListString;
		for (i32 i = 0; i < spectatorList.Count(); i++)
		{
			spectatorListString += spectatorList[i];
			if (i != spectatorList.Count() - 1)
			{
				spectatorListString += ", ";
			}
		}
		if (targetPlayer == player)
		{
			player->languageService->PrintChat(true, false, "Spectator List", spectatorList.Count(), spectatorListString.c_str());
		}
		else
		{
			player->languageService->PrintChat(true, false, "Target Spectator List", targetPlayer->GetName(), spectatorList.Count(),
											   spectatorListString.c_str());
		}
	}
	return true;
}

SCMD_LINK(surf_speclist, surf_specs);
