#pragma once
#include "version_gen.h"

#include "surf_mode.h"
#include "sdk/datatypes.h"

class Surf64tModeService : public SurfModeService
{
	using SurfModeService::SurfModeService;

	static inline CVValue_t modeCvarValues[] = {
		(float)10.0f,   // sv_accelerate
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
};
