#include "surf.h"
#include "utils/utils.h"
#include "utils/ctimer.h"
#include "anticheat/surf_anticheat.h"
#include "beam/surf_beam.h"
#include "beam/surf_zone_beam.h"
#include "checkpoint/surf_checkpoint.h"
#include "db/surf_db.h"
#include "hud/surf_hud.h"
#include "language/surf_language.h"
#include "mode/surf_mode.h"
#include "noclip/surf_noclip.h"
#include "option/surf_option.h"
#include "quiet/surf_quiet.h"
#include "spec/surf_spec.h"
#include "goto/surf_goto.h"
#include "style/surf_style.h"
#include "telemetry/surf_telemetry.h"
#include "timer/surf_timer.h"
#include "tip/surf_tip.h"
#include "trigger/surf_trigger.h"
#include "global/surf_global.h"
#include "profile/surf_profile.h"

#include "sdk/datatypes.h"
#include "sdk/entity/cbasetrigger.h"
#include "vprof.h"
#include "steam/isteamgameserver.h"
#include "tier0/memdbgon.h"

extern CSteamGameServerAPIContext g_steamAPI;

void SurfPlayer::Init()
{
	MovementPlayer::Init();
	this->hideLegs = false;

	// TODO: initialize every service.
	delete this->anticheatService;
	delete this->beamService;
	delete this->zoneBeamService;
	delete this->checkpointService;
	delete this->languageService;
	delete this->databaseService;
	delete this->quietService;
	delete this->hudService;
	delete this->specService;
	delete this->timerService;
	delete this->optionService;
	delete this->noclipService;
	delete this->tipService;
	delete this->telemetryService;
	delete this->triggerService;
	delete this->globalService;
	delete this->profileService;

	this->anticheatService = new SurfAnticheatService(this);
	this->beamService = new SurfBeamService(this);
	this->zoneBeamService = new SurfZoneBeamService(this);
	this->checkpointService = new SurfCheckpointService(this);
	this->databaseService = new SurfDatabaseService(this);
	this->languageService = new SurfLanguageService(this);
	this->noclipService = new SurfNoclipService(this);
	this->quietService = new SurfQuietService(this);
	this->hudService = new SurfHUDService(this);
	this->specService = new SurfSpecService(this);
	this->gotoService = new SurfGotoService(this);
	this->timerService = new SurfTimerService(this);
	this->optionService = new SurfOptionService(this);
	this->tipService = new SurfTipService(this);
	this->telemetryService = new SurfTelemetryService(this);
	this->triggerService = new SurfTriggerService(this);
	this->globalService = new SurfGlobalService(this);
	this->profileService = new SurfProfileService(this);

	Surf::mode::InitModeService(this);
}

void SurfPlayer::Reset()
{
	MovementPlayer::Reset();

	// Reset services that should not persist across player sessions.
	this->languageService->Reset();
	this->tipService->Reset();
	this->modeService->Reset();
	this->optionService->Reset();
	this->checkpointService->Reset();
	this->noclipService->Reset();
	this->quietService->Reset();
	this->hudService->Reset();
	this->timerService->Reset();
	this->specService->Reset();
	this->triggerService->Reset();
	this->beamService->Reset();
	this->telemetryService->Reset();

	g_pSurfModeManager->SwitchToMode(this, SurfOptionService::GetOptionStr("defaultMode", SURF_DEFAULT_MODE), true, true);
	g_pSurfStyleManager->ClearStyles(this, true);
	CSplitString styles(SurfOptionService::GetOptionStr("defaultStyles"), ",");
	FOR_EACH_VEC(styles, i)
	{
		g_pSurfStyleManager->AddStyle(this, styles[i]);
	}
}

void SurfPlayer::OnPlayerConnect(u64 steamID64)
{
	this->languageService->OnPlayerConnect(steamID64);
}

void SurfPlayer::OnPlayerActive()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	// Mode/Styles stuff must be here for convars to be properly replicated.
	g_pSurfModeManager->SwitchToMode(this, this->modeService->GetModeName(), true, true, false);
	g_pSurfStyleManager->RefreshStyles(this, false);

	this->optionService->OnPlayerActive();
}

void SurfPlayer::OnPlayerFullyConnect()
{
	this->anticheatService->OnPlayerFullyConnect();
}

void SurfPlayer::OnAuthorized()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer::OnAuthorized();
	this->databaseService->SetupClient();
	this->profileService->timeToNextRatingRefresh = 0.0f; // Force immediate refresh
	this->globalService->OnPlayerAuthorized();
}

void SurfPlayer::OnPhysicsSimulate()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer::OnPhysicsSimulate();
	this->triggerService->OnPhysicsSimulate();
	this->modeService->OnPhysicsSimulate();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPhysicsSimulate();
	}
	this->noclipService->HandleMoveCollision();
	this->EnableGodMode();
	this->UpdatePlayerModelAlpha();
}

void SurfPlayer::OnPhysicsSimulatePost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer::OnPhysicsSimulatePost();
	this->triggerService->OnPhysicsSimulatePost();
	this->telemetryService->OnPhysicsSimulatePost();
	this->modeService->OnPhysicsSimulatePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPhysicsSimulatePost();
	}
	this->timerService->OnPhysicsSimulatePost();
	if (this->specService->GetSpectatedPlayer())
	{
		SurfHUDService::DrawPanels(this->specService->GetSpectatedPlayer(), this);
	}
	else if (this->IsAlive())
	{
		SurfHUDService::DrawPanels(this, this);
	}
	this->quietService->OnPhysicsSimulatePost();
	this->profileService->OnPhysicsSimulatePost();
}

void SurfPlayer::OnProcessUsercmds(void *cmds, int numcmds)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnProcessUsercmds(cmds, numcmds);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnProcessUsercmds(cmds, numcmds);
	}
}

void SurfPlayer::OnProcessUsercmdsPost(void *cmds, int numcmds)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnProcessUsercmdsPost(cmds, numcmds);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnProcessUsercmdsPost(cmds, numcmds);
	}
}

void SurfPlayer::OnSetupMove(PlayerCommand *pc)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnSetupMove(pc);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnSetupMove(pc);
	}
}

void SurfPlayer::OnSetupMovePost(PlayerCommand *pc)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnSetupMovePost(pc);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnSetupMovePost(pc);
	}
}

void SurfPlayer::OnProcessMovement()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer::OnProcessMovement();

	Surf::mode::ApplyModeSettings(this);

	this->triggerService->OnProcessMovement();
	this->modeService->OnProcessMovement();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnProcessMovement();
	}

	this->checkpointService->TpHoldPlayerStill();
}

void SurfPlayer::OnProcessMovementPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");

	this->modeService->OnProcessMovementPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnProcessMovementPost();
	}
	this->triggerService->OnProcessMovementPost();
	MovementPlayer::OnProcessMovementPost();
}

void SurfPlayer::OnPlayerMove()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnPlayerMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPlayerMove();
	}
}

void SurfPlayer::OnPlayerMovePost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnPlayerMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPlayerMovePost();
	}
}

void SurfPlayer::OnCheckParameters()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckParameters();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckParameters();
	}
}

void SurfPlayer::OnCheckParametersPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckParametersPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckParametersPost();
	}
}

void SurfPlayer::OnCanMove()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCanMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCanMove();
	}
}

void SurfPlayer::OnCanMovePost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCanMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCanMovePost();
	}
}

void SurfPlayer::OnFullWalkMove(bool &ground)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnFullWalkMove(ground);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFullWalkMove(ground);
	}
}

void SurfPlayer::OnFullWalkMovePost(bool ground)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnFullWalkMovePost(ground);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFullWalkMovePost(ground);
	}
}

void SurfPlayer::OnMoveInit()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnMoveInit();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnMoveInit();
	}
}

void SurfPlayer::OnMoveInitPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnMoveInitPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnMoveInitPost();
	}
}

void SurfPlayer::OnCheckWater()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckWater();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckWater();
	}
}

void SurfPlayer::OnWaterMove()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnWaterMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnWaterMove();
	}
}

void SurfPlayer::OnWaterMovePost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnWaterMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnWaterMovePost();
	}
}

void SurfPlayer::OnCheckWaterPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckWaterPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckWaterPost();
	}
}

void SurfPlayer::OnCheckVelocity(const char *a3)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckVelocity(a3);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckVelocity(a3);
	}
}

void SurfPlayer::OnCheckVelocityPost(const char *a3)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckVelocityPost(a3);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckVelocityPost(a3);
	}
}

void SurfPlayer::OnDuck()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnDuck();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnDuck();
	}
}

void SurfPlayer::OnDuckPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnDuckPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnDuckPost();
	}
}

void SurfPlayer::OnCanUnduck()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCanUnduck();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCanUnduck();
	}
}

void SurfPlayer::OnCanUnduckPost(bool &ret)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCanUnduckPost(ret);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCanUnduckPost(ret);
	}
}

void SurfPlayer::OnLadderMove()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnLadderMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnLadderMove();
	}
}

void SurfPlayer::OnLadderMovePost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnLadderMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnLadderMovePost();
	}
}

void SurfPlayer::OnCheckJumpButton()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckJumpButton();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckJumpButton();
	}
	this->triggerService->OnCheckJumpButton();
}

void SurfPlayer::OnCheckJumpButtonPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckJumpButtonPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckJumpButtonPost();
	}
}

void SurfPlayer::OnJump()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnJump();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnJump();
	}
}

void SurfPlayer::OnJumpPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnJumpPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnJumpPost();
	}
}

void SurfPlayer::OnAirMove()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnAirMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnAirMove();
	}
}

void SurfPlayer::OnAirMovePost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnAirMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnAirMovePost();
	}
}

void SurfPlayer::OnFriction()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnFriction();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFriction();
	}
}

void SurfPlayer::OnFrictionPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnFrictionPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFrictionPost();
	}
}

void SurfPlayer::OnWalkMove()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnWalkMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnWalkMove();
	}
}

void SurfPlayer::OnWalkMovePost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnWalkMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnWalkMovePost();
	}
}

void SurfPlayer::OnTryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnTryPlayerMove(pFirstDest, pFirstTrace, bIsSurfing);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnTryPlayerMove(pFirstDest, pFirstTrace, bIsSurfing);
	}
}

void SurfPlayer::OnTryPlayerMovePost(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnTryPlayerMovePost(pFirstDest, pFirstTrace, bIsSurfing);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnTryPlayerMovePost(pFirstDest, pFirstTrace, bIsSurfing);
	}
}

void SurfPlayer::OnCategorizePosition(bool bStayOnGround)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCategorizePosition(bStayOnGround);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCategorizePosition(bStayOnGround);
	}
}

void SurfPlayer::OnCategorizePositionPost(bool bStayOnGround)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCategorizePositionPost(bStayOnGround);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCategorizePositionPost(bStayOnGround);
	}
}

void SurfPlayer::OnFinishGravity()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnFinishGravity();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFinishGravity();
	}
}

void SurfPlayer::OnFinishGravityPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnFinishGravityPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnFinishGravityPost();
	}
}

void SurfPlayer::OnCheckFalling()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckFalling();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckFalling();
	}
}

void SurfPlayer::OnCheckFallingPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnCheckFallingPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnCheckFallingPost();
	}
}

void SurfPlayer::OnPostPlayerMove()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnPostPlayerMove();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPostPlayerMove();
	}
}

void SurfPlayer::OnPostPlayerMovePost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnPostPlayerMovePost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPostPlayerMovePost();
	}
}

void SurfPlayer::OnPostThink()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnPostThink();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPostThink();
	}
	MovementPlayer::OnPostThink();
}

void SurfPlayer::OnPostThinkPost()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->modeService->OnPostThinkPost();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnPostThinkPost();
	}
}

void SurfPlayer::OnStartTouchGround()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->timerService->OnStartTouchGround();
	this->modeService->OnStartTouchGround();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnStartTouchGround();
	}
}

void SurfPlayer::OnStopTouchGround()
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->timerService->OnStopTouchGround();
	this->modeService->OnStopTouchGround();
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnStopTouchGround();
	}
	this->triggerService->OnStopTouchGround();
}

void SurfPlayer::OnChangeMoveType(MoveType_t oldMoveType)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->timerService->OnChangeMoveType(oldMoveType);
	this->modeService->OnChangeMoveType(oldMoveType);
	FOR_EACH_VEC(this->styleServices, i)
	{
		this->styleServices[i]->OnChangeMoveType(oldMoveType);
	}
}

void SurfPlayer::OnTeleport(const Vector *origin, const QAngle *angles, const Vector *velocity)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	this->lastTeleportTime = g_pSurfUtils->GetServerGlobals()->curtime;
	this->modeService->OnTeleport(origin, angles, velocity);
	this->timerService->OnTeleport(origin, angles, velocity);
	if (origin)
	{
		this->beamService->OnTeleport();
	}
	this->triggerService->OnTeleport();
}

void SurfPlayer::EnableGodMode()
{
	CCSPlayerPawn *pawn = this->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	if (pawn->m_bTakesDamage())
	{
		pawn->m_bTakesDamage(false);
	}
}

void SurfPlayer::UpdatePlayerModelAlpha()
{
	CCSPlayerPawn *pawn = this->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	Color ogColor = pawn->m_clrRender();
	if (this->hideLegs && pawn->m_clrRender().a() == 255)
	{
		pawn->m_clrRender(Color(255, 255, 255, 254));
	}
	else if (!this->hideLegs && pawn->m_clrRender().a() != 255)
	{
		pawn->m_clrRender(Color(255, 255, 255, 255));
	}
}

bool SurfPlayer::JustTeleported(f32 threshold)
{
	return g_pSurfUtils->GetServerGlobals()->curtime - this->lastTeleportTime < threshold;
}

void SurfPlayer::ToggleHideLegs()
{
	this->hideLegs = !this->hideLegs;
	this->optionService->SetPreferenceBool("hideLegs", this->hideLegs);
}

void SurfPlayer::PlayErrorSound()
{
	utils::PlaySoundToClient(this->GetPlayerSlot(), MV_SND_ERROR);
}

void SurfPlayer::TouchTriggersAlongPath(const Vector &start, const Vector &end, const bbox_t &bounds)
{
	this->triggerService->TouchTriggersAlongPath(start, end, bounds);
}

void SurfPlayer::UpdateTriggerTouchList()
{
	this->triggerService->UpdateTriggerTouchList();
}

void SurfPlayer::OnChangeTeamPost(i32 team)
{
	this->timerService->OnPlayerJoinTeam(team);
}

const CVValue_t *SurfPlayer::GetCvarValueFromModeStyles(const char *name)
{
	if (!name)
	{
		assert(0);
		return CVValue_t::InvalidValue();
	}

	ConVarRefAbstract cvarRef(name);
	if (!cvarRef.IsValidRef() || !cvarRef.IsConVarDataAvailable())
	{
		assert(0);
		META_CONPRINTF("Failed to find %s!\n", name);
		return CVValue_t::InvalidValue();
	}

	FOR_EACH_VEC_BACK(this->styleServices, i)
	{
		if (this->styleServices[i]->GetTweakedConvarValue(name))
		{
			return this->styleServices[i]->GetTweakedConvarValue(name);
		}
	}

	for (int i = 0; i < MODECVAR_COUNT; i++)
	{
		if (!Surf::mode::modeCvarRefs[i]->IsValidRef() || !Surf::mode::modeCvarRefs[i]->IsConVarDataAvailable())
		{
			continue;
		}
		if (!V_stricmp(Surf::mode::modeCvarRefs[i]->GetName(), name))
		{
			return &this->modeService->GetModeConVarValues()[i];
		}
	}

	return cvarRef.GetConVarData()->Value(-1);
}
