#include "surf_beam.h"
#include "surf/spec/surf_spec.h"
#include "surf/noclip/surf_noclip.h"
#include "surf/language/surf_language.h"
#include "surf/option/surf_option.h"

#include "utils/simplecmds.h"
#include "utils/ctimer.h"
#include "sdk/entity/cparticlesystem.h"
#include "entitykeyvalues.h"

static_global class SurfOptionServiceEventListener_Beam : public SurfOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(SurfPlayer *player)
	{
		player->beamService->OnPlayerPreferencesLoaded();
	}
} optionEventListener;

void SurfBeamService::OnPlayerPreferencesLoaded()
{
	this->SetBeamType(this->player->optionService->GetPreferenceInt("desiredBeamType"));
	this->playerBeamOffset = this->player->optionService->GetPreferenceVector("beamOffset", SurfBeamService::defaultOffset);
}

void SurfBeamService::Init()
{
	SurfOptionService::RegisterEventListener(&optionEventListener);
}

SCMD(surf_beam, SCFL_MISC | SCFL_PREFERENCE)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	u8 newDesiredBeamType = (player->beamService->desiredBeamType + 1) % SurfBeamService::BEAM_COUNT;
	if (args->ArgC() >= 2)
	{
		if (SURF_STREQI(args->Arg(1), "off"))
		{
			newDesiredBeamType = SurfBeamService::BEAM_NONE;
		}
		else if (SURF_STREQI(args->Arg(1), "feet"))
		{
			newDesiredBeamType = SurfBeamService::BEAM_FEET;
		}
		else if (SURF_STREQI(args->Arg(1), "ground"))
		{
			newDesiredBeamType = SurfBeamService::BEAM_GROUND;
		}
		else
		{
			player->languageService->PrintChat(true, false, "Beam Command Usage");
			return MRES_HANDLED;
		}
	}
	player->beamService->SetBeamType(newDesiredBeamType);
	switch (player->beamService->desiredBeamType)
	{
		case SurfBeamService::BEAM_NONE:
		{
			player->languageService->PrintChat(true, false, "Beam Changed (None)");
			break;
		}
		case SurfBeamService::BEAM_FEET:
		{
			player->languageService->PrintChat(true, false, "Beam Changed (Feet)");
			break;
		}
		case SurfBeamService::BEAM_GROUND:
		{
			player->languageService->PrintChat(true, false, "Beam Changed (Ground)");
			break;
		}
	}
	player->optionService->SetPreferenceInt("desiredBeamType", player->beamService->desiredBeamType);
	return MRES_HANDLED;
}

SCMD_LINK(surf_trail, surf_beam);

SCMD(surf_beamoffset, SCFL_MISC | SCFL_PREFERENCE)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (args->ArgC() < 4 || !utils::IsNumeric(args->Arg(1)) || !utils::IsNumeric(args->Arg(2)) || !utils::IsNumeric(args->Arg(3)))
	{
		player->languageService->PrintChat(true, false, "Beam Offset Command Usage");
		player->languageService->PrintChat(true, false, "Current Beam Offset", player->beamService->playerBeamOffset.x,
										   player->beamService->playerBeamOffset.y, player->beamService->playerBeamOffset.z);
		return MRES_HANDLED;
	}
	player->beamService->playerBeamOffset = Vector(atof(args->Arg(1)), atof(args->Arg(2)), atof(args->Arg(3)));

	player->optionService->SetPreferenceVector("beamOffset", player->beamService->playerBeamOffset);
	player->languageService->PrintChat(true, false, "Current Beam Offset", player->beamService->playerBeamOffset.x,
									   player->beamService->playerBeamOffset.y, player->beamService->playerBeamOffset.z);
	return MRES_HANDLED;
}

void SurfBeamService::Update()
{
	SurfPlayer *newTarget = this->player->IsAlive() ? this->player : this->player->specService->GetSpectatedPlayer();

	if (this->target != newTarget)
	{
		if (this->playerBeam.Get())
		{
			g_pSurfUtils->RemoveEntity(this->playerBeam.Get());
		}
		if (this->playerBeamNew.Get())
		{
			g_pSurfUtils->RemoveEntity(this->playerBeamNew.Get());
		}
		this->playerBeam = {};
		this->playerBeamNew = {};
		this->target = newTarget;
	}

	// It is possible that the player is in game has no player pawn on game frame loop.
	// This is probably the case for CSTV bots.
	if (!this->player->GetPlayerPawn())
	{
		this->validBeam = false;
	}
	else
	{
		if ((this->player->GetPlayerPawn()->m_fFlags() & FL_ONGROUND && this->player->GetMoveType() == MOVETYPE_WALK)
			|| this->player->GetMoveType() == MOVETYPE_LADDER)
		{
			this->validBeam = g_pSurfUtils->GetServerGlobals()->curtime - this->player->landingTime >= 0.04f;
		}

		if (this->player->noclipService->JustNoclipped() || this->teleportedThisTick)
		{
			this->validBeam = false;
		}
	}
	this->teleportedThisTick = false;
	this->UpdatePlayerBeam();
}

void SurfBeamService::UpdatePlayerBeam()
{
	bool shouldDraw = this->desiredBeamType != SurfBeamService::BEAM_NONE;

	// clang-format off
	shouldDraw &= this->target && this->target->GetPlayerPawn() && this->target->GetPlayerPawn()->IsAlive()
	 	&& this->target->beamService->validBeam
	 	&& this->target->takeoffTime > 0.0f
	 	&& (!(this->target->GetPlayerPawn()->m_fFlags() & FL_ONGROUND) || g_pSurfUtils->GetServerGlobals()->curtime - this->target->landingTime < 0.04f);
	// clang-format on
	if (!shouldDraw)
	{
		if (this->playerBeam.Get())
		{
			g_pSurfUtils->RemoveEntity(this->playerBeam.Get());
		}
		if (this->playerBeamNew.Get())
		{
			g_pSurfUtils->RemoveEntity(this->playerBeamNew.Get());
		}
		this->playerBeam = {};
		this->playerBeamNew = {};
		return;
	}
	Vector origin = this->target->moveDataPre.m_vecAbsOrigin;

	if (this->desiredBeamType == SurfBeamService::BEAM_GROUND)
	{
		origin.z = this->target->takeoffOrigin.z;
	}
	CParticleSystem *beam = static_cast<CParticleSystem *>(this->playerBeam.Get());

	if (!beam)
	{
		beam = utils::CreateEntityByName<CParticleSystem>("info_particle_system");

		CEntityKeyValues *pKeyValues = new CEntityKeyValues();
		pKeyValues->SetString("effect_name", "particles/ui/hud/ui_map_def_utility_trail.vpcf");
		pKeyValues->SetVector("origin", origin);
		pKeyValues->SetBool("start_active", true);

		beam->DispatchSpawn(pKeyValues);
		this->playerBeam = beam->GetRefEHandle();
	}
	origin += this->playerBeamOffset;
	beam->Teleport(&origin, nullptr, &vec3_origin);

	// Setup for the next beam because the current one will expire in 4 seconds.
	if (beam && g_pSurfUtils->GetServerGlobals()->curtime - beam->m_flStartTime().GetTime() > 3.0f)
	{
		if (!this->playerBeamNew.Get())
		{
			CParticleSystem *newBeam = utils::CreateEntityByName<CParticleSystem>("info_particle_system");

			CEntityKeyValues *pKeyValues = new CEntityKeyValues();
			pKeyValues->SetString("effect_name", "particles/ui/hud/ui_map_def_utility_trail.vpcf");
			pKeyValues->SetVector("origin", origin);
			pKeyValues->SetBool("start_active", true);

			newBeam->DispatchSpawn(pKeyValues);
			newBeam->m_iTeamNum(CUSTOM_PARTICLE_SYSTEM_TEAM);
			this->playerBeamNew = newBeam->GetRefEHandle();
		}
		else if (g_pSurfUtils->GetServerGlobals()->curtime - beam->m_flStartTime().GetTime() > 3.2f)
		{
			g_pSurfUtils->RemoveEntity(beam);
			this->playerBeam = this->playerBeamNew;
			this->playerBeamNew = {};
		}
	}
}

void SurfBeamService::Reset()
{
	this->playerBeam = {};
	this->playerBeamNew = {};
	this->playerBeamOffset = SurfBeamService::defaultOffset;
	this->target = {};
	this->validBeam = {};
	this->desiredBeamType = {};
	this->teleportedThisTick = false;
}

void SurfBeamService::UpdateBeams()
{
	for (i32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		if (!player || !player->IsInGame())
		{
			continue;
		}
		player->beamService->Update();
	}
}
