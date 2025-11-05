#pragma once

#include "common.h"
#include "movement/movement.h"
#include "sdk/datatypes.h"
#include "mappingapi/surf_mappingapi.h"
#include "circularbuffer.h"

// TODO: If we want to enable player collision, we need to unhardcode this.
#define SURF_COLLISION_GROUP_STANDARD  COLLISION_GROUP_DEBRIS
#define SURF_COLLISION_GROUP_NOTRIGGER LAST_SHARED_COLLISION_GROUP

#define SURF_SND_SET_CP    "UIPanorama.round_report_odds_none"
#define SURF_SND_DO_TP     "UIPanorama.round_report_odds_none"
#define SURF_SND_RESET_CPS "UIPanorama.round_report_odds_dn"

// TODO: add sound & menu addon if its even necessary
#define SURF_WORKSHOP_ADDON_ID            ""
#define SURF_WORKSHOP_ADDON_SNDEVENT_FILE ""

#define SURF_DEFAULT_CHAT_PREFIX  "{lime}Surf {grey}|{default}"
#define SURF_DEFAULT_TIP_INTERVAL 75.0
#define SURF_DEFAULT_LANGUAGE     "en"
#define SURF_DEFAULT_STYLE        "Normal"
#define SURF_DEFAULT_MODE         "64tick"

#define SURF_RECENT_TELEPORT_THRESHOLD 0.05f

class SurfPlayer;
class SurfAnticheatService;
class SurfBeamService;
class SurfZoneBeamService;
class SurfCheckpointService;
class SurfDatabaseService;
class SurfGlobalService;
class SurfHUDService;
class SurfLanguageService;
class SurfMapService;
class SurfModeService;
class SurfNoclipService;
class SurfOptionService;
class SurfQuietService;
class SurfSpecService;
class SurfGotoService;
class SurfProfileService;
class SurfStyleService;
class SurfTelemetryService;
class SurfTimerService;
class SurfTipService;
class SurfTriggerService;

class SurfPlayer : public MovementPlayer
{
public:
	SurfPlayer(i32 i) : MovementPlayer(i)
	{
		this->Init();
	}

	// General events
	virtual void Init() override;
	virtual void Reset() override;
	virtual void OnPlayerConnect(u64 steamID64) override;
	virtual void OnPlayerActive() override;
	virtual void OnPlayerFullyConnect() override;
	virtual void OnAuthorized() override;

	virtual void OnPhysicsSimulate() override;
	virtual void OnPhysicsSimulatePost() override;
	virtual void OnProcessUsercmds(void *, int) override;
	virtual void OnProcessUsercmdsPost(void *, int) override;
	virtual void OnSetupMove(PlayerCommand *) override;
	virtual void OnSetupMovePost(PlayerCommand *) override;
	virtual void OnProcessMovement() override;
	virtual void OnProcessMovementPost() override;
	virtual void OnPlayerMove() override;
	virtual void OnPlayerMovePost() override;
	virtual void OnCheckParameters() override;
	virtual void OnCheckParametersPost() override;
	virtual void OnCanMove() override;
	virtual void OnCanMovePost() override;
	virtual void OnFullWalkMove(bool &) override;
	virtual void OnFullWalkMovePost(bool) override;
	virtual void OnMoveInit() override;
	virtual void OnMoveInitPost() override;
	virtual void OnCheckWater() override;
	virtual void OnCheckWaterPost() override;
	virtual void OnWaterMove() override;
	virtual void OnWaterMovePost() override;
	virtual void OnCheckVelocity(const char *) override;
	virtual void OnCheckVelocityPost(const char *) override;
	virtual void OnDuck() override;
	virtual void OnDuckPost() override;
	virtual void OnCanUnduck() override;
	// Make an exception for this as it is the only time where we need to change the return value.
	virtual void OnCanUnduckPost(bool &) override;
	virtual void OnLadderMove() override;
	virtual void OnLadderMovePost() override;
	virtual void OnCheckJumpButton() override;
	virtual void OnCheckJumpButtonPost() override;
	virtual void OnJump() override;
	virtual void OnJumpPost() override;
	virtual void OnAirMove() override;
	virtual void OnAirMovePost() override;
	virtual void OnAirAccelerate(Vector &wishdir, f32 &wishspeed, f32 &accel) override;
	virtual void OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel) override;
	virtual void OnFriction() override;
	virtual void OnFrictionPost() override;
	virtual void OnWalkMove() override;
	virtual void OnWalkMovePost() override;
	virtual void OnTryPlayerMove(Vector *, trace_t *, bool *) override;
	virtual void OnTryPlayerMovePost(Vector *, trace_t *, bool *) override;
	virtual void OnCategorizePosition(bool) override;
	virtual void OnCategorizePositionPost(bool) override;
	virtual void OnFinishGravity() override;
	virtual void OnFinishGravityPost() override;
	virtual void OnCheckFalling() override;
	virtual void OnCheckFallingPost() override;
	virtual void OnPostPlayerMove() override;
	virtual void OnPostPlayerMovePost() override;
	virtual void OnPostThink() override;
	virtual void OnPostThinkPost() override;

	// Movement events
	virtual void OnStartTouchGround() override;
	virtual void OnStopTouchGround() override;
	virtual void OnChangeMoveType(MoveType_t oldMoveType) override;

	// Other events
	virtual void OnChangeTeamPost(i32 team) override;
	virtual void OnTeleport(const Vector *origin, const QAngle *angles, const Vector *velocity) override;

	void PlayErrorSound();

private:
	bool hideLegs {};
	f64 lastTeleportTime {};
	f32 lastValidYaw {};
	bool oldUsingTurnbinds {};

public:
	SurfAnticheatService *anticheatService {};
	SurfBeamService *beamService {};
	SurfZoneBeamService *zoneBeamService {};
	SurfCheckpointService *checkpointService {};
	SurfDatabaseService *databaseService {};
	SurfGlobalService *globalService {};
	SurfHUDService *hudService {};
	SurfLanguageService *languageService {};
	SurfModeService *modeService {};
	SurfNoclipService *noclipService {};
	SurfOptionService *optionService {};
	SurfQuietService *quietService {};
	SurfSpecService *specService {};
	SurfGotoService *gotoService {};
	SurfProfileService *profileService {};
	CUtlVector<SurfStyleService *> styleServices {};
	SurfTelemetryService *telemetryService {};
	SurfTimerService *timerService {};
	SurfTipService *tipService {};
	SurfTriggerService *triggerService {};

	void EnableGodMode();

	// Leg stuff
	void ToggleHideLegs();

	bool HidingLegs()
	{
		return this->hideLegs;
	}

	void UpdatePlayerModelAlpha();
	// Teleport checking, used for multiple services
	virtual bool JustTeleported(f32 threshold = SURF_RECENT_TELEPORT_THRESHOLD);
	// Triggerfix stuff

	// Hit all triggers from start to end with the specified bounds,
	// and call Touch/StartTouch on triggers that the player is touching.
	virtual void TouchTriggersAlongPath(const Vector &start, const Vector &end, const bbox_t &bounds);

	// Update the list of triggers that the player is touching, and call StartTouch/EndTouch appropriately.
	virtual void UpdateTriggerTouchList();

	// Print helpers
	virtual void PrintConsole(bool addPrefix, bool includeSpectators, const char *format, ...);
	virtual void PrintChat(bool addPrefix, bool includeSpectators, const char *format, ...); // Already supports colors.
	virtual void PrintCentre(bool addPrefix, bool includeSpectators, const char *format, ...);
	virtual void PrintAlert(bool addPrefix, bool includeSpectators, const char *format, ...);
	virtual void PrintHTMLCentre(bool addPrefix, bool includeSpectators, const char *format, ...);

	const CVValue_t *GetCvarValueFromModeStyles(const char *name);
};

class SurfBaseService
{
public:
	SurfPlayer *player;

	SurfBaseService(SurfPlayer *player)
	{
		this->player = player;
	}

	// To be implemented by each service class
	virtual void Reset() {}
};

class SurfPlayerManager : public MovementPlayerManager
{
public:
	SurfPlayerManager()
	{
		for (int i = 0; i < MAXPLAYERS + 1; i++)
		{
			delete players[i];
			players[i] = new SurfPlayer(i);
		}
	}

public:
	SurfPlayer *ToPlayer(CPlayerPawnComponent *component);
	SurfPlayer *ToPlayer(CBasePlayerController *controller);
	SurfPlayer *ToPlayer(CBasePlayerPawn *pawn);
	SurfPlayer *ToPlayer(CPlayerSlot slot);
	SurfPlayer *ToPlayer(CEntityIndex entIndex);
	SurfPlayer *ToPlayer(CPlayerUserId userID);
	SurfPlayer *ToPlayer(u32 index);
	SurfPlayer *SteamIdToPlayer(u64 steamID, bool validated = true);

	SurfPlayer *ToSurfPlayer(MovementPlayer *player)
	{
		return static_cast<SurfPlayer *>(player);
	}

	SurfPlayer *ToSurfPlayer(Player *player)
	{
		return static_cast<SurfPlayer *>(player);
	}
};

extern SurfPlayerManager *g_pSurfPlayerManager;

namespace Surf
{
	namespace misc
	{
		void Init();
		void OnServerActivate();
		void JoinTeam(SurfPlayer *player, int newTeam, bool restorePos = true);
		void ProcessConCommand(ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args);
		META_RES CheckBlockedRadioCommands(const char *cmd);
		void OnRoundStart();
		void InitTimeLimit();
		void EnforceTimeLimit();
		void UnrestrictTimeLimit();
		void OnPhysicsGameSystemFrameBoundary(void *pThis);
	} // namespace misc
}; // namespace Surf
