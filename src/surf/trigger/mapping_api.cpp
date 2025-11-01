#include "surf_trigger.h"
#include "surf/checkpoint/surf_checkpoint.h"
#include "surf/language/surf_language.h"
#include "surf/mode/surf_mode.h"
#include "surf/style/surf_style.h"
#include "surf/timer/surf_timer.h"

void SurfTriggerService::ResetBhopState()
{
	this->lastTouchedSingleBhop = CEntityHandle();
	// all hail fixed buffers
	this->lastTouchedSequentialBhops = CSequentialBhopBuffer();
}

void SurfTriggerService::UpdateModifiersInternal()
{
	if (this->modifiers.enableSlideCount > 0)
	{
		this->ApplySlide();
	}
	else
	{
		this->CancelSlide();
	}

	if (this->antiBhopActive)
	{
		this->ApplyAntiBhop();
	}
	else
	{
		this->CancelAntiBhop();
	}

	if (this->modifiers.forcedDuckCount > 0)
	{
		this->ApplyForcedDuck();
	}
	else if (this->lastModifiers.forcedDuckCount > 0)
	{
		this->CancelForcedDuck();
	}

	if (this->modifiers.forcedUnduckCount > 0)
	{
		this->ApplyForcedUnduck();
	}
	else if (this->lastModifiers.forcedUnduckCount > 0)
	{
		this->CancelForcedUnduck();
	}
}

bool SurfTriggerService::InAntiPauseArea()
{
	return this->modifiers.disablePausingCount > 0;
}

bool SurfTriggerService::InBhopTriggers()
{
	FOR_EACH_VEC(this->triggerTrackers, i)
	{
		bool justTouched = g_pSurfUtils->GetServerGlobals()->curtime - this->triggerTrackers[i].startTouchTime < 0.15f;
		if (justTouched && this->triggerTrackers[i].isPossibleLegacyBhopTrigger)
		{
			return true;
		}
	}
	return this->bhopTouchCount > 0;
}

bool SurfTriggerService::InAntiCpArea()
{
	return this->modifiers.disableCheckpointsCount > 0;
}

bool SurfTriggerService::CanTeleportToCheckpoints()
{
	return this->modifiers.disableTeleportsCount <= 0;
}

void SurfTriggerService::TouchModifierTrigger(TriggerTouchTracker tracker)
{
	const SurfTrigger *trigger = tracker.surfTrigger;

	if (trigger->modifier.gravity != 1)
	{
		// No gravity while paused.
		if (this->player->timerService->GetPaused())
		{
			this->player->GetPlayerPawn()->SetGravityScale(0);
			return;
		}
		this->player->GetPlayerPawn()->SetGravityScale(trigger->modifier.gravity);
	}
	this->modifiers.jumpFactor = trigger->modifier.jumpFactor;
}

void SurfTriggerService::TouchAntibhopTrigger(TriggerTouchTracker tracker)
{
	f32 timeOnGround = g_pSurfUtils->GetServerGlobals()->curtime - this->player->landingTimeServer;
	if (tracker.surfTrigger->antibhop.time == 0                          // No jump trigger
		|| timeOnGround <= tracker.surfTrigger->antibhop.time            // Haven't touched the trigger for long enough
		|| (this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) == 0) // Not on the ground (for prediction)
	{
		this->antiBhopActive = true;
	}
}

bool SurfTriggerService::TouchTeleportTrigger(TriggerTouchTracker tracker)
{
	bool shouldTeleport = false;

	bool isBhopTrigger = Surf::mapapi::IsBhopTrigger(tracker.surfTrigger->type);
	// Do not teleport the player if it's a bhop trigger and they are not on the ground.
	if (isBhopTrigger && (this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) == 0)
	{
		return false;
	}

	CEntityHandle destinationHandle = GameEntitySystem()->FindFirstEntityHandleByName(tracker.surfTrigger->teleport.destination);
	CBaseEntity *destination = dynamic_cast<CBaseEntity *>(GameEntitySystem()->GetEntityInstance(destinationHandle));
	if (!destinationHandle.IsValid() || !destination)
	{
		META_CONPRINTF("Invalid teleport destination \"%s\" on trigger with hammerID %i.\n", tracker.surfTrigger->teleport.destination,
					   tracker.surfTrigger->hammerId);
		return false;
	}

	Vector destOrigin = destination->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
	QAngle destAngles = destination->m_CBodyComponent()->m_pSceneNode()->m_angRotation();
	CBaseEntity *trigger = dynamic_cast<CBaseTrigger *>(GameEntitySystem()->GetEntityInstance(tracker.surfTrigger->entity));
	Vector triggerOrigin = Vector(0, 0, 0);
	if (trigger)
	{
		triggerOrigin = trigger->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
	}

	// NOTE: We only use the trigger's origin if we're using a relative destination, so if
	// we're not using a relative destination and don't have it, then it's fine.
	// TODO: Can this actually happen? If the trigger is touched then the entity must be valid.
	if (!trigger && tracker.surfTrigger->teleport.relative)
	{
		return false;
	}

	if (isBhopTrigger && (this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND))
	{
		f32 effectiveStartTouchTime = MAX(this->player->landingTimeServer, tracker.startTouchTime);
		f32 touchingTime = g_pSurfUtils->GetServerGlobals()->curtime - effectiveStartTouchTime;
		if (touchingTime > tracker.surfTrigger->teleport.delay)
		{
			shouldTeleport = true;
		}
		else if (tracker.surfTrigger->type == SURFTRIGGER_SINGLE_BHOP)
		{
			shouldTeleport = this->lastTouchedSingleBhop == tracker.surfTrigger->entity;
		}
		else if (tracker.surfTrigger->type == SURFTRIGGER_SEQUENTIAL_BHOP)
		{
			for (i32 i = 0; i < this->lastTouchedSequentialBhops.GetReadAvailable(); i++)
			{
				CEntityHandle handle = CEntityHandle();
				if (!this->lastTouchedSequentialBhops.Peek(&handle, i))
				{
					assert(0);
					break;
				}
				if (handle == tracker.surfTrigger->entity)
				{
					shouldTeleport = true;
					break;
				}
			}
		}
	}
	else if (tracker.surfTrigger->type == SURFTRIGGER_TELEPORT)
	{
		f32 touchingTime = g_pSurfUtils->GetServerGlobals()->curtime - tracker.startTouchTime;
		shouldTeleport = touchingTime > tracker.surfTrigger->teleport.delay || tracker.surfTrigger->teleport.delay <= 0;
	}

	if (!shouldTeleport)
	{
		return false;
	}

	bool shouldReorientPlayer = tracker.surfTrigger->teleport.reorientPlayer && destAngles[YAW] != 0;
	Vector up = Vector(0, 0, 1);
	Vector finalOrigin = destOrigin;

	if (tracker.surfTrigger->teleport.relative)
	{
		Vector playerOrigin;
		this->player->GetOrigin(&playerOrigin);
		Vector playerOffsetFromTrigger = playerOrigin - triggerOrigin;

		if (shouldReorientPlayer)
		{
			VectorRotate(playerOffsetFromTrigger, QAngle(0, destAngles[YAW], 0), playerOffsetFromTrigger);
		}

		finalOrigin = destOrigin + playerOffsetFromTrigger;
	}
	QAngle finalPlayerAngles;
	this->player->GetAngles(&finalPlayerAngles);
	Vector finalVelocity;
	this->player->GetVelocity(&finalVelocity);
	if (shouldReorientPlayer)
	{
		// TODO: BUG: sometimes when getting reoriented and holding a movement key
		//  the player's speed will get reduced, almost like velocity rotation
		//  and angle rotation is out of sync leading to counterstrafing.
		// Maybe we should check m_nHighestGeneratedServerViewAngleChangeIndex for angles overridding...
		VectorRotate(finalVelocity, QAngle(0, destAngles[YAW], 0), finalVelocity);
		finalPlayerAngles[YAW] -= destAngles[YAW];
		this->player->SetAngles(finalPlayerAngles);
	}
	else if (!tracker.surfTrigger->teleport.reorientPlayer && tracker.surfTrigger->teleport.useDestinationAngles)
	{
		this->player->SetAngles(destAngles);
	}

	if (tracker.surfTrigger->teleport.resetSpeed)
	{
		this->player->SetVelocity(vec3_origin);
	}
	else
	{
		this->player->SetVelocity(finalVelocity);
	}

	// We need to call teleport hook because we don't use teleport function directly.
	if (this->player->processingMovement && this->player->currentMoveData)
	{
		this->player->OnTeleport(&finalOrigin, nullptr, nullptr);
	}
	this->player->SetOrigin(finalOrigin);

	return true;
}

void SurfTriggerService::TouchPushTrigger(TriggerTouchTracker tracker)
{
	u32 pushConditions = tracker.surfTrigger->push.pushConditions;
	// clang-format off
	if (pushConditions & SurfMapPush::SURF_PUSH_TOUCH
		|| (this->player->IsButtonNewlyPressed(IN_ATTACK) && pushConditions & SurfMapPush::SURF_PUSH_ATTACK)
		|| (this->player->IsButtonNewlyPressed(IN_ATTACK2) && pushConditions & SurfMapPush::SURF_PUSH_ATTACK2)
		|| (this->player->IsButtonNewlyPressed(IN_JUMP) && pushConditions & SurfMapPush::SURF_PUSH_JUMP_BUTTON)
		|| (this->player->IsButtonNewlyPressed(IN_USE) && pushConditions & SurfMapPush::SURF_PUSH_USE))
	// clang-format on
	{
		this->AddPushEvent(tracker.surfTrigger);
	}
}

void SurfTriggerService::ApplySlide(bool replicate)
{
	const CVValue_t *aaValue = player->GetCvarValueFromModeStyles("sv_airaccelerate");
	const CVValue_t newAA = aaValue->m_fl32Value * 4.0f;
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_standable_normal", "2", replicate);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_walkable_normal", "2", replicate);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_airaccelerate", &newAA, replicate);
}

void SurfTriggerService::CancelSlide(bool replicate)
{
	const CVValue_t *standableValue = player->GetCvarValueFromModeStyles("sv_standable_normal");
	const CVValue_t *walkableValue = player->GetCvarValueFromModeStyles("sv_walkable_normal");
	const CVValue_t *aaValue = player->GetCvarValueFromModeStyles("sv_airaccelerate");
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_airaccelerate", aaValue, replicate);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_standable_normal", standableValue, replicate);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_walkable_normal", walkableValue, replicate);
}

void SurfTriggerService::ApplyAntiBhop(bool replicate)
{
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_jump_spam_penalty_time", "999999.9", replicate);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_autobunnyhopping", "false", replicate);
	player->GetMoveServices()->m_bOldJumpPressed() = true;
}

void SurfTriggerService::CancelAntiBhop(bool replicate)
{
	const CVValue_t *spamModeValue = player->GetCvarValueFromModeStyles("sv_jump_spam_penalty_time");
	const CVValue_t *autoBhopValue = player->GetCvarValueFromModeStyles("sv_autobunnyhopping");
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_jump_spam_penalty_time", spamModeValue, replicate);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_autobunnyhopping", autoBhopValue, replicate);
}

void SurfTriggerService::ApplyForcedDuck()
{
	this->player->GetMoveServices()->m_bDuckOverride(true);
}

void SurfTriggerService::CancelForcedDuck()
{
	this->player->GetMoveServices()->m_bDuckOverride(false);
}

void SurfTriggerService::ApplyForcedUnduck()
{
	// Set crouch time to a very large value so that the player cannot reduck.
	this->player->GetMoveServices()->m_flLastDuckTime(100000.0f);
	// This needs to be done every tick just since the player can be in spots that are not unduckable (eg. crouch tunnels)
	this->player->GetPlayerPawn()->m_fFlags(this->player->GetPlayerPawn()->m_fFlags() & ~FL_DUCKING);
}

void SurfTriggerService::CancelForcedUnduck()
{
	this->player->GetMoveServices()->m_flLastDuckTime(0.0f);
}

void SurfTriggerService::ApplyJumpFactor(bool replicate)
{
	const CVValue_t *impulseModeValue = player->GetCvarValueFromModeStyles("sv_jump_impulse");
	const CVValue_t newImpulseValue = (impulseModeValue->m_fl32Value * this->modifiers.jumpFactor);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_jump_impulse", &newImpulseValue, replicate);

	const CVValue_t *jumpCostValue = player->GetCvarValueFromModeStyles("sv_staminajumpcost");
	const CVValue_t newJumpCostValue = (jumpCostValue->m_fl32Value / this->modifiers.jumpFactor);
	utils::SetConVarValue(player->GetPlayerSlot(), "sv_staminajumpcost", &newJumpCostValue, replicate);
}
