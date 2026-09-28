#include "cstrike15_usermessages.pb.h"
#include "usermessages.pb.h"
#include "gameevents.pb.h"
#include "cs_gameevents.pb.h"

#include "sdk/entity/cparticlesystem.h"
#include "sdk/entity/ccscustomplayercamera.h"
#include "sdk/services.h"

#include "surf_quiet.h"
#include "surf/beam/surf_beam.h"
#include "surf/option/surf_option.h"
#include "surf/language/surf_language.h"

#include "utils/utils.h"
#include "utils/simplecmds.h"
#include "utils/ctimer.h"

static_global class SurfOptionServiceEventListener_Quiet : public SurfOptionServiceEventListener
{
	virtual void OnPlayerPreferenceLoaded(SurfPlayer *player)
	{
		player->quietService->ApplyPreferences();
	}

	virtual void OnPlayerPreferenceChanged(SurfPlayer *player, const char *optionName)
	{
		if (SURF_STREQI(optionName, "hideWeapon") || SURF_STREQI(optionName, "hideOtherPlayers"))
		{
			player->quietService->ApplyPreferences();
		}
	}
} optionEventListener;

void Surf::quiet::OnCheckTransmit(CCheckTransmitInfo **pInfo, int infoCount)
{
	for (int i = 0; i < infoCount; i++)
	{
		// Cast it to our own TransmitInfo struct because CCheckTransmitInfo isn't correct.
		TransmitInfo *pTransmitInfo = reinterpret_cast<TransmitInfo *>(pInfo[i]);

		// Find out who this info will be sent to.
		uintptr_t targetAddr = reinterpret_cast<uintptr_t>(pTransmitInfo) + g_pGameConfig->GetOffset("QuietPlayerSlot");
		CPlayerSlot targetSlot = CPlayerSlot(*reinterpret_cast<int *>(targetAddr));
		SurfPlayer *targetPlayer = g_pSurfPlayerManager->ToPlayer(targetSlot);
		// Make sure the target isn't CSTV.
		CCSPlayerController *targetController = targetPlayer->GetController();
		if (!targetController || targetController->m_bIsHLTV)
		{
			continue;
		}
		targetPlayer->quietService->UpdateHideState();

		EntityInstanceByClassIter_t iterParticleSystem(NULL, "info_particle_system");

		for (CParticleSystem *particleSystem = static_cast<CParticleSystem *>(iterParticleSystem.First()); particleSystem;
			 particleSystem = static_cast<CParticleSystem *>(iterParticleSystem.Next()))
		{
			if (!targetPlayer->quietService->ShouldHide())
			{
				continue; // Only hide player beam if the target enables !hide
			}
			if (particleSystem->m_iTeamNum() != CUSTOM_PARTICLE_SYSTEM_TEAM)
			{
				continue; // Only hide custom particle systems created by the plugin.
			}
			if (targetPlayer->beamService->playerBeam == particleSystem->GetRefEHandle()
				|| targetPlayer->beamService->playerBeamNew == particleSystem->GetRefEHandle())
			{
				// Don't hide the beam for the owner.
				continue;
			}
			pTransmitInfo->m_pTransmitEdict->Clear(particleSystem->GetEntityIndex().Get());
		}

		EntityInstanceByClassIter_t iter(NULL, "player");
		// clang-format off
		for (CCSPlayerPawn *pawn = static_cast<CCSPlayerPawn *>(iter.First());
			 pawn != NULL;
			 pawn = pawn->m_pEntity->m_pNextByClass ? static_cast<CCSPlayerPawn *>(pawn->m_pEntity->m_pNextByClass->m_pInstance) : nullptr)
		// clang-format on
		{
			// Bit is not even set, don't bother.
			if (!pTransmitInfo->m_pTransmitEdict->IsBitSet(pawn->entindex()))
			{
				continue;
			}
			// Do not transmit a pawn without any controller to prevent crashes.
			if (!pawn->m_hController().IsValid())
			{
				pTransmitInfo->m_pTransmitEdict->Clear(pawn->entindex());
				continue;
			}
			// Respawn must be enabled or !hide will cause client crash.
#if 0
			// Never send dead players to prevent crashes.
			if (pawn->m_lifeState() != LIFE_ALIVE)
			{
				pTransmitInfo->m_pTransmitEdict->Clear(pawn->entindex());
				continue;
			}
#endif
			// Finally check if player is using !hide.
			if (!targetPlayer->quietService->ShouldHide())
			{
				continue;
			}
			u32 index = g_pSurfPlayerManager->ToPlayer(pawn)->index;
			if (targetPlayer->quietService->ShouldHideIndex(index))
			{
				pTransmitInfo->m_pTransmitEdict->Clear(pawn->entindex());
			}
		}
	}
}

static void FilterQuietClients(const uint64 *clients, u32 emitterPlayerIndex = 0)
{
	for (i32 recipientPlayerIndex = 1; recipientPlayerIndex < MAXPLAYERS + 1; recipientPlayerIndex++)
	{
		SurfPlayer *recipient = g_pSurfPlayerManager->ToPlayer(recipientPlayerIndex);
		if (recipient->quietService->ShouldHide() && (emitterPlayerIndex == 0 || recipient->quietService->ShouldHideIndex(emitterPlayerIndex)))
		{
			*(uint64 *)clients &= ~(1ull << (recipientPlayerIndex - 1));
		}
	}
}

void Surf::quiet::OnPostEvent(INetworkMessageInternal *pEvent, const CNetMessage *pData, const uint64 *clients)
{
	NetMessageInfo_t *info = pEvent->GetNetMessageInfo();
	u32 emitterEntIndex = 0;

	switch (info->m_MessageId)
	{
		// Hide bullet decals, and sound.
		case GE_FireBulletsId:
		{
			auto msg = const_cast<CNetMessage *>(pData)->ToPB<CMsgTEFireBullets>();
			emitterEntIndex = msg->player() & 0x3FFF;
			break;
		}
		// Hide reload sounds.
		case CS_UM_WeaponSound:
		{
			auto msg = const_cast<CNetMessage *>(pData)->ToPB<CCSUsrMsg_WeaponSound>();
			emitterEntIndex = msg->entidx();
			break;
		}
		// Hide other sounds from player (eg. armor equipping)
		case GE_SosStartSoundEvent:
		{
			auto msg = const_cast<CNetMessage *>(pData)->ToPB<CMsgSosStartSoundEvent>();
			emitterEntIndex = msg->source_entity_index();
			break;
		}
		// Used by surf_misc to block valve's player say messages.
		case UM_SayText:
		{
			if (!SurfOptionService::GetOptionInt("overridePlayerChat", true))
			{
				return;
			}
			auto msg = const_cast<CNetMessage *>(pData)->ToPB<CUserMessageSayText>();
			i32 index = msg->playerindex();
			if (index == -1)
			{
				return;
			}
			{
				*(uint64 *)clients = 0;
			}
			return;
		}
		case UM_SayText2:
		{
			if (!SurfOptionService::GetOptionInt("overridePlayerChat", true))
			{
				return;
			}
			auto msg = const_cast<CNetMessage *>(pData)->ToPB<CUserMessageSayText2>();
			i32 index = msg->entityindex();
			if (index == -1)
			{
				return;
			}
			if (!msg->mutable_param1()->empty() || !msg->mutable_param2()->empty())
			{
				*(uint64 *)clients = 0;
			}
			return;
		}
		default:
		{
			return;
		}
	}
	CBaseEntity *emitterEnt = static_cast<CBaseEntity *>(GameEntitySystem()->GetEntityInstance(CEntityIndex(emitterEntIndex)));
	if (emitterEntIndex == 0 || !emitterEnt)
	{
		return;
	}

	// Convert this entindex into the index in the player controller.
	if (emitterEnt->IsPawn())
	{
		CBasePlayerPawn *pawn = static_cast<CBasePlayerPawn *>(emitterEnt);
		u32 emitterPlayerIndex = g_pSurfPlayerManager->ToPlayer(utils::GetController(pawn))->index;
		FilterQuietClients(clients, emitterPlayerIndex);
	}
	else if (V_strstr(emitterEnt->GetClassname(), "weapon_"))
	{
		// Find the owner of the weapon if possible
		if (emitterEnt->m_hOwnerEntity().IsValid() && emitterEnt->m_hOwnerEntity.Get()->IsPawn())
		{
			CBasePlayerPawn *ownerPawn = static_cast<CBasePlayerPawn *>(emitterEnt->m_hOwnerEntity().Get());
			u32 emitterPlayerIndex = g_pSurfPlayerManager->ToPlayer(ownerPawn)->index;
			FilterQuietClients(clients, emitterPlayerIndex);
		}
		// Otherwise just hide from everyone having !hide enabled
		else
		{
			FilterQuietClients(clients);
		}
	}
	// Special case for the armor sound upon spawning/respawning, because the emitter player is not yet known
	else if (V_strcmp(emitterEnt->GetClassname(), "item_assaultsuit") == 0)
	{
		FilterQuietClients(clients);
	}
}

void SurfQuietService::Init()
{
	SurfOptionService::RegisterEventListener(&optionEventListener);
}

void SurfQuietService::Reset()
{
	this->hideOtherPlayers = this->player->optionService->GetPreferenceBool("hideOtherPlayers", false);
	this->hideWeapon = this->player->optionService->GetPreferenceBool("hideWeapon", false);
}

void SurfQuietService::SendFullUpdate()
{
	if (CServerSideClient *client = g_pSurfUtils->GetClientBySlot(this->player->GetPlayerSlot()))
	{
		client->ForceFullUpdate();
	}
	// Keep the player's angles the same.
	QAngle angles;
	this->player->GetAngles(&angles);
	this->player->SetAngles(angles);
}

bool SurfQuietService::ShouldHide()
{
	if (!this->hideOtherPlayers)
	{
		return false;
	}

	// If the player is not alive and not spectating another player, don't hide other players
	if (!this->player->IsAlive() && !this->player->specService->GetSpectatedPlayer())
	{
		return false;
	}

	return true;
}

bool SurfQuietService::ShouldHideIndex(u32 targetIndex)
{
	// Don't self-hide.
	if (this->player->index == targetIndex)
	{
		return false;
	}

	// Don't hide the player being spectated
	if (this->player->specService->GetSpectatedPlayer() && this->player->specService->GetSpectatedPlayer()->index == targetIndex)
	{
		return false;
	}

	return true;
}

SCMD(surf_hideweapon, SCFL_PLAYER)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->quietService->ToggleHideWeapon();
	return true;
}

SCMD_LINK(surf_hw, surf_hideweapon);

void SurfQuietService::ToggleHideWeapon()
{
	auto *opts = this->player->optionService;
	opts->SetPreferenceBool("hideWeapon", !opts->GetPreferenceBool("hideWeapon", false));
	this->player->languageService->PrintChat(true, false,
											 this->hideWeapon ? "Quiet Option - Show Weapon - Disable" : "Quiet Option - Show Weapon - Enable");
}

void SurfQuietService::OnPhysicsSimulatePost()
{
	this->UpdateWeaponCamera();
}

// A custom camera that follows the eyes is still the pawn's view entity, and the client skips the viewmodel
// while one is set. The weapon itself stays networked, so the native crosshair keeps working.
void SurfQuietService::UpdateWeaponCamera()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!this->hideWeapon || !pawn || !pawn->IsAlive())
	{
		this->ReleaseWeaponCamera();
		return;
	}
	CCSCustomPlayerCamera *camera = static_cast<CCSCustomPlayerCamera *>(this->weaponCamera.Get());
	if (!camera || camera->m_hPawn().Get() != pawn)
	{
		this->ReleaseWeaponCamera();
		camera = CCSCustomPlayerCamera::Create(pawn);
		if (!camera)
		{
			return;
		}
		// GetCustomCamera() finds a pawn's camera by designer name, so a map script never gets handed this one
		// and spawns its own instead.
		camera->m_pEntity->m_designerName = GameEntitySystem()->AllocPooledString("surf_weapon_camera");
		camera->SetFollowConfig(pawn, true);
		this->weaponCamera = camera->GetRefEHandle();
	}
	CPlayer_CameraServices *cameraServices = pawn->m_pCameraServices();
	if (!cameraServices)
	{
		return;
	}
	// Only take the view while nothing else holds it: a map camera or point_viewcontrol keeps priority, and
	// the weapon is hidden again as soon as it lets go.
	CBaseEntity *viewEntity = cameraServices->m_hViewEntity().Get();
	if (!viewEntity || viewEntity == pawn)
	{
		camera->SetMode(CUSTOM_CAMERA_MODE_FOLLOW_POSITION);
	}
}

void SurfQuietService::ReleaseWeaponCamera()
{
	// Null on server exit.
	if (CCSCustomPlayerCamera *camera = GameEntitySystem() ? static_cast<CCSCustomPlayerCamera *>(this->weaponCamera.Get()) : nullptr)
	{
		// Only clears the view entity while it is still this camera.
		camera->SetMode(CUSTOM_CAMERA_MODE_DISABLED);
		g_pSurfUtils->RemoveEntity(camera);
	}
	this->weaponCamera.Term();
}

void SurfQuietService::Cleanup()
{
	for (i32 i = 0; i < MAXPLAYERS; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(CPlayerSlot(i));
		if (player && player->quietService)
		{
			player->quietService->ReleaseWeaponCamera();
		}
	}
}

void SurfQuietService::ApplyPreferences()
{
	auto *opts = this->player->optionService;

	const bool newHideOthers = opts->GetPreferenceBool("hideOtherPlayers", false);
	const bool changedHideOthers = newHideOthers != this->hideOtherPlayers;
	this->hideOtherPlayers = newHideOthers;

	this->hideWeapon = opts->GetPreferenceBool("hideWeapon", false);

	if (changedHideOthers)
	{
		this->SendFullUpdate();
	}
	this->UpdatePistol(this->player);
}

f64 SurfQuietService::UpdatePistol(SurfPlayer *player)
{
	if (!player->IsAlive() || !player->IsInGame())
	{
		return -1;
	}

	player->GetPlayerPawn()->m_pItemServices()->RemoveAllItems(false);
	if (player->quietService->ShouldHideWeapon())
	{
		auto weapon = player->GetPlayerPawn()->m_pItemServices()->GiveNamedItem(
			player->GetController()->m_iTeamNum() == CS_TEAM_CT ? "weapon_knife" : "weapon_knife_t");
	}
	else
	{
		auto knife = player->GetPlayerPawn()->m_pItemServices()->GiveNamedItem(
			player->GetController()->m_iTeamNum() == CS_TEAM_CT ? "weapon_knife" : "weapon_knife_t");
		auto weapon = player->GetPlayerPawn()->m_pItemServices()->GiveNamedItem(
			player->GetController()->m_iTeamNum() == CS_TEAM_CT ? "weapon_usp_silencer" : "weapon_glock");
	}
	return -1;
}

void SurfQuietService::ToggleHide()
{
	auto *opts = this->player->optionService;
	opts->SetPreferenceBool("hideOtherPlayers", !opts->GetPreferenceBool("hideOtherPlayers", false));
}

void SurfQuietService::UpdateHideState()
{
	CPlayer_ObserverServices *obsServices = this->player->GetController()->m_hPawn()->m_pObserverServices;
	if (!obsServices)
	{
		this->lastObserverMode = OBS_MODE_NONE;
		this->lastObserverTarget.Term();
		return;
	}
	// Nuclear option, define this if things crash still!
#if 0
	if (obsServices->m_iObserverMode() != this->lastObserverMode || obsServices->m_hObserverTarget() != this->lastObserverTarget)
	{
		this->SendFullUpdate();
	}
#endif
	this->lastObserverMode = obsServices->m_iObserverMode();
	this->lastObserverTarget = obsServices->m_hObserverTarget();
}
