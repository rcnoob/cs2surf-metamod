#include "surf_timer.h"
#include "surf/db/surf_db.h"
#include "surf/global/surf_global.h"
#include "surf/language/surf_language.h"
#include "surf/mode/surf_mode.h"
#include "surf/style/surf_style.h"
#include "surf/noclip/surf_noclip.h"
#include "surf/option/surf_option.h"
#include "surf/language/surf_language.h"
#include "surf/trigger/surf_trigger.h"
#include "surf/spec/surf_spec.h"
#include "announce.h"

#include "utils/utils.h"
#include "utils/simplecmds.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

// clang-format off
constexpr const char *diffTextKeys[SurfTimerService::CompareType::COMPARETYPE_COUNT] = {
	"",
	"Server PB Diff (Overall)",
	"Global PB Diff (Overall)",
	"SR Diff (Overall)",
	"WR Diff (Overall)"
};

constexpr const char *missedTimeKeys[SurfTimerService::CompareType::COMPARETYPE_COUNT] = {
	"",
	"Missed Server PB (Overall)",
	"Missed Global PB (Overall)",
	"Missed SR (Overall)",
	"Missed WR (Overall)"
};

// clang-format on

static_global class SurfDatabaseServiceEventListener_Timer : public SurfDatabaseServiceEventListener
{
public:
	virtual void OnMapSetup() override;
	virtual void OnClientSetup(Player *player, u64 steamID64, bool isCheater) override;
} databaseEventListener;

static_global class SurfOptionServiceEventListener_Timer : public SurfOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(SurfPlayer *player)
	{
		player->timerService->OnPlayerPreferencesLoaded();
	}
} optionEventListener;

std::unordered_map<PBDataKey, PBData> SurfTimerService::srCache;
std::unordered_map<PBDataKey, PBData> SurfTimerService::wrCache;

static_global CUtlVector<SurfTimerServiceEventListener *> eventListeners;

bool SurfTimerService::RegisterEventListener(SurfTimerServiceEventListener *eventListener)
{
	if (eventListeners.Find(eventListener) >= 0)
	{
		return false;
	}
	eventListeners.AddToTail(eventListener);
	return true;
}

bool SurfTimerService::UnregisterEventListener(SurfTimerServiceEventListener *eventListener)
{
	return eventListeners.FindAndRemove(eventListener);
}

void SurfTimerService::StartZoneStartTouch(const SurfCourseDescriptor *course)
{
	this->touchedGroundSinceTouchingStartZone = !!(this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND);
	this->TimerStop(false);
	this->inStartzone = true;
}

void SurfTimerService::StartZoneEndTouch(const SurfCourseDescriptor *course)
{
	if (this->touchedGroundSinceTouchingStartZone)
	{
		this->TimerStart(course);
	}
	this->inStartzone = false;
}

void SurfTimerService::CheckpointZoneStartTouch(const SurfCourseDescriptor *course, i32 cpNumber)
{
	if (!this->timerRunning || course->guid != this->currentCourseGUID)
	{
		return;
	}

	assert(cpNumber > INVALID_CHECKPOINT_NUMBER && cpNumber < SURF_MAX_CHECKPOINT_ZONES);

	if (this->cpZoneTimes[cpNumber - 1] < 0)
	{
		this->PlayReachedCheckpointSound();
		this->cpZoneTimes[cpNumber - 1] = this->GetTime();
		this->ShowCheckpointText(cpNumber);
		this->lastCheckpoint = cpNumber;
		this->reachedCheckpoints++;
	}
}

void SurfTimerService::StageZoneStartTouch(const SurfCourseDescriptor *course, i32 stageNumber)
{
	if (!this->timerRunning || course->guid != this->currentCourseGUID)
	{
		return;
	}

	assert(stageNumber > INVALID_STAGE_NUMBER && stageNumber < SURF_MAX_STAGE_ZONES);

	// skipped stage
	if (stageNumber > this->currentStage + 1)
	{
		this->PlayMissedZoneSound();
		this->player->languageService->PrintChat(true, false, "Touched too high stage number (Missed stage)", this->currentStage + 1);
		return;
	}

	// same stage (failed)
	if (stageNumber == this->currentStage)
	{
		return;
	}

	// next stage
	if (stageNumber == this->currentStage + 1)
	{
		this->stageZoneTimes[this->currentStage - 1] = this->GetTime() - this->stageEndTouchTimes[this->currentStage - 1];

		this->PlayReachedStageSound();
		this->ShowStageText();
		this->currentStage++;
	}
}

void SurfTimerService::StageZoneEndTouch(const SurfCourseDescriptor *course, i32 stageNumber)
{
	if (!this->timerRunning || course->guid != this->currentCourseGUID)
	{
		return;
	}

	assert(stageNumber > INVALID_STAGE_NUMBER && stageNumber < SURF_MAX_STAGE_ZONES);

	this->stageEndTouchTimes[this->currentStage - 1] = this->GetTime();
}

bool SurfTimerService::TimerStart(const SurfCourseDescriptor *courseDesc, bool playSound)
{
	// clang-format off
	if (!this->player->GetPlayerPawn()->IsAlive()
		|| this->JustStartedTimer()
		|| this->player->JustTeleported()
		|| this->player->inPerf
		|| this->player->noclipService->JustNoclipped()
		|| !this->HasValidMoveType()
		|| this->JustLanded()
		|| (this->GetTimerRunning() && courseDesc->guid == this->currentCourseGUID)
		|| (!(this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) && !this->GetValidJump()))
	// clang-format on
	{
		return false;
	}
	if (V_strlen(this->player->modeService->GetModeName()) > SURF_MAX_MODE_NAME_LENGTH)
	{
		Warning("[Surf] Timer start failed: Mode name is too long!");
		return false;
	}

	bool allowStart = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowStart &= eventListeners[i]->OnTimerStart(this->player, courseDesc->guid);
	}
	if (!allowStart)
	{
		return false;
	}

	this->currentTime = 0.0f;
	this->timerRunning = true;

	this->reachedCheckpoints = 0;
	this->lastCheckpoint = 0;

	f64 invalidTime = -1;
	this->cpZoneTimes.SetSize(courseDesc->checkpointCount);
	this->stageZoneTimes.SetSize(courseDesc->stageCount);
	this->stageEndTouchTimes.SetSize(courseDesc->stageCount);

	this->cpZoneTimes.FillWithValue(invalidTime);
	this->stageZoneTimes.FillWithValue(invalidTime);
	this->stageEndTouchTimes.FillWithValue(invalidTime);

	if (courseDesc->stageCount > 0)
	{
		this->currentStage = 1;
		// initialize stage 1 end touch time
		this->stageEndTouchTimes[0] = this->GetTime();
	}
	else
	{
		this->currentStage = 0;
	}

	SetCourse(courseDesc->guid);
	this->validTime = true;
	this->shouldAnnounceMissedTime = true;

	this->UpdateCurrentCompareType(ToPBDataKey(Surf::mode::GetModeInfo(this->player->modeService).id, courseDesc->guid));

	if (playSound)
	{
		for (SurfPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
		{
			player->timerService->PlayTimerStartSound();
		}
		this->PlayTimerStartSound();
	}

	if (!this->player->IsAuthenticated())
	{
		this->player->languageService->PrintChat(true, false, "No Steam Authentication Warning");
	}
	if (SurfGlobalService::IsAvailable() && !this->player->hasPrime)
	{
		this->player->languageService->PrintChat(true, false, "No Prime Warning");
	}

	const char *language = this->player->languageService->GetLanguage();
	std::string startSpeedText = this->player->timerService->GetStartSpeedText(language);

	this->player->languageService->PrintChat(true, true, startSpeedText.c_str());

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerStartPost(this->player, courseDesc->guid);
	}
	return true;
}

bool SurfTimerService::TimerEnd(const SurfCourseDescriptor *courseDesc)
{
	if (!this->player->IsAlive())
	{
		return false;
	}

	if (!this->timerRunning || courseDesc->guid != this->currentCourseGUID)
	{
		for (SurfPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
		{
			player->timerService->PlayTimerFalseEndSound();
		}
		this->PlayTimerFalseEndSound();
		this->lastFalseEndTime = g_pSurfUtils->GetServerGlobals()->curtime;
		return false;
	}

	if (courseDesc->stageCount > 0 && (this->currentStage - 1 != courseDesc->stageCount))
	{
		this->PlayMissedZoneSound();
		this->player->languageService->PrintChat(true, false, "Can't Finish Run (Missed Stage)", this->currentStage + 1);
		return false;
	}

	if (this->reachedCheckpoints != courseDesc->checkpointCount)
	{
		this->PlayMissedZoneSound();
		i32 missCount = courseDesc->checkpointCount - this->reachedCheckpoints;
		if (missCount == 1)
		{
			this->player->languageService->PrintChat(true, false, "Can't Finish Run (Missed a Checkpoint Zone)");
		}
		else
		{
			this->player->languageService->PrintChat(true, false, "Can't Finish Run (Missed Checkpoint Zones)", missCount);
		}
		return false;
	}

	f32 time = this->GetTime() + g_pSurfUtils->GetServerGlobals()->frametime;
	u32 teleportsUsed = this->player->checkpointService->GetTeleportCount();

	bool allowEnd = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowEnd &= eventListeners[i]->OnTimerEnd(this->player, this->currentCourseGUID, time);
	}
	if (!allowEnd)
	{
		return false;
	}
	// Update current time for one last time.
	this->currentTime = time;

	this->timerRunning = false;
	this->lastEndTime = g_pSurfUtils->GetServerGlobals()->curtime;

	for (SurfPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
	{
		player->timerService->PlayTimerEndSound();
	}
	this->PlayTimerEndSound();

	if (!this->player->GetPlayerPawn()->IsBot())
	{
		RecordAnnounce::Create(this->player);
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerEndPost(this->player, this->currentCourseGUID, time);
	}

	// Reset current stage immediately to remove HUD element
	this->currentStage = 0;

	return true;
}

bool SurfTimerService::TimerStop(bool playSound)
{
	if (!this->timerRunning)
	{
		return false;
	}
	this->timerRunning = false;
	if (playSound)
	{
		for (SurfPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
		{
			player->timerService->PlayTimerStopSound();
		}
		this->PlayTimerStopSound();
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerStopped(this->player, this->currentCourseGUID);
	}

	// Reset current stage immediately to remove HUD element
	this->currentStage = 0;

	return true;
}

void SurfTimerService::TimerStopAll(bool playSound)
{
	for (int i = 0; i < MAXPLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		if (!player || !player->timerService)
		{
			continue;
		}
		player->timerService->TimerStop(playSound);
	}
}

void SurfTimerService::InvalidateJump()
{
	this->validJump = false;
	this->lastInvalidateTime = g_pSurfUtils->GetServerGlobals()->curtime;
}

void SurfTimerService::PlayTimerStartSound()
{
	if (g_pSurfUtils->GetServerGlobals()->curtime - this->lastStartSoundTime > SURF_TIMER_SOUND_COOLDOWN)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_START);
		this->lastStartSoundTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}
}

void SurfTimerService::InvalidateRun()
{
	if (!this->validTime)
	{
		return;
	}
	this->validTime = false;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerInvalidated(this->player);
	}
}

bool SurfTimerService::HasValidMoveType()
{
	return SurfTimerService::IsValidMoveType(this->player->GetMoveType());
}

bool SurfTimerService::JustEndedTimer()
{
	return g_pSurfUtils->GetServerGlobals()->curtime - this->lastEndTime > 1.0f;
}

void SurfTimerService::PlayTimerEndSound()
{
	utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_END);
}

void SurfTimerService::PlayTimerFalseEndSound()
{
	utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_FALSE_END);
}

void SurfTimerService::PlayMissedZoneSound()
{
	utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_MISSED_ZONE);
}

void SurfTimerService::PlayReachedCheckpointSound()
{
	utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_REACH_CHECKPOINT);
}

void SurfTimerService::PlayReachedStageSound()
{
	utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_REACH_STAGE);
}

void SurfTimerService::PlayTimerStopSound()
{
	if (this->shouldPlayTimerStopSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_STOP);
	}
}

void SurfTimerService::PlayMissedTimeSound()
{
	if (g_pSurfUtils->GetServerGlobals()->curtime - this->lastMissedTimeSoundTime > SURF_TIMER_SOUND_COOLDOWN)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_MISSED_TIME);
		this->lastMissedTimeSoundTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}
}

void SurfTimerService::FormatTime(f64 time, char *output, u32 length, bool precise)
{
	int roundedTime = RoundFloatToInt(time * 1000); // Time rounded to number of ms

	int milliseconds = roundedTime % 1000;
	roundedTime = (roundedTime - milliseconds) / 1000;
	int seconds = roundedTime % 60;
	roundedTime = (roundedTime - seconds) / 60;
	int minutes = roundedTime % 60;
	roundedTime = (roundedTime - minutes) / 60;
	int hours = roundedTime;

	if (hours == 0)
	{
		if (precise)
		{
			snprintf(output, length, "%02i:%02i.%03i", minutes, seconds, milliseconds);
		}
		else
		{
			snprintf(output, length, "%i:%02i", minutes, seconds);
		}
	}
	else
	{
		if (precise)
		{
			snprintf(output, length, "%i:%02i:%02i.%03i", hours, minutes, seconds, milliseconds);
		}
		else
		{
			snprintf(output, length, "%i:%02i:%02i", hours, minutes, seconds);
		}
	}
}

static_function std::string GetTeleportCountText(int tpCount, const char *language)
{
	return tpCount == 1 ? SurfLanguageService::PrepareMessageWithLang(language, "1 Teleport Text")
						: SurfLanguageService::PrepareMessageWithLang(language, "2+ Teleports Text", tpCount);
}

void SurfTimerService::Pause()
{
	if (!this->CanPause(true))
	{
		return;
	}

	bool allowPause = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowPause &= eventListeners[i]->OnPause(this->player);
	}
	if (!allowPause)
	{
		this->player->languageService->PrintChat(true, false, "Can't Pause (Generic)");
		this->player->PlayErrorSound();
		return;
	}

	this->paused = true;
	this->pausedOnLadder = this->player->GetMoveType() == MOVETYPE_LADDER;
	this->lastDuckValue = this->player->GetMoveServices()->m_flDuckAmount;
	this->lastStaminaValue = this->player->GetMoveServices()->m_flStamina;
	this->player->SetVelocity(vec3_origin);
	this->player->SetMoveType(MOVETYPE_NONE);
	this->player->GetPlayerPawn()->SetGravityScale(0);

	if (this->GetTimerRunning())
	{
		this->hasPausedInThisRun = true;
		this->lastPauseTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnPausePost(this->player);
	}
}

bool SurfTimerService::CanPause(bool showError)
{
	if (this->paused)
	{
		return false;
	}

	Vector velocity;
	this->player->GetVelocity(&velocity);

	if (this->GetTimerRunning())
	{
		if (this->hasResumedInThisRun && g_pSurfUtils->GetServerGlobals()->curtime - this->lastResumeTime < SURF_PAUSE_COOLDOWN)
		{
			if (showError)
			{
				this->player->languageService->PrintChat(true, false, "Can't Pause (Just Resumed)");
				this->player->PlayErrorSound();
			}
			return false;
		}
		else if (!(this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) && !(velocity.Length2D() == 0.0f && velocity.z == 0.0f))
		{
			if (showError)
			{
				this->player->languageService->PrintChat(true, false, "Can't Pause (Midair)");
				this->player->PlayErrorSound();
			}
			return false;
		}
	}
	return true;
}

void SurfTimerService::Resume(bool force)
{
	if (!this->paused)
	{
		return;
	}
	if (!force && !this->CanResume(true))
	{
		return;
	}

	bool allowResume = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowResume &= eventListeners[i]->OnResume(this->player);
	}
	if (!allowResume)
	{
		this->player->languageService->PrintChat(true, false, "Can't Resume (Generic)");
		this->player->PlayErrorSound();
		return;
	}

	if (this->pausedOnLadder)
	{
		this->player->SetMoveType(MOVETYPE_LADDER);
	}
	else
	{
		this->player->SetMoveType(MOVETYPE_WALK);
	}

	// GOKZ: prevent noclip exploit
	this->player->GetPlayerPawn()->m_Collision().m_CollisionGroup() = SURF_COLLISION_GROUP_STANDARD;
	this->player->GetPlayerPawn()->CollisionRulesChanged();

	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}
	this->player->GetMoveServices()->m_flDuckAmount = this->lastDuckValue;
	this->player->GetMoveServices()->m_flStamina = this->lastStaminaValue;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

bool SurfTimerService::CanResume(bool showError)
{
	if (this->GetTimerRunning() && this->hasPausedInThisRun && g_pSurfUtils->GetServerGlobals()->curtime - this->lastPauseTime < SURF_PAUSE_COOLDOWN)
	{
		if (showError)
		{
			this->player->languageService->PrintChat(true, false, "Can't Resume (Just Paused)");
			this->player->PlayErrorSound();
		}
		return false;
	}
	return true;
}

SCMD(surf_timerstopsound, SCFL_TIMER | SCFL_PREFERENCE)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->timerService->ToggleTimerStopSound();
	return MRES_SUPERCEDE;
}

SCMD_LINK(surf_tss, surf_toggletimerstopsound);

void SurfTimerService::ToggleTimerStopSound()
{
	this->shouldPlayTimerStopSound = !this->shouldPlayTimerStopSound;
	this->player->optionService->SetPreferenceBool("timerStopSound", this->shouldPlayTimerStopSound);
	this->player->languageService->PrintChat(true, false, this->shouldPlayTimerStopSound ? "Timer Stop Sound Enabled" : "Timer Stop Sound Disabled");
}

void SurfTimerService::Reset()
{
	this->timerRunning = {};
	this->currentTime = {};
	this->currentCourseGUID = 0;
	this->lastEndTime = {};
	this->lastFalseEndTime = {};
	this->lastStartSoundTime = {};
	this->lastMissedTimeSoundTime = {};
	this->validTime = {};
	this->paused = {};
	this->pausedOnLadder = {};
	this->lastPauseTime = {};
	this->hasPausedInThisRun = {};
	this->lastResumeTime = {};
	this->hasResumedInThisRun = {};
	this->lastDuckValue = {};
	this->lastStaminaValue = {};
	this->validJump = {};
	this->lastInvalidateTime = {};
	this->touchedGroundSinceTouchingStartZone = {};
	this->shouldPlayTimerStopSound = true;
}

void SurfTimerService::OnPhysicsSimulatePost()
{
	if (this->player->IsAlive() && this->GetTimerRunning() && !this->GetPaused())
	{
		this->currentTime += ENGINE_FIXED_TICK_INTERVAL;
		this->CheckMissedTime();
	}
}

void SurfTimerService::OnStartTouchGround()
{
	this->touchedGroundSinceTouchingStartZone = true;
}

void SurfTimerService::OnStopTouchGround()
{
	if (this->HasValidMoveType() && this->lastInvalidateTime != g_pSurfUtils->GetServerGlobals()->curtime)
	{
		this->validJump = true;
	}
	else
	{
		this->InvalidateJump();
	}
}

void SurfTimerService::OnChangeMoveType(MoveType_t oldMoveType)
{
	if (oldMoveType == MOVETYPE_LADDER && this->player->GetMoveType() == MOVETYPE_WALK
		&& this->lastInvalidateTime != g_pSurfUtils->GetServerGlobals()->curtime)
	{
		this->validJump = true;
	}
	else
	{
		this->InvalidateJump();
	}
	// Check if player has escaped MOVETYPE_NONE
	if (!this->paused || this->player->GetMoveType() == MOVETYPE_NONE)
	{
		return;
	}

	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

void SurfTimerService::OnTeleportToStart()
{
	this->TimerStop();
}

void SurfTimerService::OnClientDisconnect()
{
	this->TimerStop();
}

void SurfTimerService::OnPlayerSpawn()
{
	if (!this->player->GetPlayerPawn() || !this->paused)
	{
		return;
	}

	// Player has left paused state by spawning in, so resume
	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}
	this->player->GetMoveServices()->m_flDuckAmount = this->lastDuckValue;
	this->player->GetMoveServices()->m_flStamina = this->lastStaminaValue;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

void SurfTimerService::OnPlayerJoinTeam(i32 team)
{
	if (team == CS_TEAM_SPECTATOR)
	{
		this->paused = true;
		if (this->GetTimerRunning())
		{
			this->hasPausedInThisRun = true;
			this->lastPauseTime = g_pSurfUtils->GetServerGlobals()->curtime;
		}

		FOR_EACH_VEC(eventListeners, i)
		{
			eventListeners[i]->OnPausePost(this->player);
		}
	}
}

void SurfTimerService::OnPlayerDeath()
{
	this->TimerStop();
}

void SurfTimerService::OnRoundStart()
{
	SurfTimerService::TimerStopAll();
}

void SurfTimerService::OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	if (newPosition || newVelocity)
	{
		this->InvalidateJump();
	}
}

SCMD(surf_stop, SCFL_TIMER)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (player->timerService->GetTimerRunning())
	{
		player->timerService->TimerStop();
	}
	return MRES_SUPERCEDE;
}

SCMD(surf_pause, SCFL_TIMER)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->timerService->TogglePause();
	return MRES_SUPERCEDE;
}

SCMD(surf_comparelevel, SCFL_RECORD | SCFL_TIMER | SCFL_PREFERENCE | SCFL_GLOBAL)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->timerService->SetCompareTarget(args->Arg(1));
	return MRES_SUPERCEDE;
}

static_function SurfTimerService::CompareType GetCompareTypeFromString(const char *typeString)
{
	if (V_stricmp("off", typeString) == 0 || V_stricmp("none", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_NONE;
	}
	if (V_stricmp("spb", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_SPB;
	}
	if (V_stricmp("gpb", typeString) == 0 || V_stricmp("pb", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_GPB;
	}
	if (V_stricmp("sr", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_SR;
	}
	if (V_stricmp("wr", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_WR;
	}
	return SurfTimerService::CompareType::COMPARETYPE_COUNT;
}

void SurfTimerService::SetCompareTarget(const char *typeString)
{
	if (!typeString || !V_stricmp("", typeString))
	{
		this->player->languageService->PrintChat(true, false, "Compare Command Usage");
		return;
	}

	CompareType type = GetCompareTypeFromString(typeString);
	if (type == COMPARETYPE_COUNT)
	{
		this->player->languageService->PrintChat(true, false, "Compare Command Usage");
		return;
	}

	assert(type < COMPARETYPE_COUNT && type >= COMPARE_NONE);
	switch (type)
	{
		case COMPARE_NONE:
		{
			this->player->languageService->PrintChat(true, false, "Compare Disabled");
			break;
		}
		case COMPARE_SPB:
		{
			this->player->languageService->PrintChat(true, false, "Compare Server PB");
			break;
		}
		case COMPARE_GPB:
		{
			this->player->languageService->PrintChat(true, false, "Compare Global PB");
			break;
		}
		case COMPARE_SR:
		{
			this->player->languageService->PrintChat(true, false, "Compare Server Record");
			break;
		}
		case COMPARE_WR:
		{
			this->player->languageService->PrintChat(true, false, "Compare World Record");
			break;
		}
	}
	this->preferredCompareType = type;
	this->player->optionService->SetPreferenceInt("preferredCompareType", this->preferredCompareType);
	if (this->GetCourse())
	{
		this->UpdateCurrentCompareType(ToPBDataKey(Surf::mode::GetModeInfo(this->player->modeService).id, this->GetCourse()->guid));
	}
}

void SurfTimerService::UpdateCurrentCompareType(PBDataKey key)
{
	for (u8 type = this->preferredCompareType; type > COMPARE_NONE; type--)
	{
		if (this->GetCompareTargetForType((CompareType)type, key))
		{
			this->currentCompareType = (CompareType)type;
			return;
		}
	}
	this->currentCompareType = COMPARE_NONE;
}

const PBData *SurfTimerService::GetCompareTargetForType(CompareType type, PBDataKey key)
{
	switch (type)
	{
		case COMPARE_WR:
		{
			if (SurfTimerService::wrCache.find(key) != SurfTimerService::wrCache.end())
			{
				return &SurfTimerService::wrCache[key];
			}
			break;
		}
		case COMPARE_SR:
		{
			if (SurfTimerService::srCache.find(key) != SurfTimerService::srCache.end())
			{
				return &SurfTimerService::srCache[key];
			}
			break;
		}
		case COMPARE_GPB:
		{
			if (SurfTimerService::globalPBCache.find(key) != SurfTimerService::globalPBCache.end())
			{
				return &this->globalPBCache[key];
			}
			break;
		}
		case COMPARE_SPB:
		{
			if (SurfTimerService::localPBCache.find(key) != SurfTimerService::localPBCache.end())
			{
				return &this->localPBCache[key];
			}
			break;
		}
	}
	return nullptr;
}

const PBData *SurfTimerService::GetCompareTarget(PBDataKey key)
{
	switch (this->currentCompareType)
	{
		case COMPARE_WR:
		{
			if (SurfTimerService::wrCache.find(key) != SurfTimerService::wrCache.end())
			{
				return &SurfTimerService::wrCache[key];
			}
			break;
		}
		case COMPARE_SR:
		{
			if (SurfTimerService::srCache.find(key) != SurfTimerService::srCache.end())
			{
				return &SurfTimerService::srCache[key];
			}
			break;
		}
		case COMPARE_GPB:
		{
			if (SurfTimerService::globalPBCache.find(key) != SurfTimerService::globalPBCache.end())
			{
				return &this->globalPBCache[key];
			}
			break;
		}
		case COMPARE_SPB:
		{
			if (SurfTimerService::localPBCache.find(key) != SurfTimerService::localPBCache.end())
			{
				return &this->localPBCache[key];
			}
			break;
		}
	}
	return nullptr;
}

void SurfTimerService::ClearRecordCache()
{
	SurfTimerService::srCache.clear();
	SurfTimerService::wrCache.clear();
	for (i32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		if (player && player->timerService)
		{
			player->timerService->ClearPBCache();
		}
	}
}

void SurfTimerService::UpdateLocalRecordCache()
{
	auto onQuerySuccess = [](std::vector<ISQLQuery *> queries)
	{
		ISQLResult *result = queries[0]->GetResultSet();
		if (result && result->GetRowCount() > 0)
		{
			while (result->FetchRow())
			{
				auto modeInfo = Surf::mode::GetModeInfoFromDatabaseID(result->GetInt(2));
				if (modeInfo.databaseID < 0)
				{
					continue;
				}
				const SurfCourseDescriptor *course = Surf::course::GetCourseByLocalCourseID(result->GetInt(1));
				if (!course)
				{
					continue;
				}
				SurfTimerService::InsertRecordToCache(result->GetFloat(0), course, modeInfo.id, false, result->GetString(3));
			}
		}
	};
	SurfDatabaseService::QueryAllRecords(g_pSurfUtils->GetCurrentMapName(), onQuerySuccess, SurfDatabaseService::OnGenericTxnFailure);
}

void SurfTimerService::InsertRecordToCache(f64 time, const SurfCourseDescriptor *course, PluginId modeID, bool global, CUtlString metadata)
{
	PBData &pb = global ? SurfTimerService::wrCache[ToPBDataKey(modeID, course->guid)] : SurfTimerService::srCache[ToPBDataKey(modeID, course->guid)];

	pb.overall.pbTime = time;
	KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);
	CUtlString error = "";
	if (metadata.IsEmpty())
	{
		return;
	}
	LoadKV3FromJSON(&kv, &error, metadata.Get(), "");
	if (!error.IsEmpty())
	{
		META_CONPRINTF("[Surf::Timer] Failed to insert PB to cache due to metadata error: %s\n", error.Get());
		return;
	}

	KeyValues3 *data = kv.FindMember("cpZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->checkpointCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbCpZoneTimes[i] = time;
		}
	}

	data = kv.FindMember("stageZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->stageCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbStageZoneTimes[i] = time;
		}
	}
}

void SurfTimerService::ClearPBCache()
{
	this->localPBCache.clear();
}

const PBData *SurfTimerService::GetGlobalCachedPB(const SurfCourseDescriptor *course, PluginId modeID)
{
	PBDataKey key = ToPBDataKey(modeID, course->guid);

	if (this->globalPBCache.find(key) == this->globalPBCache.end())
	{
		return nullptr;
	}

	return &this->globalPBCache[key];
}

void SurfTimerService::InsertPBToCache(f64 time, const SurfCourseDescriptor *course, PluginId modeID, bool global, CUtlString metadata, f64 points)
{
	PBData &pb = global ? this->globalPBCache[ToPBDataKey(modeID, course->guid)] : this->localPBCache[ToPBDataKey(modeID, course->guid)];

	pb.overall.points = points;
	pb.overall.pbTime = time;
	KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);
	CUtlString error = "";
	if (metadata.IsEmpty())
	{
		return;
	}
	LoadKV3FromJSON(&kv, &error, metadata.Get(), "");
	if (!error.IsEmpty())
	{
		META_CONPRINTF("[Surf::Timer] Failed to insert server record to cache due to metadata error: %s\n", error.Get());
		return;
	}

	KeyValues3 *data = kv.FindMember("cpZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->checkpointCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbCpZoneTimes[i] = time;
		}
	}

	data = kv.FindMember("stageZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->stageCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbStageZoneTimes[i] = time;
		}
	}
}

void SurfTimerService::CheckMissedTime()
{
	const SurfCourseDescriptor *course = this->GetCourse();
	// No active course, the timer is not running or if we already announce late PBs.
	if (!course || !this->GetTimerRunning() || !this->shouldAnnounceMissedTime)
	{
		return;
	}
	// No comparison available for styled runs.
	if (this->player->styleServices.Count() > 0)
	{
		return;
	}
	auto modeInfo = Surf::mode::GetModeInfo(this->player->modeService->GetModeName());

	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);

	// Check if there is personal best data for this mode and course.
	auto pb = this->GetCompareTarget(key);
	if (!pb)
	{
		return;
	}

	if (this->shouldAnnounceMissedTime && pb->overall.pbTime > 0 && this->GetTime() > pb->overall.pbTime)
	{
		CUtlString timeText = SurfTimerService::FormatTime(pb->overall.pbTime);
		this->player->languageService->PrintChat(true, false, missedTimeKeys[this->currentCompareType], timeText.Get());
		this->shouldAnnounceMissedTime = false;
		this->PlayMissedTimeSound();
	}
}

void SurfTimerService::ShowCheckpointText(u32 currentCheckpoint)
{
	const SurfCourseDescriptor *course = this->GetCourse();
	// No active course so we can't compare anything.
	if (!course)
	{
		return;
	}
	// No comparison available for styled runs.
	if (this->player->styleServices.Count() > 0)
	{
		return;
	}

	CUtlString time;
	std::string pbDiff = "";

	time = SurfTimerService::FormatTime(this->cpZoneTimes[currentCheckpoint - 1]);
	if (this->lastCheckpoint != 0)
	{
		f64 diff = this->cpZoneTimes[currentCheckpoint - 1] - this->cpZoneTimes[this->lastCheckpoint - 1];
		CUtlString splitTime = SurfTimerService::FormatDiffTime(diff);
		splitTime.Format(" {grey}({default}%s{grey})", splitTime.Get());
		time.Append(splitTime.Get());
	}

	auto modeInfo = Surf::mode::GetModeInfo(this->player->modeService->GetModeName());
	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);

	// Check if there is personal best data for this mode and course.
	const PBData *pb = this->GetCompareTarget(key);
	if (pb)
	{
		if (pb->overall.pbCpZoneTimes[currentCheckpoint - 1] > 0)
		{
			f64 diff = this->cpZoneTimes[currentCheckpoint - 1] - pb->overall.pbCpZoneTimes[currentCheckpoint - 1];
			CUtlString diffText = SurfTimerService::FormatDiffTime(diff);
			diffText.Format("{grey}%s%s{grey}", diff < 0 ? "{green}" : "{lightred}", diffText.Get());
			pbDiff = this->player->languageService->PrepareMessage(diffTextKeys[this->currentCompareType], diffText.Get());
		}
	}

	this->player->languageService->PrintChat(true, false, "Course Checkpoint Reached", currentCheckpoint, time.Get(), pbDiff.c_str());
}

void SurfTimerService::ShowStageText()
{
	const SurfCourseDescriptor *course = this->GetCourse();
	// No active course so we can't compare anything.
	if (!course)
	{
		return;
	}
	// No comparison available for styled runs.
	if (this->player->styleServices.Count() > 0)
	{
		return;
	}

	CUtlString time;
	std::string pbDiff = "";

	time = SurfTimerService::FormatTime(this->stageZoneTimes[this->currentStage - 1]);

	auto modeInfo = Surf::mode::GetModeInfo(this->player->modeService->GetModeName());
	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);

	// Check if there is personal best data for this mode and course.
	const PBData *pb = this->GetCompareTarget(key);
	if (pb)
	{
		if (pb->overall.pbStageZoneTimes[this->currentStage - 1] > 0)
		{
			f64 diff = this->stageZoneTimes[this->currentStage - 1] - pb->overall.pbStageZoneTimes[this->currentStage - 1];
			CUtlString diffText = SurfTimerService::FormatDiffTime(diff);
			diffText.Format("{grey}%s%s{grey}", diff < 0 ? "{green}" : "{lightred}", diffText.Get());
			pbDiff = this->player->languageService->PrepareMessage(diffTextKeys[this->currentCompareType], diffText.Get());
		}
	}

	this->player->languageService->PrintChat(true, false, "Course Stage Reached", this->currentStage, time.Get(), pbDiff.c_str());
}

CUtlString SurfTimerService::GetCurrentRunMetadata()
{
	KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);

	KeyValues3 *cpZoneTimesKV = kv.FindOrCreateMember("cpZoneTimes");
	cpZoneTimesKV->SetToEmptyArray();
	FOR_EACH_VEC(this->cpZoneTimes, i)
	{
		KeyValues3 *time = cpZoneTimesKV->ArrayAddElementToTail();
		time->SetDouble(this->cpZoneTimes[i]);
	}

	KeyValues3 *stageZoneTimesKV = kv.FindOrCreateMember("stageZoneTimes");
	FOR_EACH_VEC(this->stageZoneTimes, i)
	{
		KeyValues3 *time = stageZoneTimesKV->ArrayAddElementToTail();
		time->SetDouble(this->stageZoneTimes[i]);
	}

	CUtlString result, error;
	if (SaveKV3AsJSON(&kv, &error, &result))
	{
		return result;
	}
	META_CONPRINTF("[Surf::Timer] Failed to obtain current run's metadata! (%s)\n", error.Get());
	return "";
}

void SurfTimerService::UpdateLocalPBCache()
{
	CPlayerUserId uid = player->GetClient()->GetUserID();

	auto onQuerySuccess = [uid](std::vector<ISQLQuery *> queries)
	{
		SurfPlayer *pl = g_pSurfPlayerManager->ToPlayer(uid);
		if (!pl)
		{
			return;
		}
		ISQLResult *result = queries[0]->GetResultSet();
		if (result && result->GetRowCount() > 0)
		{
			while (result->FetchRow())
			{
				auto modeInfo = Surf::mode::GetModeInfoFromDatabaseID(result->GetInt(2));
				if (modeInfo.databaseID < 0)
				{
					continue;
				}
				const SurfCourseDescriptor *course = Surf::course::GetCourseByLocalCourseID(result->GetInt(1));
				if (!course)
				{
					continue;
				}
				pl->timerService->InsertPBToCache(result->GetFloat(0), course, modeInfo.id, false, result->GetString(3));
			}
		}
	};
	SurfDatabaseService::QueryAllPBs(player->GetSteamId64(), g_pSurfUtils->GetCurrentMapName(), onQuerySuccess,
									 SurfDatabaseService::OnGenericTxnFailure);
}

std::string SurfTimerService::GetStartSpeedText(const char *language)
{
	Vector velocity, baseVelocity;
	this->player->GetVelocity(&velocity);
	this->player->GetBaseVelocity(&baseVelocity);
	velocity += baseVelocity;

	float startSpeed = velocity.Length2D();
	return SurfLanguageService::PrepareMessageWithLang(language, "Start Speed", startSpeed);
}

void SurfTimerService::Init()
{
	SurfDatabaseService::RegisterEventListener(&databaseEventListener);
	SurfOptionService::RegisterEventListener(&optionEventListener);
}

void SurfTimerService::OnPlayerPreferencesLoaded()
{
	if (this->player->optionService->GetPreferenceInt("preferredCompareType", COMPARE_GPB) > COMPARETYPE_COUNT)
	{
		this->preferredCompareType = COMPARE_GPB;
		return;
	}
	this->preferredCompareType = (CompareType)this->player->optionService->GetPreferenceInt("preferredCompareType", COMPARE_GPB);
	this->shouldPlayTimerStopSound = this->player->optionService->GetPreferenceBool("timerStopSound", true);
}

void SurfDatabaseServiceEventListener_Timer::OnMapSetup()
{
	// TODO: find a better way to do this, we now call SetupLocalCourses after all trigger_multiple spawns
	// Surf::course::SetupLocalCourses();
	SurfTimerService::UpdateLocalRecordCache();
}

void SurfDatabaseServiceEventListener_Timer::OnClientSetup(Player *player, u64 steamID64, bool isCheater)
{
	SurfPlayer *SurfPlayer = g_pSurfPlayerManager->ToSurfPlayer(player);
	SurfPlayer->timerService->UpdateLocalPBCache();
}
