#include "surf_mode.h"
#include "sdk/datatypes.h"

CConVarRef<f32> sv_standable_normal("sv_standable_normal");

// The player that is currently running TryPlayerMove or CategorizePosition.
static_global SurfModeService *tracingModeService {};

// Every hull trace done by the game's movement code goes through this function.
static KHook::Return<void> TracePlayerBBoxPost(void *traceCache, trace_t *pm, const Vector *start, const Vector *end, const bbox_t *bounds,
											   CTraceFilter *filter)
{
	if (tracingModeService)
	{
		tracingModeService->OnTracePlayerBBoxPost(Ray_t(bounds->mins, bounds->maxs), *start, *end, filter, pm);
	}
	return {KHook::Action::Ignore};
}

static KHook::Function<void, void *, trace_t *, const Vector *, const Vector *, const bbox_t *, CTraceFilter *> TracePlayerBBox(nullptr,
																																TracePlayerBBoxPost);

bool Surf::mode::InitTrace()
{
	static_persist bool initialized = false;
	if (initialized)
	{
		return true;
	}
	CGameConfig *gameConfig = g_pSurfUtils->GetGameConfig();
	if (!gameConfig)
	{
		return false;
	}
	void *tracePlayerBBoxAddress = gameConfig->ResolveSignature("TracePlayerBBox");
	if (!tracePlayerBBoxAddress)
	{
		return false;
	}
	TracePlayerBBox.Configure(tracePlayerBBoxAddress);
	initialized = true;
	return true;
}

void Surf::mode::CleanupTrace(SurfModeService *service)
{
	if (tracingModeService == service)
	{
		tracingModeService = nullptr;
	}
}

void SurfModeService::OnStopTouchGround()
{
	Vector velocity;
	this->player->GetVelocity(&velocity);
	this->player->takeoffVelocity = velocity;
}

void SurfModeService::OnStartTouchGround()
{
	this->SlopeFix();
	bbox_t bounds;
	// Shrink the bounds here as well, otherwise we touch triggers that are flush with the ground.
	bbox_t offset = {{0.03125f, 0.03125f, 0.03125f}, {-0.03125f, -0.03125f, -0.03125f}};
	this->player->GetBBoxBounds(&bounds, &offset);
	Vector ground = this->player->landingOrigin;
	ground.z = this->player->GetGroundPosition();
	this->player->TouchTriggersAlongPath(this->player->landingOrigin, ground, bounds);
}

void SurfModeService::OnProcessMovement()
{
	if (this->player->GetPlayerPawn()->m_flVelocityModifier() != 1.0f)
	{
		this->player->GetPlayerPawn()->m_flVelocityModifier(1.0f);
	}
	this->CheckVelocityQuantization();
	this->ReduceDuckSlowdown();
	this->InterpolateViewAngles();
	this->UpdateAngleHistory();
}

void SurfModeService::OnPlayerMove()
{
	this->originalMaxSpeed = this->player->currentMoveData->m_flMaxSpeed;
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void SurfModeService::OnProcessMovementPost()
{
	this->player->UpdateTriggerTouchList();
	this->RestoreInterpolatedViewAngles();
	this->oldDuckPressed = this->forcedUnduck || this->player->IsButtonPressed(IN_DUCK, true);
	this->oldJumpPressed = this->player->IsButtonPressed(IN_JUMP);
	Vector velocity;
	this->player->GetVelocity(&velocity);
	this->postProcessMovementZSpeed = velocity.z;
}

void SurfModeService::InterpolateViewAngles()
{
	// Second half of the movement, no change.
	CGlobalVars *globals = g_pSurfUtils->GetGlobals();
	f64 subtickFraction, whole;
	subtickFraction = modf((f64)globals->curtime * ENGINE_FIXED_TICK_RATE, &whole);
	if (subtickFraction < 0.001)
	{
		return;
	}

	// First half of the movement, tweak the angle to be the middle of the desired angle and the last angle
	QAngle newAngles = player->currentMoveData->m_vecViewAngles;
	QAngle oldAngles = this->hasValidDesiredViewAngle ? this->lastValidDesiredViewAngle : this->player->moveDataPost.m_vecViewAngles;
	if (newAngles[YAW] - oldAngles[YAW] > 180)
	{
		newAngles[YAW] -= 360.0f;
	}
	else if (newAngles[YAW] - oldAngles[YAW] < -180)
	{
		newAngles[YAW] += 360.0f;
	}

	for (u32 i = 0; i < 3; i++)
	{
		newAngles[i] += oldAngles[i];
		newAngles[i] *= 0.5f;
	}
	player->currentMoveData->m_vecViewAngles = newAngles;
}

void SurfModeService::RestoreInterpolatedViewAngles()
{
	player->currentMoveData->m_vecViewAngles = player->moveDataPre.m_vecViewAngles;
	if (g_pSurfUtils->GetGlobals()->frametime > 0.0f)
	{
		this->hasValidDesiredViewAngle = true;
		this->lastValidDesiredViewAngle = player->currentMoveData->m_vecViewAngles;
	}
}

void SurfModeService::ReduceDuckSlowdown()
{
	if (!this->player->GetMoveServices()->m_bDucking && this->player->GetMoveServices()->m_flDuckSpeed < DUCK_SPEED_NORMAL - EPSILON)
	{
		this->player->GetMoveServices()->m_flDuckSpeed = DUCK_SPEED_NORMAL;
	}
	else if (this->player->GetMoveServices()->m_flDuckSpeed < DUCK_SPEED_MINIMUM - EPSILON)
	{
		this->player->GetMoveServices()->m_flDuckSpeed = DUCK_SPEED_MINIMUM;
	}
}

void SurfModeService::UpdateAngleHistory()
{
	CMoveData *mv = this->player->currentMoveData;
	u32 oldEntries = 0;
	FOR_EACH_VEC(this->angleHistory, i)
	{
		if (this->angleHistory[i].when < g_pSurfUtils->GetGlobals()->curtime)
		{
			oldEntries++;
			continue;
		}
		break;
	}
	this->angleHistory.RemoveMultipleFromHead(oldEntries);
	if ((this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) == 0)
	{
		return;
	}

	AngleHistory *angHist = this->angleHistory.AddToTailGetPtr();
	angHist->when = g_pSurfUtils->GetGlobals()->curtime;
	angHist->duration = g_pSurfUtils->GetGlobals()->frametime;

	// Not turning if velocity is null.
	if (mv->m_vecVelocity.Length2D() == 0)
	{
		angHist->rate = 0;
		return;
	}

	// Copying from WalkMove
	Vector forward, right, up;
	AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

	f32 fmove = mv->m_flForwardMove;
	f32 smove = -mv->m_flSideMove;

	if (forward[2] != 0)
	{
		forward[2] = 0;
		forward = g_pSurfUtils->NormalizeVector(forward);
	}

	if (right[2] != 0)
	{
		right[2] = 0;
		right = g_pSurfUtils->NormalizeVector(right);
	}

	Vector wishdir;
	for (int i = 0; i < 2; i++)
	{
		wishdir[i] = forward[i] * fmove + right[i] * smove;
	}
	wishdir[2] = 0;

	wishdir = g_pSurfUtils->NormalizeVector(wishdir);

	if (wishdir.Length() == 0)
	{
		angHist->rate = 0;
		return;
	}

	Vector velocity = mv->m_vecVelocity;
	velocity[2] = 0;
	velocity = g_pSurfUtils->NormalizeVector(velocity);
	QAngle accelAngle;
	QAngle velAngle;
	VectorAngles(wishdir, accelAngle);
	VectorAngles(velocity, velAngle);
	accelAngle.y = g_pSurfUtils->NormalizeDeg(accelAngle.y);
	velAngle.y = g_pSurfUtils->NormalizeDeg(velAngle.y);
	angHist->rate = g_pSurfUtils->GetAngleDifference(velAngle.y, accelAngle.y, 180.0, true);
}

void SurfModeService::CheckVelocityQuantization()
{
	if (this->postProcessMovementZSpeed > this->player->currentMoveData->m_vecVelocity.z
		&& this->postProcessMovementZSpeed - this->player->currentMoveData->m_vecVelocity.z < 0.03125f
		// Colliding with a flat floor can result in a velocity of +0.0078125u/s, and this breaks ladders.
		// The quantization accidentally fixed this bug...
		&& fabs(this->player->currentMoveData->m_vecVelocity.z) > 0.03125f)
	{
		this->player->currentMoveData->m_vecVelocity.z = this->postProcessMovementZSpeed;
	}
}

// ORIGINAL AUTHORS : Mev & Blacky
// URL: https://forums.alliedmods.net/showthread.php?p=2322788
void SurfModeService::SlopeFix()
{
	CTraceFilterPlayerMovementCS filter(this->player->GetPlayerPawn());

	Vector ground = this->player->currentMoveData->m_vecAbsOrigin;
	ground.z -= 2;

	f32 standableZ = 0.7f; // Equal to the mode's cvar.

	if (sv_standable_normal.IsValidRef() && sv_standable_normal.IsConVarDataAvailable())
	{
		standableZ = sv_standable_normal.Get();
	}
	bbox_t bounds;
	this->player->GetBBoxBounds(&bounds);
	trace_t trace;

	INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), this->player->currentMoveData->m_vecAbsOrigin, ground, &filter, &trace);

	// Doesn't hit anything, fall back to the original ground
	if (trace.m_bStartInSolid || trace.m_flFraction == 1.0f)
	{
		return;
	}

	if (standableZ <= trace.m_vHitNormal.z && trace.m_vHitNormal.z < 1.0f)
	{
		// Copy the ClipVelocity function from sdk2013
		float backoff;
		float change;
		Vector newVelocity;

		backoff = DotProduct(this->player->landingVelocity, trace.m_vHitNormal) * 1;

		for (u32 i = 0; i < 3; i++)
		{
			change = trace.m_vHitNormal[i] * backoff;
			newVelocity[i] = this->player->landingVelocity[i] - change;
		}

		f32 adjust = DotProduct(newVelocity, trace.m_vHitNormal);
		if (adjust < 0.0f)
		{
			newVelocity -= (trace.m_vHitNormal * adjust);
		}
		// Make sure the player is going down a ramp by checking if they actually will gain speed from the boost.
		if (newVelocity.Length2D() >= this->player->landingVelocity.Length2D())
		{
			this->player->currentMoveData->m_vecVelocity.x = newVelocity.x;
			this->player->currentMoveData->m_vecVelocity.y = newVelocity.y;
			this->player->landingVelocity.x = newVelocity.x;
			this->player->landingVelocity.y = newVelocity.y;
		}
	}
}

void SurfModeService::OnTryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	tracingModeService = this;
	this->inTryPlayerMove = true;
	this->stuckTraceCount = 0;
	// The rest of the path is added by FixTryPlayerMoveTrace.
	Vector origin;
	this->player->GetOrigin(&origin);
	this->tpmTriggerFixOrigins.RemoveAll();
	this->tpmTriggerFixOrigins.AddToTail(origin);

	// WalkMove does the first trace before calling TryPlayerMove, so we need to fix that one here.
	// TryPlayerMove only uses it if it ends where its own first trace would end.
	if (pFirstDest && pFirstTrace && (this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND))
	{
		Vector velocity, end;
		this->player->GetVelocity(&velocity);
		VectorMA(origin, g_pSurfUtils->GetGlobals()->frametime, velocity, end);
		if (end == *pFirstDest)
		{
			bbox_t bounds;
			this->player->GetBBoxBounds(&bounds);
			CTraceFilterPlayerMovementCS filter(this->player->GetPlayerPawn());
			this->FixTryPlayerMoveTrace(Ray_t(bounds.mins, bounds.maxs), origin, end, &filter, pFirstTrace);
		}
	}
}

void SurfModeService::OnTryPlayerMovePost(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	tracingModeService = nullptr;
	this->inTryPlayerMove = false;
	if (this->airMoving)
	{
		if (this->tpmTriggerFixOrigins.Count() > 1)
		{
			bbox_t bounds;
			// We need to shrink the bounds a bit to prevent touching triggers that we shouldn't be touching when doing triggerfix.
			bbox_t offset = {{0.03125f, 0.03125f, 0.03125f}, {-0.03125f, -0.03125f, -0.03125f}};
			this->player->GetBBoxBounds(&bounds, &offset);
			for (int i = 0; i < this->tpmTriggerFixOrigins.Count() - 1; i++)
			{
				this->player->TouchTriggersAlongPath(this->tpmTriggerFixOrigins[i], this->tpmTriggerFixOrigins[i + 1], bounds);
			}
		}
		this->player->UpdateTriggerTouchList();
	}
}

void SurfModeService::OnCategorizePosition(bool bStayOnGround)
{
	tracingModeService = this;
}

void SurfModeService::OnCategorizePositionPost(bool bStayOnGround)
{
	tracingModeService = nullptr;
}

void SurfModeService::OnTracePlayerBBoxPost(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, trace_t *pm)
{
	if (this->inTryPlayerMove)
	{
		this->FixTryPlayerMoveTrace(ray, start, end, filter, pm);
	}
	else
	{
		this->FixGroundTrace(ray, start, end, filter, pm);
	}
}

/*
	Rampbug fix:
	The game keeps the player 1/32 unit away from whatever they collide with.
	While sliding on a ramp, the player can end up closer to the ramp than that, or collide with the edge between two triangles of the ramp
	instead of the ramp itself, which gives a wrong normal. TryPlayerMove then either stops the player or sends them in a random direction.
	To fix this, we redo the trace a bit further away from the ramp. We only use that trace if it actually goes further than the original one.
	If it just ends up on another plane right away, the player is really running into a corner and the original trace is kept.
*/
void SurfModeService::FixTryPlayerMoveTrace(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, trace_t *pm)
{
	Vector direction = end - start;
	f32 length = direction.NormalizeInPlace();
	// We hit a plane that will change our velocity, or we barely moved at all. This is either a real obstacle or a rampbug.
	bool shouldConsiderRampbug = pm->m_flFraction < 1.0f && !pm->m_bStartInSolid && this->lastPlane.LengthSqr() > 0.0f && length > 0.0f
								 && (pm->m_vHitNormal.Dot(this->lastPlane) < RAMP_BUG_THRESHOLD || pm->m_flFraction * length < RAMP_BUG_OFFSET);
	if (shouldConsiderRampbug)
	{
		bool success {};
		// We already failed to move twice, so TryPlayerMove is about to stop the player. Use any trace that goes further.
		bool stuck = this->stuckTraceCount >= 2;
		// This can only be a rampbug if the player is still right next to the last plane they hit.
		trace_t test;
		INavPhysicsInterface::TraceShape(ray, pm->m_vEndPos + this->lastPlane * RAMP_BUG_OFFSET,
										 pm->m_vEndPos - this->lastPlane * (RAMP_BUG_OFFSET * 2.0f), filter, &test);
		if (test.m_flFraction < 1.0f && !test.m_bStartInSolid && test.m_vHitNormal.Dot(this->lastPlane) >= RAMP_BUG_THRESHOLD)
		{
			// Redo the trace a bit further away from the plane.
			Vector offset = this->lastPlane * RAMP_BUG_OFFSET;
			trace_t offsetTrace;
			INavPhysicsInterface::TraceShape(ray, start + offset, end + offset, filter, &offsetTrace);
			// If this trace only goes a bit further and then hits a different plane, the original trace hit something real,
			// like a corner or two ramps meeting each other. Keep the original so TryPlayerMove can slide along both planes.
			f32 distance = (offsetTrace.m_vEndPos - pm->m_vEndPos).Dot(direction);
			bool samePlane = offsetTrace.m_vHitNormal.Dot(this->lastPlane) >= RAMP_BUG_SAME_PLANE;
			success = !offsetTrace.m_bStartInSolid && distance > RAMP_BUG_OFFSET
					  && (stuck || offsetTrace.m_flFraction == 1.0f || distance > RAMP_BUG_MIN_DISTANCE || samePlane);
			if (success)
			{
				*pm = offsetTrace;
			}
		}
		if (!success && stuck && pm->m_flFraction * length < RAMP_BUG_OFFSET)
		{
			// The player is stuck between two planes, moving away from one of them will just push them into the other one.
			// A trace only gives us one of the two planes, so we find the other one by tracing away from the first.
			Vector normal = pm->m_vHitNormal;
			Vector otherNormal = vec3_origin;
			if (normal.Dot(this->lastPlane) < RAMP_BUG_THRESHOLD)
			{
				otherNormal = this->lastPlane;
			}
			else
			{
				INavPhysicsInterface::TraceShape(ray, start, start + normal * (RAMP_BUG_OFFSET * 2.0f), filter, &test);
				if (test.m_flFraction < 1.0f && !test.m_bStartInSolid && test.m_vHitNormal.Dot(normal) < RAMP_BUG_THRESHOLD)
				{
					otherNormal = test.m_vHitNormal;
				}
			}
			if (otherNormal.LengthSqr() > 0.0f && normal.Dot(otherNormal) > -0.99f)
			{
				// Move away from both planes at once.
				Vector offset = normal + otherNormal;
				offset.NormalizeInPlace();
				offset *= MIN(RAMP_BUG_OFFSET / normal.Dot(offset), 1.0f);
				trace_t offsetTrace;
				INavPhysicsInterface::TraceShape(ray, start + offset, end + offset, filter, &offsetTrace);
				if (!offsetTrace.m_bStartInSolid && (offsetTrace.m_vEndPos - pm->m_vEndPos).Dot(direction) > RAMP_BUG_OFFSET)
				{
					*pm = offsetTrace;
				}
			}
		}
	}
	this->FixGroundTrace(ray, start, end, filter, pm);
	if (pm->m_flFraction < 1.0f && !pm->m_bStartInSolid)
	{
		if (pm->m_vHitNormal.LengthSqr() > 0.98f)
		{
			this->lastPlane = pm->m_vHitNormal;
		}
		if (pm->m_flFraction * length < RAMP_BUG_OFFSET)
		{
			this->stuckTraceCount++;
		}
	}
	// Triggerfix related
	this->tpmTriggerFixOrigins.AddToTail(pm->m_vEndPos);
}

// Sometimes the game thinks the player is on standable ground while they are actually on a steep slope right below a wall.
// A slightly smaller hull doesn't have this problem and will hit the slope instead.
// It will also hit a slope if the player is standing over a gap between two floors though, so we make sure there's a wall next to the player.
void SurfModeService::FixGroundTrace(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, trace_t *pm)
{
	f32 standableZ = Surf::mode::modeCvarRefs[MODECVAR_SV_STANDABLE_NORMAL]->GetFloat();
	if (pm->m_flFraction == 1.0f || pm->m_bStartInSolid || pm->m_vHitNormal.z < standableZ)
	{
		return;
	}
	Vector shrink(GROUND_CHECK_SHRINK_SIZE, GROUND_CHECK_SHRINK_SIZE, 0.0f);
	trace_t smallTrace;
	INavPhysicsInterface::TraceShape(Ray_t(ray.m_Hull.m_vMins + shrink, ray.m_Hull.m_vMaxs - shrink), start, end, filter, &smallTrace);
	if (smallTrace.m_flFraction == 1.0f || smallTrace.m_bStartInSolid || smallTrace.m_vHitNormal.z >= standableZ)
	{
		return;
	}
	// Is there a wall on the side that the slope goes up towards?
	Vector wallDirection(-smallTrace.m_vHitNormal.x, -smallTrace.m_vHitNormal.y, 0.0f);
	wallDirection.NormalizeInPlace();
	trace_t wallTrace;
	INavPhysicsInterface::TraceShape(ray, pm->m_vEndPos, pm->m_vEndPos + wallDirection * GROUND_CHECK_SHRINK_SIZE, filter, &wallTrace);
	if (wallTrace.m_flFraction < 0.5f && !wallTrace.m_bStartInSolid)
	{
		pm->m_vHitNormal = smallTrace.m_vHitNormal;
	}
}

void SurfModeService::OnDuckPost()
{
	this->player->UpdateTriggerTouchList();
}

void SurfModeService::OnAirMove()
{
	this->airMoving = true;
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void SurfModeService::OnAirMovePost()
{
	this->airMoving = false;
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void SurfModeService::OnWaterMove()
{
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void SurfModeService::OnWaterMovePost()
{
	this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
}

void SurfModeService::OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	if (!this->player->processingMovement)
	{
		return;
	}
	// Only happens when triggerfix happens.
	if (newPosition)
	{
		this->player->currentMoveData->m_vecAbsOrigin = *newPosition;
	}
	if (newVelocity)
	{
		this->player->currentMoveData->m_vecVelocity = *newVelocity;
	}
}

// Only touch timer triggers on tick.
bool SurfModeService::OnTriggerStartTouch(CBaseTrigger *trigger)
{
	if (!g_pMappingApi->IsTriggerATimerZone(trigger))
	{
		return true;
	}
	f64 tick = g_pSurfUtils->GetGlobals()->curtime * ENGINE_FIXED_TICK_RATE;
	if (fabs(roundf(tick) - tick) < 0.001f)
	{
		return true;
	}

	return false;
}

bool SurfModeService::OnTriggerTouch(CBaseTrigger *trigger)
{
	if (!g_pMappingApi->IsTriggerATimerZone(trigger))
	{
		return true;
	}
	f64 tick = g_pSurfUtils->GetGlobals()->curtime * ENGINE_FIXED_TICK_RATE;
	if (fabs(roundf(tick) - tick) < 0.001f)
	{
		return true;
	}
	return false;
}

bool SurfModeService::OnTriggerEndTouch(CBaseTrigger *trigger)
{
	if (!g_pMappingApi->IsTriggerATimerZone(trigger))
	{
		return true;
	}
	f64 tick = g_pSurfUtils->GetGlobals()->curtime * ENGINE_FIXED_TICK_RATE;
	if (fabs(roundf(tick) - tick) < 0.001f)
	{
		return true;
	}
	return false;
}
