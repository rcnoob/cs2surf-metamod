#pragma once
#include "version_gen.h"

#include "surf_mode.h"
#include "sdk/datatypes.h"

class SurfSourceModePlugin : public ISmmPlugin, public IMetamodListener
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
		return "CS2Surf-Mode-Source";
	}

	const char *GetDescription()
	{
		return "Source mode plugin for CS2Surf";
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

class SurfSourceModeService : public SurfModeService
{
	using SurfModeService::SurfModeService;

	static inline CVValue_t modeCvarValues[] = {
		(float)5.0f,    // sv_accelerate
		(bool)false,    // sv_accelerate_use_weapon_speed
		(float)150.0f,  // sv_airaccelerate
		(float)30.0f,   // sv_air_max_wishspeed
		(bool)true,     // sv_autobunnyhopping
		(float)0.0f,    // sv_bounce
		(bool)true,     // sv_enablebunnyhopping
		(float)4.0f,    // sv_friction
		(float)800.0f,  // sv_gravity
		(float)302.0f,  // sv_jump_impulse
		(bool)false,    // sv_jump_precision_enable
		(float)-1.267f, // sv_jump_spam_penalty_time
		(float)-0.707f, // sv_ladder_angle
		(float)1.0f,    // sv_ladder_dampen
		(float)1.0f,    // sv_ladder_scale_speed
		(float)320.0f,  // sv_maxspeed
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
		(bool)true,     // sv_legacy_jump
		(float)0.02f    // sv_bhop_time_window (unused)
	};
	static_assert(SURF_ARRAYSIZE(modeCvarValues) == MODECVAR_COUNT, "Array modeCvarValues length is not the same as MODECVAR_COUNT!");

public:
	virtual void Reset() override;
	virtual void Cleanup() override;
	virtual const char *GetModeName() override;
	virtual const char *GetModeShortName() override;
	virtual const CVValue_t *GetModeConVarValues() override;
	virtual void OnSetupMove(CUserCmd *) override;
	virtual void OnPhysicsSimulate() override;
	virtual void OnPhysicsSimulatePost() override;
};
