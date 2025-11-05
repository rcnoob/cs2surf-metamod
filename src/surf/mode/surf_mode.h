#pragma once
#include "../surf.h"
#include "surf/mappingapi/surf_mappingapi.h"
#include "surf/global/api.h"
#include "UtlStringMap.h"
#include "cs_usercmd.pb.h"
#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"
#include "sdk/usercmd.h"
#include "sdk/tracefilter.h"
#include "sdk/entity/cbasetrigger.h"

#define SURF_MODE_MANAGER_INTERFACE "SurfModeManagerInterface"

// Rampbug fix related
#define MAX_BUMPS                   4
#define RAMP_PIERCE_DISTANCE        0.0625f
#define RAMP_BUG_THRESHOLD          0.98f
#define RAMP_BUG_VELOCITY_THRESHOLD 0.95f
#define NEW_RAMP_THRESHOLD          0.95f

#define DUCK_SPEED_NORMAL  8.0f
#define DUCK_SPEED_MINIMUM 6.0234375f // Equal to if you just ducked/unducked for the first time in a while

#define SPEED_NORMAL 260.0f

enum SurfModeCvars
{
	MODECVAR_FIRST = 0,
	MODECVAR_SLOPE_DROP_ENABLE = 0,
	MODECVAR_SV_ACCELERATE,
	MODECVAR_SV_ACCELERATE_USE_WEAPON_SPEED,
	MODECVAR_SV_AIRACCELERATE,
	MODECVAR_SV_AIR_MAX_WISHSPEED,
	MODECVAR_SV_AUTOBUNNYHOPPING,
	MODECVAR_SV_BOUNCE,
	MODECVAR_SV_ENABLEBUNNYHOPPING,
	MODECVAR_SV_FRICTION,
	MODECVAR_SV_GRAVITY,
	MODECVAR_SV_JUMP_IMPULSE,
	MODECVAR_SV_JUMP_PRECISION_ENABLE,
	MODECVAR_SV_JUMP_SPAM_PENALTY_TIME,
	MODECVAR_SV_LADDER_ANGLE,
	MODECVAR_SV_LADDER_DAMPEN,
	MODECVAR_SV_LADDER_SCALE_SPEED,
	MODECVAR_SV_MAXSPEED,
	MODECVAR_SV_MAXVELOCITY,
	MODECVAR_SV_STAMINAJUMPCOST,
	MODECVAR_SV_STAMINALANDCOST,
	MODECVAR_SV_STAMINAMAX,
	MODECVAR_SV_STAMINARECOVERYRATE,
	MODECVAR_SV_STANDABLE_NORMAL,
	MODECVAR_SV_STEP_MOVE_VEL_MIN,
	MODECVAR_SV_TIMEBETWEENDUCKS,
	MODECVAR_SV_WALKABLE_NORMAL,
	MODECVAR_SV_WATERACCELERATE,
	MODECVAR_SV_WATERFRICTION,
	MODECVAR_SV_WATER_SLOW_AMOUNT,
	MODECVAR_MP_SOLID_TEAMMATES,
	MODECVAR_MP_SOLID_ENEMIES,
	MODECVAR_SV_SUBTICK_MOVEMENT_VIEW_ANGLES,
	MODECVAR_COUNT,
};
class SurfPlayer;

class SurfModeService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

protected:
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
	virtual const char *GetModeName() = 0;
	virtual const char *GetModeShortName() = 0;
	virtual void Init() {};
	virtual void Cleanup() {};
	virtual const CVValue_t *GetModeConVarValues() = 0;

	// Fixes
	bool EnableWaterFix()
	{
		return true;
	}

	// Movement hooks
	void OnPhysicsSimulate() {}

	void OnPhysicsSimulatePost() {}

	void OnProcessUsercmds(void *, int) {}

	void OnProcessUsercmdsPost(void *, int) {}

	void OnSetupMove(PlayerCommand *);

	void OnSetupMovePost(PlayerCommand *) {}

	void OnProcessMovement();

	void OnProcessMovementPost();

	void OnPlayerMove();

	void OnPlayerMovePost() {}

	void OnCheckParameters() {}

	void OnCheckParametersPost() {}

	void OnCanMove() {}

	void OnCanMovePost() {}

	void OnFullWalkMove(bool &) {}

	void OnFullWalkMovePost(bool) {}

	void OnMoveInit() {}

	void OnMoveInitPost() {}

	void OnCheckWater() {}

	void OnCheckWaterPost() {}

	void OnWaterMove();

	void OnWaterMovePost();

	void OnCheckVelocity(const char *) {}

	void OnCheckVelocityPost(const char *) {}

	void OnDuck() {}

	void OnDuckPost();

	// Make an exception for this as it is the only time where we need to change the return value.
	void OnCanUnduck() {}

	void OnCanUnduckPost(bool &) {}

	void OnLadderMove() {}

	void OnLadderMovePost() {}

	void OnCheckJumpButton() {}

	void OnCheckJumpButtonPost() {}

	void OnJump() {}

	void OnJumpPost() {}

	void OnAirMove();

	void OnAirMovePost();

	virtual void OnAirAccelerate(Vector &wishdir, f32 &wishspeed, f32 &accel) {}

	virtual void OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel) {}

	void OnFriction() {}

	void OnFrictionPost() {}

	void OnWalkMove() {}

	void OnWalkMovePost() {}

	void OnTryPlayerMove(Vector *, trace_t *, bool *);

	void OnTryPlayerMovePost(Vector *, trace_t *, bool *);

	void OnCategorizePosition(bool);

	void OnCategorizePositionPost(bool) {}

	void OnFinishGravity() {}

	void OnFinishGravityPost() {}

	void OnCheckFalling() {}

	void OnCheckFallingPost() {}

	void OnPostPlayerMove() {}

	void OnPostPlayerMovePost() {}

	void OnPostThink() {}

	void OnPostThinkPost() {}

	// Movement events
	void OnStartTouchGround();

	void OnStopTouchGround();

	void OnChangeMoveType(MoveType_t oldMoveType) {}

	bool OnTriggerStartTouch(CBaseTrigger *trigger);

	bool OnTriggerTouch(CBaseTrigger *trigger);

	bool OnTriggerEndTouch(CBaseTrigger *trigger);

	// Other events
	void OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity);

	void ClipVelocity(Vector &in, Vector &normal, Vector &out);

	bool IsValidMovementTrace(trace_t &tr, bbox_t bounds, CTraceFilterPlayerMovementCS *filter);

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

typedef SurfModeService *(*ModeServiceFactory)(SurfPlayer *player);

class SurfModeManager
{
public:
	struct ModePluginInfo
	{
		// ID 0 is reserved for VNL
		// -1 is for mode that exists in the database (but not loaded in the plugin)
		// -2 is for invalid mode.
		PluginId id = -2;
		CUtlString shortModeName;
		CUtlString longModeName;
		ModeServiceFactory factory {};
		bool shortCmdRegistered {};
		char md5[33] {};
		i32 databaseID = -1;
	};

	// clang-format off
	virtual bool RegisterMode(PluginId id, const char *shortModeName, const char *longModeName, ModeServiceFactory factory);
	// clang-format on

	virtual void UnregisterMode(PluginId id);
	bool SwitchToMode(SurfPlayer *player, const char *modeName, bool silent = false, bool force = false, bool updatePreference = true);
	void Cleanup();
};

extern SurfModeManager *g_pSurfModeManager;

namespace Surf::mode
{
	bool CheckModeCvars();
	void InitModeService(SurfPlayer *player);
	void InitModeManager();
	void LoadModePlugins();
	void UpdateModeDatabaseID(CUtlString name, i32 id, CUtlString shortName = "");
	// clang-format off

	inline const char *modeCvarNames[] = {
		"slope_drop_enable",
		"sv_accelerate",
		"sv_accelerate_use_weapon_speed",
		"sv_airaccelerate",
		"sv_air_max_wishspeed",
		"sv_autobunnyhopping",
		"sv_bounce",
		"sv_enablebunnyhopping",
		"sv_friction",
		"sv_gravity",
		"sv_jump_impulse",
		"sv_jump_precision_enable",
		"sv_jump_spam_penalty_time",
		"sv_ladder_angle",
		"sv_ladder_dampen",
		"sv_ladder_scale_speed",
		"sv_maxspeed",
		"sv_maxvelocity",
		"sv_staminajumpcost",
		"sv_staminalandcost",
		"sv_staminamax",
		"sv_staminarecoveryrate",
		"sv_standable_normal",
		"sv_step_move_vel_min",
		"sv_timebetweenducks",
		"sv_walkable_normal",
		"sv_wateraccelerate",
		"sv_waterfriction",
		"sv_water_slow_amount",
		"mp_solid_teammates",
		"mp_solid_enemies",
		"sv_subtick_movement_view_angles"
	};


	static_assert(SURF_ARRAYSIZE(modeCvarNames) == MODECVAR_COUNT, "Array modeCvarNames length is not the same as MODECVAR_COUNT!");
	// Quite a horrible thing to do but there is no other way around it...
	inline ConVarRefAbstract *modeCvarRefs[] =
	{
		new CConVarRef<bool>("slope_drop_enable"),
		new CConVarRef<float>("sv_accelerate"),
		new CConVarRef<bool>("sv_accelerate_use_weapon_speed"),
		new CConVarRef<float>("sv_airaccelerate"),
		new CConVarRef<float>("sv_air_max_wishspeed"),
		new CConVarRef<bool>("sv_autobunnyhopping"),
		new CConVarRef<float>("sv_bounce"),
		new CConVarRef<bool>("sv_enablebunnyhopping"),
		new CConVarRef<float>("sv_friction"),
		new CConVarRef<float>("sv_gravity"),
		new CConVarRef<float>("sv_jump_impulse"),
		new CConVarRef<bool>("sv_jump_precision_enable"),
		new CConVarRef<float>("sv_jump_spam_penalty_time"),
		new CConVarRef<float>("sv_ladder_angle"),
		new CConVarRef<float>("sv_ladder_dampen"),
		new CConVarRef<float>("sv_ladder_scale_speed"),
		new CConVarRef<float>("sv_maxspeed"),
		new CConVarRef<float>("sv_maxvelocity"),
		new CConVarRef<float>("sv_staminajumpcost"),
		new CConVarRef<float>("sv_staminalandcost"),
		new CConVarRef<float>("sv_staminamax"),
		new CConVarRef<float>("sv_staminarecoveryrate"),
		new CConVarRef<float>("sv_standable_normal"),
		new CConVarRef<float>("sv_step_move_vel_min"),
		new CConVarRef<float>("sv_timebetweenducks"),
		new CConVarRef<float>("sv_walkable_normal"),
		new CConVarRef<float>("sv_wateraccelerate"),
		new CConVarRef<float>("sv_waterfriction"),
		new CConVarRef<float>("sv_water_slow_amount"),
		new CConVarRef<int>("mp_solid_teammates"),
		new CConVarRef<int>("mp_solid_enemies"),
		new CConVarRef<bool>("sv_subtick_movement_view_angles")
	};

	// clang-format on
	static_assert(SURF_ARRAYSIZE(modeCvarNames) == MODECVAR_COUNT, "Array modeCvarRefs length is not the same as MODECVAR_COUNT!");

	void ApplyModeSettings(SurfPlayer *player);
	void DisableReplicatedModeCvars();
	void EnableReplicatedModeCvars();

	SurfModeManager::ModePluginInfo GetModeInfo(SurfModeService *mode);
	SurfModeManager::ModePluginInfo GetModeInfo(Surf::API::Mode mode);
	SurfModeManager::ModePluginInfo GetModeInfo(CUtlString modeName);
	SurfModeManager::ModePluginInfo GetModeInfoFromDatabaseID(i32 id);
}; // namespace Surf::mode
