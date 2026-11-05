#include "cs2surf.h"
#include "surf/surf.h"
#include "surf_replaysystem.h"
#include "bot.h"
#include "data.h"
#include "playback.h"
#include "events.h"
#include "commands.h"

namespace Surf::replaysystem
{

	void Init()
	{
		Surf::replaysystem::item::InitItemAttributes();
	}

	void Cleanup()
	{
		bot::KickBot();
		CleanupWatcher();
	}

	void OnRoundStart()
	{
		bot::KickBot();
	}

	void OnGameFrame()
	{
		// Process any completed async loads on the main thread
		data::ProcessAsyncLoadCompletion();
	}

	void OnPhysicsSimulate(SurfPlayer *player)
	{
		playback::OnPhysicsSimulate(player);
	}

	void OnProcessMovement(SurfPlayer *player)
	{
		playback::OnProcessMovement(player);
	}

	void OnProcessMovementPost(SurfPlayer *player)
	{
		playback::OnProcessMovementPost(player);
	}

	void OnFinishMovePre(SurfPlayer *player, CMoveData *pMoveData)
	{
		playback::OnFinishMovePre(player, pMoveData);
	}

	void OnPhysicsSimulatePost(SurfPlayer *player)
	{
		playback::OnPhysicsSimulatePost(player);
	}

	void OnPlayerRunCommandPre(SurfPlayer *player, CUserCmd *command)
	{
		playback::OnPlayerRunCommandPre(player, command);
	}

	bool IsReplayBot(SurfPlayer *player)
	{
		return bot::IsValidBot(player ? player->GetController() : nullptr);
	}

	bool CanTouchTrigger(SurfPlayer *player, CBaseTrigger *trigger)
	{
		// Don't care about non-bot players.
		if (!bot::IsValidBot(player->GetController()))
		{
			return true;
		}

		// Don't care about non timer triggers.
		const SurfTrigger *surfTrigger = Surf::mapapi::GetSurfTrigger(trigger);
		if (!surfTrigger)
		{
			return true;
		}

		if (Surf::mapapi::IsTimerTrigger(surfTrigger->type) || Surf::mapapi::IsTeleportTrigger(surfTrigger->type))
		{
			return false;
		}

		return true;
	}

	i32 GetCurrentCpIndex()
	{
		return data::GetCurrentCpIndex();
	}

	i32 GetCheckpointCount()
	{
		return data::GetCheckpointCount();
	}

	i32 GetTeleportCount()
	{
		return data::GetTeleportCount();
	}

	f32 GetTime()
	{
		return data::GetReplayTime();
	}

	f32 GetEndTime()
	{
		return data::GetEndTime();
	}

	bool GetPaused()
	{
		return data::GetPaused();
	}

} // namespace Surf::replaysystem
