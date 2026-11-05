
#ifndef SURF_REPLAYSYSTEM_H
#define SURF_REPLAYSYSTEM_H
#include "sdk/datatypes.h"

class SurfPlayer;
class CCSPlayerPawnBase;
class CUserCmd;
class CBasePlayerWeapon;
struct EconInfo;
class CBaseTrigger;

namespace Surf::replaysystem
{
	void Init();
	void Cleanup();
	void OnRoundStart();
	void OnGameFrame();
	void OnPhysicsSimulate(SurfPlayer *player);
	void OnProcessMovement(SurfPlayer *player);
	void OnProcessMovementPost(SurfPlayer *player);
	void OnFinishMovePre(SurfPlayer *player, CMoveData *pMoveData);
	void OnPhysicsSimulatePost(SurfPlayer *player);
	void OnPlayerRunCommandPre(SurfPlayer *player, CUserCmd *command);
	bool IsReplayBot(SurfPlayer *player);
	bool CanTouchTrigger(SurfPlayer *player, CBaseTrigger *trigger);

	namespace item
	{
		void InitItemAttributes();
		std::string GetItemAttributeName(u16 id);
		std::string GetWeaponName(u16 id);
		gear_slot_t GetWeaponGearSlot(u16 id);
		bool DoesPaintKitUseLegacyModel(float paintKit);
		void ApplyItemAttributesToWeapon(CBasePlayerWeapon &weapon, const EconInfo &info);
		void ApplyModelAttributesToPawn(CCSPlayerPawn *pawn, const EconInfo &info, const char *modelName);
	} // namespace item

	i32 GetCurrentCpIndex();
	i32 GetCheckpointCount();
	i32 GetTeleportCount();
	f32 GetTime();
	f32 GetEndTime();
	bool GetPaused();

	void InitWatcher();
	void CleanupWatcher();
} // namespace Surf::replaysystem

#endif // Surf_REPLAYSYSTEM_H
