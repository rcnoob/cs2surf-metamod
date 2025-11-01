#include "surf_trigger.h"
#include "surf/mode/surf_mode.h"
#include "surf/style/surf_style.h"
#include "surf/checkpoint/surf_checkpoint.h"
#include "surf/noclip/surf_noclip.h"
#include "surf/timer/surf_timer.h"
#include "surf/language/surf_language.h"

/*
	Note: Whether touching is allowed is set determined by the mode, while Mapping API effects will be applied after touching events.
*/

// Whether we allow interaction from happening.
bool SurfTriggerService::OnTriggerStartTouchPre(CBaseTrigger *trigger)
{
	bool retValue = this->player->modeService->OnTriggerStartTouch(trigger);
	FOR_EACH_VEC(this->player->styleServices, i)
	{
		retValue &= this->player->styleServices[i]->OnTriggerStartTouch(trigger);
	}
	return retValue;
}

bool SurfTriggerService::OnTriggerTouchPre(CBaseTrigger *trigger, TriggerTouchTracker tracker)
{
	bool retValue = this->player->modeService->OnTriggerTouch(trigger);
	FOR_EACH_VEC(this->player->styleServices, i)
	{
		retValue &= this->player->styleServices[i]->OnTriggerTouch(trigger);
	}
	return retValue;
}

bool SurfTriggerService::OnTriggerEndTouchPre(CBaseTrigger *trigger, TriggerTouchTracker tracker)
{
	bool retValue = this->player->modeService->OnTriggerEndTouch(trigger);
	FOR_EACH_VEC(this->player->styleServices, i)
	{
		retValue &= this->player->styleServices[i]->OnTriggerEndTouch(trigger);
	}
	return retValue;
}

// Mapping API stuff.
void SurfTriggerService::OnTriggerStartTouchPost(CBaseTrigger *trigger, TriggerTouchTracker tracker)
{
	if (!tracker.surfTrigger || !trigger->PassesTriggerFilters(this->player->GetPlayerPawn()))
	{
		return;
	}
	this->OnMappingApiTriggerStartTouchPost(tracker);
}

void SurfTriggerService::OnTriggerTouchPost(CBaseTrigger *trigger, TriggerTouchTracker tracker)
{
	if (!tracker.surfTrigger || !trigger->PassesTriggerFilters(this->player->GetPlayerPawn()))
	{
		return;
	}
	this->OnMappingApiTriggerTouchPost(tracker);
}

void SurfTriggerService::OnTriggerEndTouchPost(CBaseTrigger *trigger, TriggerTouchTracker tracker)
{
	if (!tracker.surfTrigger || !trigger->PassesTriggerFilters(this->player->GetPlayerPawn()))
	{
		return;
	}
	this->OnMappingApiTriggerEndTouchPost(tracker);
}

void SurfTriggerService::AddPushEvent(const SurfTrigger *trigger)
{
	f32 curtime = g_pSurfUtils->GetGlobals()->curtime;
	PushEvent event {trigger, curtime + trigger->push.delay};
	if (this->pushEvents.Find(event) == -1)
	{
		this->pushEvents.AddToTail(event);
	}
}

void SurfTriggerService::CleanupPushEvents()
{
	f32 frametime = g_pSurfUtils->GetGlobals()->frametime;
	// Don't remove push events since these push events are not fired yet.
	if (frametime == 0.0f)
	{
		return;
	}
	f32 curtime = g_pSurfUtils->GetGlobals()->curtime;
	FOR_EACH_VEC_BACK(this->pushEvents, i)
	{
		if (!this->pushEvents[i].applied)
		{
			continue;
		}
		if (curtime - frametime >= this->pushEvents[i].pushTime + this->pushEvents[i].source->push.cooldown
			|| curtime < this->pushEvents[i].pushTime + this->pushEvents[i].source->push.cooldown)
		{
			this->pushEvents.Remove(i);
		}
	}
}

void SurfTriggerService::ApplyPushes()
{
	f32 frametime = g_pSurfUtils->GetGlobals()->frametime;
	// There's no point applying any push if player isn't going to move anyway.
	if (frametime == 0.0f)
	{
		return;
	}
	f32 curtime = g_pSurfUtils->GetGlobals()->curtime;
	bool setSpeed[3] {};

	if (this->pushEvents.Count() == 0)
	{
		return;
	}
	bool useBaseVelocity = this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND && this->player->processingMovement;
	FOR_EACH_VEC(this->pushEvents, i)
	{
		if (curtime - frametime >= this->pushEvents[i].pushTime || curtime < this->pushEvents[i].pushTime || this->pushEvents[i].applied)
		{
			continue;
		}
		this->pushEvents[i].applied = true;
		auto &push = this->pushEvents[i].source->push;
		for (u32 i = 0; i < 3; i++)
		{
			Vector vel;
			if (useBaseVelocity && i != 2)
			{
				this->player->GetBaseVelocity(&vel);
			}
			else
			{
				this->player->GetVelocity(&vel);
			}
			// Set speed overrides add speed.
			if (push.setSpeed[i])
			{
				vel[i] = push.impulse[i];
				setSpeed[i] = true;
			}
			else if (!setSpeed[i])
			{
				vel[i] += push.impulse[i];
			}
			// If we are pushing the player up, make sure they cannot re-ground themselves.
			if (i == 2 && vel[i] > 0 && useBaseVelocity)
			{
				this->player->GetPlayerPawn()->m_hGroundEntity().FromIndex(INVALID_EHANDLE_INDEX);
				this->player->GetPlayerPawn()->m_fFlags() &= ~FL_ONGROUND;
				this->player->currentMoveData->m_groundNormal = vec3_origin;
			}
			if (useBaseVelocity && i != 2)
			{
				this->player->SetBaseVelocity(vel);
				this->player->GetPlayerPawn()->m_fFlags() |= FL_BASEVELOCITY;
			}
			else
			{
				this->player->SetVelocity(vel);
			}
		}
	}
	// Try to nullify velocity if needed.
	if (useBaseVelocity)
	{
		Vector velocity, newVelocity;
		this->player->GetVelocity(&velocity);
		newVelocity = velocity;
		for (u32 i = 0; i < 2; i++)
		{
			if (setSpeed[i])
			{
				newVelocity[i] = 0;
			}
		}
		if (velocity != newVelocity)
		{
			this->player->SetVelocity(newVelocity);
		}
	}
}

void SurfTriggerService::OnMappingApiTriggerStartTouchPost(TriggerTouchTracker tracker)
{
	const SurfTrigger *trigger = tracker.surfTrigger;
	const SurfCourseDescriptor *course = Surf::mapapi::GetCourseDescriptorFromTrigger(trigger);
	if (Surf::mapapi::IsTimerTrigger(trigger->type) && !course)
	{
		return;
	}

	switch (trigger->type)
	{
		case SURFTRIGGER_MODIFIER:
		{
			SurfMapModifier modifier = trigger->modifier;
			this->modifiers.disablePausingCount += modifier.disablePausing ? 1 : 0;
			this->modifiers.disableCheckpointsCount += modifier.disableCheckpoints ? 1 : 0;
			this->modifiers.disableTeleportsCount += modifier.disableTeleports ? 1 : 0;
		}
		break;

		case SURFTRIGGER_RESET_CHECKPOINTS:
		{
			if (this->player->timerService->GetTimerRunning())
			{
				if (this->player->checkpointService->GetCheckpointCount())
				{
					this->player->languageService->PrintChat(true, false, "Checkpoints Cleared By Map");
				}
				this->player->checkpointService->ResetCheckpoints(true, false);
			}
		};
		break;

		case SURFTRIGGER_SINGLE_BHOP_RESET:
		{
			this->ResetBhopState();
		}
		break;

		case SURFTRIGGER_ZONE_START:
		{
			this->player->checkpointService->ResetCheckpoints();
			this->player->timerService->StartZoneStartTouch(course);
		}
		break;

		case SURFTRIGGER_ZONE_END:
		{
			this->player->timerService->TimerEnd(course);
		}
		break;

		case SURFTRIGGER_ZONE_SPLIT:
		{
			this->player->timerService->SplitZoneStartTouch(course, trigger->zone.number);
		}
		break;

		case SURFTRIGGER_ZONE_CHECKPOINT:
		{
			this->player->timerService->CheckpointZoneStartTouch(course, trigger->zone.number);
		}
		break;

		case SURFTRIGGER_ZONE_STAGE:
		{
			this->player->timerService->StageZoneStartTouch(course, trigger->zone.number);
		}
		break;

		case SURFTRIGGER_TELEPORT:
		case SURFTRIGGER_MULTI_BHOP:
		case SURFTRIGGER_SINGLE_BHOP:
		case SURFTRIGGER_SEQUENTIAL_BHOP:
		{
			if (Surf::mapapi::IsBhopTrigger(trigger->type))
			{
				this->bhopTouchCount++;
			}
		}
		break;
		case SURFTRIGGER_PUSH:
		{
			if (tracker.surfTrigger->push.pushConditions & SurfMapPush::SURF_PUSH_START_TOUCH)
			{
				this->AddPushEvent(trigger);
			}
		}
		break;
		default:
			break;
	}
}

void SurfTriggerService::OnMappingApiTriggerTouchPost(TriggerTouchTracker tracker)
{
	bool shouldRecheckTriggers = false;
	switch (tracker.surfTrigger->type)
	{
		case SURFTRIGGER_MODIFIER:
		{
			this->TouchModifierTrigger(tracker);
		}
		break;

		case SURFTRIGGER_ANTI_BHOP:
		{
			this->TouchAntibhopTrigger(tracker);
		}
		break;

		case SURFTRIGGER_TELEPORT:
		case SURFTRIGGER_MULTI_BHOP:
		case SURFTRIGGER_SINGLE_BHOP:
		case SURFTRIGGER_SEQUENTIAL_BHOP:
		{
			this->TouchTeleportTrigger(tracker);
		}
		break;
		case SURFTRIGGER_PUSH:
		{
			this->TouchPushTrigger(tracker);
		}
		break;
	}
}

void SurfTriggerService::OnMappingApiTriggerEndTouchPost(TriggerTouchTracker tracker)
{
	const SurfCourseDescriptor *course = Surf::mapapi::GetCourseDescriptorFromTrigger(tracker.surfTrigger);
	if (Surf::mapapi::IsTimerTrigger(tracker.surfTrigger->type) && !course)
	{
		return;
	}

	switch (tracker.surfTrigger->type)
	{
		case SURFTRIGGER_MODIFIER:
		{
			SurfMapModifier modifier = tracker.surfTrigger->modifier;
			this->modifiers.disablePausingCount -= modifier.disablePausing ? 1 : 0;
			this->modifiers.disableCheckpointsCount -= modifier.disableCheckpoints ? 1 : 0;
			this->modifiers.disableTeleportsCount -= modifier.disableTeleports ? 1 : 0;

			assert(this->modifiers.disablePausingCount >= 0);
			assert(this->modifiers.disableCheckpointsCount >= 0);
			assert(this->modifiers.disableTeleportsCount >= 0);
			assert(this->modifiers.forcedDuckCount >= 0);
			assert(this->modifiers.forcedUnduckCount >= 0);
		}
		break;

		case SURFTRIGGER_ZONE_START:
		{
			this->player->checkpointService->ResetCheckpoints();
			this->player->timerService->StartZoneEndTouch(course);
		}
		break;

		case SURFTRIGGER_TELEPORT:
		case SURFTRIGGER_MULTI_BHOP:
		case SURFTRIGGER_SINGLE_BHOP:
		case SURFTRIGGER_SEQUENTIAL_BHOP:
		{
			if (Surf::mapapi::IsBhopTrigger(tracker.surfTrigger->type))
			{
				this->bhopTouchCount--;
			}
		}
		break;
		case SURFTRIGGER_PUSH:
		{
			if (tracker.surfTrigger->push.pushConditions & SurfMapPush::SURF_PUSH_END_TOUCH)
			{
				this->AddPushEvent(tracker.surfTrigger);
			}
		}
		break;
		default:
			break;
	}
}
