#pragma once
#include "../surf.h"

#define SURF_STYLE_MANAGER_INTERFACE "SurfStyleManagerInterface"

// TODO styles: normal, backwards, sw, hsw, w only, lowgrav, autobhop, 250 speed, high gravity, notrigger, alivestrafe
class SurfStyleService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

public:
	virtual const char *GetStyleName() = 0;
	virtual const char *GetStyleShortName() = 0;
	virtual void Init() {};
	virtual void Cleanup() {};

	virtual bool IsCompatibleWithStyle(CUtlString style)
	{
		return true;
	}

	// Return the tweaked value, null if this convar name is not tweaked.
	virtual const CVValue_t *GetTweakedConvarValue(const char *name)
	{
		return nullptr;
	}

	// Movement hooks
	// These functions are always called after the mode service's functions.
	virtual void OnPhysicsSimulate() {}

	virtual void OnPhysicsSimulatePost() {}

	virtual void OnProcessUsercmds(void *, int) {}

	virtual void OnProcessUsercmdsPost(void *, int) {}

	virtual void OnSetupMove(PlayerCommand *) {}

	virtual void OnSetupMovePost(PlayerCommand *) {}

	virtual void OnProcessMovement() {}

	virtual void OnProcessMovementPost() {}

	virtual void OnPlayerMove() {}

	virtual void OnPlayerMovePost() {}

	virtual void OnCheckParameters() {}

	virtual void OnCheckParametersPost() {}

	virtual void OnCanMove() {}

	virtual void OnCanMovePost() {}

	virtual void OnFullWalkMove(bool &) {}

	virtual void OnFullWalkMovePost(bool) {}

	virtual void OnMoveInit() {}

	virtual void OnMoveInitPost() {}

	virtual void OnCheckWater() {}

	virtual void OnCheckWaterPost() {}

	virtual void OnWaterMove() {}

	virtual void OnWaterMovePost() {}

	virtual void OnCheckVelocity(const char *) {}

	virtual void OnCheckVelocityPost(const char *) {}

	virtual void OnDuck() {}

	virtual void OnDuckPost() {}

	// Make an exception for this as it is the only time where we need to change the return value.
	virtual void OnCanUnduck() {}

	virtual void OnCanUnduckPost(bool &) {}

	virtual void OnLadderMove() {}

	virtual void OnLadderMovePost() {}

	virtual void OnCheckJumpButton() {}

	virtual void OnCheckJumpButtonPost() {}

	virtual void OnJump() {}

	virtual void OnJumpPost() {}

	virtual void OnAirMove() {}

	virtual void OnAirMovePost() {}

	virtual void OnAirAccelerate(Vector &wishdir, f32 &wishspeed, f32 &accel) {}

	virtual void OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel) {}

	virtual void OnFriction() {}

	virtual void OnFrictionPost() {}

	virtual void OnWalkMove() {}

	virtual void OnWalkMovePost() {}

	virtual void OnTryPlayerMove(Vector *, trace_t *, bool *) {}

	virtual void OnTryPlayerMovePost(Vector *, trace_t *, bool *) {}

	virtual void OnCategorizePosition(bool) {}

	virtual void OnCategorizePositionPost(bool) {}

	virtual void OnFinishGravity() {}

	virtual void OnFinishGravityPost() {}

	virtual void OnCheckFalling() {}

	virtual void OnCheckFallingPost() {}

	virtual void OnPostPlayerMove() {}

	virtual void OnPostPlayerMovePost() {}

	virtual void OnPostThink() {}

	virtual void OnPostThinkPost() {}

	// Movement events
	virtual void OnStartTouchGround() {}

	virtual void OnStopTouchGround() {}

	virtual void OnChangeMoveType(MoveType_t oldMoveType) {}

	virtual bool OnTriggerStartTouch(CBaseTrigger *trigger)
	{
		return true;
	}

	virtual bool OnTriggerTouch(CBaseTrigger *trigger)
	{
		return true;
	}

	virtual bool OnTriggerEndTouch(CBaseTrigger *trigger)
	{
		return true;
	}
};

typedef SurfStyleService *(*StyleServiceFactory)(SurfPlayer *player);

class SurfStyleManager
{
public:
	struct StylePluginInfo
	{
		// -1 is for style that exists in the database (but not loaded in the plugin)
		// -2 is for invalid style.
		PluginId id = -2;
		const char *shortName {};
		const char *longName {};
		StyleServiceFactory factory {};
		char md5[33] {};
		i32 databaseID = -1;
		CCopyableUtlVector<CUtlString> incompatibleStyles;
	};

	virtual bool RegisterStyle(PluginId id, const char *shortName, const char *longName, StyleServiceFactory factory,
							   const char **incompatibleStyles = nullptr, u32 incompatibleStylesCount = 0);
	virtual void UnregisterStyle(PluginId id);
	void Cleanup();

	void AddStyle(SurfPlayer *player, const char *styleName, bool silent = false, bool updatePreference = true);
	void RemoveStyle(SurfPlayer *player, const char *styleName, bool silent = false, bool updatePreference = true);
	void ToggleStyle(SurfPlayer *player, const char *styleName, bool silent = false, bool updatePreference = true);
	void ClearStyles(SurfPlayer *player, bool silent = false, bool updatePreference = true);
	void RefreshStyles(SurfPlayer *player, bool updatePreference = true);
	CUtlString GetStylesString(SurfPlayer *player);
	void PrintActiveStyles(SurfPlayer *player);
	void PrintAllStyles(SurfPlayer *player);

private:
};

extern SurfStyleManager *g_pSurfStyleManager;

namespace Surf::style
{
	void InitStyleManager();
	void LoadStylePlugins();
	void UpdateStyleDatabaseID(CUtlString name, i32 id);
	SurfStyleManager::StylePluginInfo GetStyleInfo(SurfStyleService *style);
	SurfStyleManager::StylePluginInfo GetStyleInfo(CUtlString styleName);

}; // namespace Surf::style
