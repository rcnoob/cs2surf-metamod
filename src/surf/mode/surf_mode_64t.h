#pragma once
#include "version_gen.h"

#include "surf_mode.h"
#include "sdk/datatypes.h"

#define MODE_NAME_SHORT "64t"
#define MODE_NAME       "64tick"
// Rampbug fix related
#define MAX_BUMPS                   4
#define RAMP_PIERCE_DISTANCE        0.0625f
#define RAMP_BUG_THRESHOLD          0.98f
#define RAMP_BUG_VELOCITY_THRESHOLD 0.95f
#define NEW_RAMP_THRESHOLD          0.95f

#define DUCK_SPEED_NORMAL  8.0f
#define DUCK_SPEED_MINIMUM 6.0234375f // Equal to if you just ducked/unducked for the first time in a while

#define SPEED_NORMAL 260.0f

class Surf64tModePlugin : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late);
	bool Unload(char *error, size_t maxlen);
	bool Pause(char *error, size_t maxlen);
	bool Unpause(char *error, size_t maxlen);

public:
	const char *GetAuthor()
	{
		return PLUGIN_AUTHOR;
	}

	const char *GetName()
	{
		return "CS2Surf-Mode-64tick";
	}

	const char *GetDescription()
	{
		return "64tick mode plugin for CS2Surf";
	}

	const char *GetURL()
	{
		return PLUGIN_URL;
	}

	const char *GetLicense()
	{
		return PLUGIN_LICENSE;
	}

	const char *GetVersion()
	{
		return PLUGIN_FULL_VERSION;
	}

	const char *GetDate()
	{
		return __DATE__;
	}

	const char *GetLogTag()
	{
		return PLUGIN_LOGTAG;
	}
};

class Surf64tModeService : public SurfModeService
{
	using SurfModeService::SurfModeService;

	static inline CVValue_t modeCvarValues[] = {
		(bool)false,    // slope_drop_enable
		(float)10.0f,     // sv_accelerate
		(bool)false,    // sv_accelerate_use_weapon_speed
		(float)150.0f,  // sv_airaccelerate
		(float)30.0f,   // sv_air_max_wishspeed
		(bool)true,     // sv_autobunnyhopping
		(float)0.0f,    // sv_bounce
		(bool)true,     // sv_enablebunnyhopping
		(float)5.2f,    // sv_friction
		(float)800.0f,  // sv_gravity
		(float)302.0f,  // sv_jump_impulse
		(bool)false,    // sv_jump_precision_enable
		(float)0.0f,    // sv_jump_spam_penalty_time
		(float)-0.707f, // sv_ladder_angle
		(float)1.0f,    // sv_ladder_dampen
		(float)1.0f,    // sv_ladder_scale_speed
		(float)320.0f,  // sv_maxspeed
		(float)4096.0f, // sv_maxvelocity
		(float)0.0f,    // sv_staminajumpcost
		(float)0.0f,    // sv_staminalandcost
		(float)0.0f,    // sv_staminamax
		(float)9999.0f, // sv_staminarecoveryrate
		(float)0.7f,    // sv_standable_normal
		(float)64.0f,   // sv_step_move_vel_min
		(float)0.0f,    // sv_timebetweenducks
		(float)0.7f,    // sv_walkable_normal
		(float)10.0f,   // sv_wateraccelerate
		(float)1.0f,    // sv_waterfriction
		(float)0.9f,    // sv_water_slow_amount
		(int)0,         // mp_solid_teammates
		(int)0,         // mp_solid_enemies
		(bool)false,    // sv_subtick_movement_view_angles
	};
	static_assert(SURF_ARRAYSIZE(modeCvarValues) == MODECVAR_COUNT, "Array modeCvarValues length is not the same as MODECVAR_COUNT!");

	bool hasValidDesiredViewAngle {};
	QAngle lastValidDesiredViewAngle;
	f32 lastJumpReleaseTime {};
	bool oldDuckPressed {};
	bool oldJumpPressed {};
	bool forcedUnduck {};
	f32 postProcessMovementZSpeed {};

	struct AngleHistory
	{
		f32 rate;
		f32 when;
		f32 duration;
	};

	CUtlVector<AngleHistory> angleHistory;
	f32 leftPreRatio {};
	f32 rightPreRatio {};
	f32 bonusSpeed {};
	f32 maxPre {};
	f32 originalMaxSpeed {};
	f32 tweakedMaxSpeed {};

	bool didTPM {};
	bool overrideTPM {};
	Vector tpmVelocity = vec3_invalid;
	Vector tpmOrigin = vec3_invalid;
	Vector lastValidPlane = vec3_origin;

	// Keep track of TryPlayerMove path for triggerfixing.
	bool airMoving {};
	CUtlVector<Vector> tpmTriggerFixOrigins;

public:
	virtual void Reset() override;
	virtual void Cleanup() override;
	virtual const char *GetModeName() override;
	virtual const char *GetModeShortName() override;

	virtual bool EnableWaterFix() override;

	virtual const CVValue_t *GetModeConVarValues() override;

	virtual void OnSetupMove(PlayerCommand *pc) override;
	virtual void OnProcessMovement() override;
	virtual void OnPlayerMove() override;
	virtual void OnProcessMovementPost() override;
	virtual void OnCategorizePosition(bool bStayOnGround) override;
	virtual void OnDuckPost() override;
	virtual void OnAirMove() override;
	virtual void OnAirMovePost() override;
	virtual void OnWaterMove() override;
	virtual void OnWaterMovePost() override;
	virtual void OnStartTouchGround() override;
	virtual void OnStopTouchGround() override;
	virtual void OnTryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing) override;
	virtual void OnTryPlayerMovePost(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing) override;
	virtual void OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity) override;

	virtual bool OnTriggerStartTouch(CBaseTrigger *trigger) override;
	virtual bool OnTriggerTouch(CBaseTrigger *trigger) override;
	virtual bool OnTriggerEndTouch(CBaseTrigger *trigger) override;

	// Insert subtick timing to be called later. Should only call this in PhysicsSimulate.
	void InsertSubtickTiming(float time);

	void InterpolateViewAngles();
	void RestoreInterpolatedViewAngles();

	void UpdateAngleHistory();

	void CheckVelocityQuantization();
	/*
		Ported from DanZay's SimpleKZ:
		Duck speed is reduced by the game upon ducking or unducking.
		The goal here is to accept that duck speed is reduced, but
		stop it from being reduced further when spamming duck.

		This is done by enforcing a minimum duck speed equivalent to
		the value as if the player only ducked once. When not in not
		in the middle of ducking, duck speed is reset to its normal
		value in effort to reduce the number of times the minimum
		duck speed is enforced. This should reduce noticeable lag.
	*/
	void ReduceDuckSlowdown();

	void SlopeFix();
};
