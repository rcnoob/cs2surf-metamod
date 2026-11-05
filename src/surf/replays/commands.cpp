#define NOMINMAX
#include "cs2surf.h"
#include "surf/surf.h"
#include "surf/language/surf_language.h"
#include "surf/mode/surf_mode.h"
#include "surf/style/surf_style.h"
#include "surf/option/surf_option.h"
#include "commands.h"
#include "data.h"
#include "bot.h"
#include "events.h"
#include "playback.h"
#include "watcher.h"
#include "utils/uuid.h"
#include "utils/simplecmds.h"
#include <functional>

extern ReplayWatcher g_ReplayWatcher;

namespace Surf::replaysystem::commands
{

	void NavigateReplay(SurfPlayer *player, u32 targetTick)
	{
		auto replay = data::GetCurrentReplay();

		// Reset replay state and reprocess events up to target tick
		data::ResetReplayState(replay);
		events::ReprocessEventsUpToTick(replay, targetTick);

		// Set current tick
		replay->currentTick = targetTick;

		// Apply the target tick's state immediately
		auto bot = bot::GetBot();
		if (bot)
		{
			SurfPlayer *botPlayer = g_pSurfPlayerManager->ToPlayer(bot);
			if (botPlayer)
			{
				TickData *tickData = &replay->tickData[targetTick];
				playback::ApplyTickState(botPlayer, tickData);
			}
		}
	}

	void LoadReplay(SurfPlayer *player, const char *uuid)
	{
		if (!player)
		{
			return;
		}

		// Check if already loading
		if (data::IsLoading())
		{
			player->languageService->PrintChat(true, false, "Replay - Loading Already");
			return;
		}

		UUID_t parsedUuid;
		if (!UUID_t::FromString(uuid, &parsedUuid))
		{
			// Try to find replays matching the UUID substring
			auto matches = g_ReplayWatcher.FindReplaysByUUIDSubstring(uuid);
			if (matches.empty())
			{
				player->languageService->PrintChat(true, false, "Replay - Invalid UUID");
				return;
			}
			else if (matches.size() > 1)
			{
				player->languageService->PrintChat(true, false, "Replay - Multiple Matches", uuid);
				return;
			}
			// Exactly one match found
			parsedUuid = matches[0];
		}

		// Validate uuid format
		char replayPath[512];
		V_snprintf(replayPath, sizeof(replayPath), SURF_REPLAY_PATH "/%s.replay", parsedUuid.ToString().c_str());

		// Show loading message
		player->languageService->PrintChat(true, false, "Replay - Loading");

		// Get player user ID for thread-safe callback access
		CPlayerUserId playerUserID = player->GetClient()->GetUserID();

		// Start async loading
		// clang-format off
		data::LoadReplayAsync(
			replayPath,
			// Success callback (runs on main thread via ProcessAsyncLoadCompletion)
			data::LoadSuccessCallback([playerUserID]() {
				SurfPlayer* player = g_pSurfPlayerManager->ToPlayer(playerUserID);
				if (!player)
				{
					return;
				}
				if (!SURF_STREQI(data::GetCurrentReplay()->header.map().name().c_str(), g_pSurfUtils->GetCurrentMapName().c_str()))
				{
					player->languageService->PrintChat(true, false, "Replay - Wrong Map", 
						data::GetCurrentReplay()->header.map().name().c_str(), g_pSurfUtils->GetCurrentMapName().c_str());
					return;
				}
				for (u32 i = 0; i < data::GetCurrentReplay()->numEvents; i++)
				{
					auto& event = data::GetCurrentReplay()->events[i];
					switch (event.type)
					{
						case RPEVENT_MODE_CHANGE:
						{
							if (Surf::mode::GetModeInfo(event.data.modeChange.name).id < 0)
							{
								player->languageService->PrintChat(true, false, "Replay - Unknown Mode", event.data.modeChange.name);
							}
							break;
						}
						case RPEVENT_STYLE_CHANGE:
						{
							if (event.data.styleChange.name[0] != '\0' && Surf::style::GetStyleInfo(event.data.styleChange.name).id < 0)
							{
								player->languageService->PrintChat(true, false, "Replay - Unknown Style", event.data.styleChange.name);
							}
							break;
						}
					}
				}
				player->languageService->PrintChat(true, false, "Replay - Loaded Successfully");

				// Initialize bot and start playback (safe to call on main thread)
				// The replay data is already stored in the global g_currentReplay
				auto replay = data::GetCurrentReplay();
				bot::InitializeBotForReplay(replay->header);
				playback::StartReplay();
				playback::InitializeWeapons();
				bot::SpectateBot(player);
			}),
			// Failure callback (runs on main thread via ProcessAsyncLoadCompletion)
			data::LoadFailureCallback([playerUserID](const char* error) {
				SurfPlayer* player = g_pSurfPlayerManager->ToPlayer(playerUserID);
				if (player)
				{
					player->languageService->PrintChat(true, false, error);
				}
			})
		);
		// clang-format on
	}

	void JumpToReplayTime(SurfPlayer *player, const char *input)
	{
		if (!player)
		{
			return;
		}

		if (!data::IsReplayPlaying())
		{
			player->languageService->PrintChat(true, false, "Replay - No Replay Playing");
			return;
		}

		auto replay = data::GetCurrentReplay();
		u32 targetTick;
		bool isRelative = (input[0] == '+' || input[0] == '-');

		if (isRelative)
		{
			// Relative time seeking
			f64 seekSeconds = -1.0;

			if (!utils::ParseTimeString(input + 1, &seekSeconds)) // Skip the +/- sign
			{
				player->languageService->PrintChat(true, false, "Replay - Invalid Relative Time");
				return;
			}

			if (input[0] == '-')
			{
				seekSeconds = -seekSeconds;
			}

			// Calculate target tick based on current replay position + seek amount
			i32 ticksToSeek = (i32)(seekSeconds / ENGINE_FIXED_TICK_INTERVAL);
			i32 newTargetTick = (i32)replay->currentTick + ticksToSeek;

			// Clamp to valid range
			if (newTargetTick < 0)
			{
				targetTick = 0;
			}
			else if (newTargetTick >= (i32)replay->tickCount)
			{
				targetTick = replay->tickCount - 1;
			}
			else
			{
				targetTick = (u32)newTargetTick;
			}
		}
		else
		{
			// Absolute time navigation
			f64 targetSeconds = -1.0f;
			if (!utils::ParseTimeString(input, &targetSeconds))
			{
				player->languageService->PrintChat(true, false, "Replay - Invalid Absolute Time");
				return;
			}
			targetTick = (u32)(targetSeconds / ENGINE_FIXED_TICK_INTERVAL);
		}

		char time[32];
		utils::FormatTime(targetTick * ENGINE_FIXED_TICK_INTERVAL, time, sizeof(time), false);
		char maxTime[32];
		utils::FormatTime((replay->tickCount - 1) * ENGINE_FIXED_TICK_INTERVAL, maxTime, sizeof(maxTime), false);
		if (targetTick >= replay->tickCount)
		{
			player->languageService->PrintChat(true, false, "Replay - Time Out Of Range", time, maxTime);
			return;
		}

		NavigateReplay(player, targetTick);

		if (isRelative)
		{
			f64 seekSeconds = -1.0f;
			utils::ParseTimeString(input + 1, &seekSeconds);
			if (input[0] == '-')
			{
				seekSeconds = -seekSeconds;
			}
			player->languageService->PrintChat(true, false, "Replay - Seeked To Tick", seekSeconds, targetTick, time);
		}
		else
		{
			player->languageService->PrintChat(true, false, "Replay - Jumped To Tick", targetTick, time);
		}
	}

	void JumpToReplayTick(SurfPlayer *player, const char *input)
	{
		if (!player)
		{
			return;
		}

		if (!data::IsReplayPlaying())
		{
			player->languageService->PrintChat(true, false, "Replay - No Replay Playing");
			return;
		}

		auto replay = data::GetCurrentReplay();
		u32 targetTick;
		bool isRelative = (input[0] == '+' || input[0] == '-');

		if (isRelative)
		{
			// Relative tick seeking
			char *endPtr;
			long tickOffset = strtol(input + 1, &endPtr, 10); // Skip the +/- sign
			if (*endPtr != '\0' || tickOffset < 0)
			{
				player->languageService->PrintChat(true, false, "Replay - Invalid Tick Number");
				return;
			}

			if (input[0] == '-')
			{
				tickOffset = -tickOffset;
			}

			// Calculate target tick based on current replay position + tick offset
			i32 newTargetTick = (i32)replay->currentTick + tickOffset;

			// Clamp to valid range
			if (newTargetTick < 0)
			{
				targetTick = 0;
			}
			else if (newTargetTick >= (i32)replay->tickCount)
			{
				targetTick = replay->tickCount - 1;
			}
			else
			{
				targetTick = (u32)newTargetTick;
			}
		}
		else
		{
			// Absolute tick navigation
			char *endPtr;
			long tickValue = strtol(input, &endPtr, 10);
			if (*endPtr != '\0' || tickValue < 0)
			{
				player->languageService->PrintChat(true, false, "Replay - Invalid Tick Number");
				return;
			}
			targetTick = (u32)tickValue;
		}

		if (targetTick >= replay->tickCount)
		{
			player->languageService->PrintChat(true, false, "Replay - Tick Out Of Range", targetTick, replay->tickCount - 1);
			return;
		}

		NavigateReplay(player, targetTick);
		char time[32];
		utils::FormatTime(targetTick * ENGINE_FIXED_TICK_INTERVAL, time, sizeof(time), false);
		player->languageService->PrintChat(true, false, "Replay - Jumped To Tick", targetTick, time);
	}

	void GetReplayInfo(SurfPlayer *player)
	{
		if (!player)
		{
			return;
		}

		if (!data::IsReplayPlaying())
		{
			player->languageService->PrintChat(true, false, "Replay - No Replay Playing");
			return;
		}

		auto replay = data::GetCurrentReplay();

		char timeStr[64], maxTime[64];
		utils::FormatTime(replay->currentTick * ENGINE_FIXED_TICK_INTERVAL, timeStr, sizeof(timeStr), false);
		utils::FormatTime((replay->tickCount - 1) * ENGINE_FIXED_TICK_INTERVAL, maxTime, sizeof(maxTime), false);
		char timestamp[64];
		time_t time = replay->header.timestamp();
		strftime(timestamp, 64, "%Y-%m-%d %H:%M:%S", localtime(&time));
		player->languageService->PrintChat(true, false, "Replay - Current Info", replay->currentTick, replay->tickCount - 1, timeStr, maxTime);
		player->languageService->PrintConsole(false, false, "Replay - General Info Console", replay->uuid.ToString().c_str(),
											  replay->header.player().name().c_str(), replay->header.player().steamid64(), timestamp,
											  replay->header.server_version(), replay->header.plugin_version());
		switch (static_cast<ReplayType>(replay->header.type()))
		{
			case ReplayType::RP_CHEATER:
			{
				if (replay->header.has_cheater())
				{
					player->languageService->PrintConsole(false, false, "Replay - Cheater Info Console", replay->header.cheater().reason().c_str());
				}
				break;
			}
			case ReplayType::RP_RUN:
			{
				if (replay->header.has_run())
				{
					char modeStr[128];
					auto &run = replay->header.run();
					i32 styleCount = run.styles_size();
					V_snprintf(modeStr, sizeof(modeStr), "%s%s", run.mode().name().c_str(), styleCount ? "*" : "");
					std::string timeString = utils::FormatTime(run.time());
					player->languageService->PrintConsole(false, false, "Replay - Run Info Console", run.course_name().c_str(), modeStr,
														  timeString.c_str());
				}
				break;
			}
			case ReplayType::RP_MANUAL:
			{
				if (replay->header.has_manual())
				{
					auto &manual = replay->header.manual();
					if (manual.has_saved_by())
					{
						player->languageService->PrintConsole(false, false, "Replay - Manual Info Console", manual.saved_by().name().c_str(),
															  manual.saved_by().steamid64());
					}
				}
				break;
			}
		}
	}

	void ToggleReplayPause(SurfPlayer *player)
	{
		if (!player)
		{
			return;
		}

		if (!data::IsReplayPlaying())
		{
			player->languageService->PrintChat(true, false, "Replay - No Replay Playing");
			return;
		}

		auto replay = data::GetCurrentReplay();

		// Toggle pause state
		replay->replayPaused = !replay->replayPaused;

		if (replay->replayPaused)
		{
			player->languageService->PrintChat(true, false, "Replay - Paused");
		}
		else
		{
			player->languageService->PrintChat(true, false, "Replay - Resumed");
		}
	}

	void CheckReplayLoadProgress(SurfPlayer *player)
	{
		if (!player)
		{
			return;
		}

		auto status = data::GetLoadStatus();

		switch (status->state.load())
		{
			case data::LoadingState::Idle:
				player->languageService->PrintChat(true, false, "Replay - No Loading Progress");
				break;

			case data::LoadingState::Loading:
			{
				float progress = status->progress.load() * 100.0f;
				player->languageService->PrintChat(true, false, "Replay - Loading Progress", progress);
				break;
			}

			case data::LoadingState::Completed:
				player->languageService->PrintChat(true, false, "Replay - Loading Completed");
				break;

			case data::LoadingState::Failed:
			{
				std::lock_guard<std::mutex> lock(status->errorMutex);
				player->languageService->PrintChat(true, false, status->errorMessage.c_str());
				break;
			}
		}
	}

	void CancelReplayLoad(SurfPlayer *player)
	{
		if (!player)
		{
			return;
		}

		if (!data::IsLoading())
		{
			player->languageService->PrintChat(true, false, "Replay - No Loading Progress");
			return;
		}

		data::CancelAsyncLoad();
		player->languageService->PrintChat(true, false, "Replay - Loading Cancelled");
	}

	void ListReplays(SurfPlayer *player, const char *filter)
	{
		if (!player)
		{
			return;
		}
		if (!filter || filter[0] == '\0')
		{
			g_ReplayWatcher.PrintUsage(player);
		}
		g_ReplayWatcher.FindReplaysMatchingCriteria(filter, player);
	}

	void ToggleLegsVisibility(SurfPlayer *player)
	{
		if (!player)
		{
			return;
		}
		if (!data::IsReplayPlaying())
		{
			player->languageService->PrintChat(true, false, "Replay - No Replay Playing");
			return;
		}
		bot::GetBotPlayer()->ToggleHideLegs();
		if (bot::GetBotPlayer()->optionService->GetPreferenceBool("hideLegs"))
		{
			player->languageService->PrintChat(true, false, "Replay - Hide Player Legs - Enable");
		}
		else
		{
			player->languageService->PrintChat(true, false, "Replay - Hide Player Legs - Disable");
		}
	}
} // namespace Surf::replaysystem::commands

// Command implementations using the new modules
SCMD(surf_replay, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!g_pFullFileSystem || !player)
	{
		return true;
	}

	if (args->ArgC() < 2)
	{
		player->languageService->PrintChat(true, false, "Replay - Usage Command");
		return true;
	}

	Surf::replaysystem::commands::LoadReplay(player, args->Arg(1));
	return true;
}

SCMD(surf_rpgoto, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!player)
	{
		return true;
	}

	if (args->ArgC() < 2)
	{
		player->languageService->PrintChat(true, false, "Replay - Usage Goto Time");
		return true;
	}

	Surf::replaysystem::commands::JumpToReplayTime(player, args->ArgS());
	return true;
}

SCMD(surf_rpgototick, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!player)
	{
		return true;
	}

	if (args->ArgC() < 2)
	{
		player->languageService->PrintChat(true, false, "Replay - Usage Goto Tick");
		return true;
	}

	Surf::replaysystem::commands::JumpToReplayTick(player, args->Arg(1));
	return true;
}

SCMD(surf_rpinfo, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!player)
	{
		return true;
	}

	Surf::replaysystem::commands::GetReplayInfo(player);
	return true;
}

SCMD_LINK(surf_rpseek, surf_rpgoto);

SCMD(surf_rppause, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!player)
	{
		return true;
	}

	Surf::replaysystem::commands::ToggleReplayPause(player);
	return true;
}

SCMD(surf_rploadprogress, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!player)
	{
		return true;
	}

	Surf::replaysystem::commands::CheckReplayLoadProgress(player);
	return true;
}

SCMD(surf_rpcancelload, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!player)
	{
		return true;
	}

	Surf::replaysystem::commands::CancelReplayLoad(player);
	return true;
}

SCMD(surf_replays, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!player)
	{
		return true;
	}

	Surf::replaysystem::commands::ListReplays(player, args->ArgS());
	return true;
}

SCMD(surf_rphidelegs, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!player)
	{
		return true;
	}
	Surf::replaysystem::commands::ToggleLegsVisibility(player);
	return true;
}
