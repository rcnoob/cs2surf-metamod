#ifndef SURF_REPLAYPLAYBACK_H
#define SURF_REPLAYPLAYBACK_H
#include "common.h"
#include "sdk/datatypes.h"

class SurfPlayer;
class CUserCmd;
class CMoveData;
struct TickData;
struct SubtickData;
class CBasePlayerWeapon;
struct EconInfo;

namespace Surf::replaysystem::playback
{
	// Core playback functions
	void OnPhysicsSimulate(SurfPlayer *player);
	void OnProcessMovement(SurfPlayer *player);
	void OnProcessMovementPost(SurfPlayer *player);
	void OnFinishMovePre(SurfPlayer *player, CMoveData *mv);
	void OnPhysicsSimulatePost(SurfPlayer *player);
	void OnPlayerRunCommandPre(SurfPlayer *player, CUserCmd *command);

	// Weapon management during playback
	void CheckWeapon(SurfPlayer &player, CUserCmd &cmd);
	void InitializeWeapons();

	// Playback state management
	void StartReplay();

	// Navigation support
	void ApplyTickState(SurfPlayer *player, const TickData *tickData);
} // namespace Surf::replaysystem::playback

#endif // SURF_REPLAYPLAYBACK_H
