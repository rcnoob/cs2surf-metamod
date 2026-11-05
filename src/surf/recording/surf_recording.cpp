#include "cs2surf.h"
#include "surf_recording.h"
#include "surf/timer/surf_timer.h"
#include "surf/timer/submission.h"
#include "surf/checkpoint/surf_checkpoint.h"
#include "surf/replays/surf_replaysystem.h"
#include "surf/language/surf_language.h"
#include "surf/spec/surf_spec.h"
#include "utils/simplecmds.h"

#include "sdk/cskeletoninstance.h"
#include "sdk/usercmd.h"

#include "steam/steam_gameserver.h"
#include "filesystem/ifilesystem.h"
#include "vprof.h"

#include <set>

ReplayFileWriter *SurfRecordingService::fileWriter = nullptr;

// Not sure what's the best place to put this, so putting it here for now.
void SubtickData::RpSubtickMove::FromMove(const CSubtickMoveStep &move)
{
	this->when = move.when();
	this->button = move.button();
	if (move.button())
	{
		this->pressed = move.pressed();
		this->analogMove.pitch_delta = move.pitch_delta();
		this->analogMove.yaw_delta = move.yaw_delta();
	}
	else
	{
		this->analogMove.analog_forward_delta = move.analog_forward_delta();
		this->analogMove.analog_left_delta = move.analog_left_delta();
	}
}

void Recorder::Init(ReplayHeader &hdr, SurfPlayer *player, ReplayType type)
{
	hdr.set_version(SURF_REPLAY_VERSION);
	hdr.set_type(static_cast<cs2surf::replay::ReplayType>(type));

	// Map info
	hdr.mutable_map()->set_name(g_pSurfUtils->GetCurrentMapName().c_str());
	char md5[33] = {};
	g_pSurfUtils->GetCurrentMapMD5(md5, sizeof(md5));
	hdr.mutable_map()->set_md5(md5);

	hdr.set_plugin_version(g_SurfPlugin.GetVersion());
	hdr.set_server_version(utils::GetServerVersion());

	time_t unixTime = 0;
	time(&unixTime);
	hdr.set_timestamp((u64)unixTime);

	hdr.set_server_ip(SteamGameServer() ? SteamGameServer()->GetPublicIP().m_unIPv4 : 0);

	// Player info
	auto *playerMsg = hdr.mutable_player();
	playerMsg->set_name(player->GetName());
	playerMsg->set_steamid64(player->GetSteamId64());

	// Model name
	CSkeletonInstance *pSkeleton = static_cast<CSkeletonInstance *>(player->GetPlayerPawn()->m_CBodyComponent()->m_pSceneNode());
	hdr.set_model_name(pSkeleton->m_modelState().m_ModelName().String());

	hdr.set_sensitivity(utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(player->GetPlayerSlot(), "sensitivity")));
	hdr.set_yaw(utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(player->GetPlayerSlot(), "m_yaw")));
	hdr.set_pitch(utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(player->GetPlayerSlot(), "m_pitch")));
	hdr.set_viewmodel_offset_x(utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(player->GetPlayerSlot(), "viewmodel_offset_x")));
	hdr.set_viewmodel_offset_y(utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(player->GetPlayerSlot(), "viewmodel_offset_y")));
	hdr.set_viewmodel_offset_z(utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(player->GetPlayerSlot(), "viewmodel_offset_z")));
	hdr.set_viewmodel_fov(utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(player->GetPlayerSlot(), "viewmodel_fov")));
}

void SurfRecordingService::Reset()
{
	if (this->circularRecording)
	{
		this->circularRecording->tickData->Advance(this->circularRecording->tickData->GetReadAvailable());
		this->circularRecording->subtickData->Advance(this->circularRecording->subtickData->GetReadAvailable());
		this->circularRecording->cmdData->Advance(this->circularRecording->cmdData->GetReadAvailable());
		this->circularRecording->cmdSubtickData->Advance(this->circularRecording->cmdSubtickData->GetReadAvailable());
		this->circularRecording->rpEvents->Advance(this->circularRecording->rpEvents->GetReadAvailable());
	}
	this->runRecorders.clear();
	this->lastCmdNumReceived = 0;
	this->currentRunUUID = UUID_t(false);
	this->currentTickData = {};
	this->currentSubtickData = {};
	this->lastKnownMode = {};
	this->lastKnownStyles.clear();
	this->currentWeaponID = -1;
	this->weapons.clear();
}

void SurfRecordingService::RecordTickData_PhysicsSimulate()
{
	// Reset the tick data.
	this->currentTickData = {};
	this->currentSubtickData = {};

	this->currentTickData.serverTick = g_pSurfUtils->GetServerGlobals()->tickcount;
	this->currentTickData.gameTime = g_pSurfUtils->GetServerGlobals()->curtime;
	this->currentTickData.realTime = g_pSurfUtils->GetServerGlobals()->realtime;
	time_t unixTime = 0;
	time(&unixTime);
	this->currentTickData.unixTime = (u64)unixTime;

	this->player->GetOrigin(&this->currentTickData.pre.origin);
	this->player->GetVelocity(&this->currentTickData.pre.velocity);
	this->player->GetAngles(&this->currentTickData.pre.angles);
	auto movementServices = this->player->GetMoveServices();
	this->currentTickData.pre.buttons[0] = static_cast<u32>(movementServices->m_nButtons().m_pButtonStates[0]);
	this->currentTickData.pre.buttons[1] = static_cast<u32>(movementServices->m_nButtons().m_pButtonStates[1]);
	this->currentTickData.pre.buttons[2] = static_cast<u32>(movementServices->m_nButtons().m_pButtonStates[2]);
	this->currentTickData.pre.jumpPressedTime = movementServices->m_LegacyJump().m_flJumpPressedTime;
	this->currentTickData.pre.duckSpeed = movementServices->m_flDuckSpeed;
	this->currentTickData.pre.duckAmount = movementServices->m_flDuckAmount;
	this->currentTickData.pre.lastDuckTime = movementServices->m_flLastDuckTime;
	this->currentTickData.pre.replayFlags.ducking = movementServices->m_bDucking;
	this->currentTickData.pre.replayFlags.ducked = movementServices->m_bDucked;
	this->currentTickData.pre.replayFlags.desiresDuck = movementServices->m_bDesiresDuck;

	this->currentTickData.pre.entityFlags = this->player->GetPlayerPawn()->m_fFlags();
	this->currentTickData.pre.moveType = this->player->GetPlayerPawn()->m_nActualMoveType;
}

void SurfRecordingService::RecordTickData_SetupMove(CUserCmd *pc)
{
	this->currentTickData.cmdNumber = pc->cmdNum;
	this->currentTickData.clientTick = pc->base().client_tick();
	this->currentTickData.forward = pc->base().has_forwardmove() ? pc->base().forwardmove() : 0;
	this->currentTickData.left = pc->base().has_leftmove() ? pc->base().leftmove() : 0;
	this->currentTickData.up = pc->base().has_upmove() ? pc->base().upmove() : 0;
	this->currentTickData.pre.angles = {pc->base().viewangles().x(), pc->base().viewangles().y(), pc->base().viewangles().z()};
	this->currentTickData.leftHanded = this->player->GetPlayerPawn()->m_bLeftHanded() || pc->left_hand_desired();

	this->currentSubtickData.numSubtickMoves = pc->base().subtick_moves_size();
	for (u32 i = 0; i < this->currentSubtickData.numSubtickMoves && i < MAX_SUBTICK_MOVES; i++)
	{
		this->currentSubtickData.subtickMoves[i].FromMove(pc->base().subtick_moves(i));
	}
}

void SurfRecordingService::RecordTickData_PhysicsSimulatePost()
{
	this->EnsureCircularRecorderInitialized();

	this->player->GetOrigin(&this->currentTickData.post.origin);
	this->player->GetVelocity(&this->currentTickData.post.velocity);
	this->player->GetAngles(&this->currentTickData.post.angles);
	auto movementServices = this->player->GetMoveServices();
	this->currentTickData.post.buttons[0] = static_cast<u32>(movementServices->m_nButtons().m_pButtonStates[0]);
	this->currentTickData.post.buttons[1] = static_cast<u32>(movementServices->m_nButtons().m_pButtonStates[1]);
	this->currentTickData.post.buttons[2] = static_cast<u32>(movementServices->m_nButtons().m_pButtonStates[2]);
	this->currentTickData.pre.jumpPressedTime = movementServices->m_LegacyJump().m_flJumpPressedTime;
	this->currentTickData.post.duckSpeed = movementServices->m_flDuckSpeed;
	this->currentTickData.post.duckAmount = movementServices->m_flDuckAmount;
	this->currentTickData.post.lastDuckTime = movementServices->m_flLastDuckTime;
	this->currentTickData.post.replayFlags.ducking = movementServices->m_bDucking;
	this->currentTickData.post.replayFlags.ducked = movementServices->m_bDucked;
	this->currentTickData.post.replayFlags.desiresDuck = movementServices->m_bDesiresDuck;

	this->currentTickData.post.entityFlags = this->player->GetPlayerPawn()->m_fFlags();
	this->currentTickData.post.moveType = this->player->GetPlayerPawn()->m_nActualMoveType;

	this->currentTickData.weapon = this->currentWeaponID;
	// Push the tick data to the circular buffer and recorders.
	this->circularRecording->tickData->Write(this->currentTickData);
	this->PushToRecorders(this->currentTickData, RecorderType::Run);

	this->circularRecording->subtickData->Write(this->currentSubtickData);
	this->PushToRecorders<Recorder::Vec::Tick>(this->currentSubtickData, RecorderType::Run);
}

void SurfRecordingService::RecordCommand(CUserCmd *cmds, i32 numCmds)
{
	this->EnsureCircularRecorderInitialized();

	i32 currentTick = g_pSurfUtils->GetServerGlobals()->tickcount;
	for (i32 i = 0; i < numCmds; i++)
	{
		auto &pc = cmds[i];

		if (pc.cmdNum <= this->lastCmdNumReceived)
		{
			continue;
		}
		CmdData data;
		data.serverTick = currentTick;
		data.gameTime = g_pSurfUtils->GetServerGlobals()->curtime;
		data.realTime = g_pSurfUtils->GetServerGlobals()->realtime;
		time_t unixTime = 0;
		time(&unixTime);
		data.unixTime = (u64)unixTime;
		INetChannelInfo *netchan = interfaces::pEngine->GetPlayerNetInfo(this->player->GetPlayerSlot());
		if (netchan)
		{
			netchan->GetRemoteFramerate(&data.framerate, nullptr, nullptr);
			data.latency = netchan->GetEngineLatency();
			data.avgLoss = netchan->GetAvgLoss(FLOW_INCOMING) + netchan->GetAvgChoke(FLOW_INCOMING);
		}
		data.cmdNumber = pc.cmdNum;
		data.clientTick = pc.base().client_tick();
		data.forward = pc.base().has_forwardmove() ? pc.base().forwardmove() : 0;
		data.left = pc.base().has_leftmove() ? pc.base().leftmove() : 0;
		data.up = pc.base().has_upmove() ? pc.base().upmove() : 0;
		data.buttons[0] = pc.base().buttons_pb().buttonstate1();
		data.buttons[1] = pc.base().buttons_pb().buttonstate2();
		data.buttons[2] = pc.base().buttons_pb().buttonstate3();
		data.angles = {pc.base().viewangles().x(), pc.base().viewangles().y(), pc.base().viewangles().z()};
		data.mousedx = pc.base().mousedx();
		data.mousedy = pc.base().mousedy();

		// Record cvar values every tick (compression will handle optimization)
		data.sensitivity = utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(this->player->GetPlayerSlot(), "sensitivity"));
		data.m_yaw = utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(this->player->GetPlayerSlot(), "m_yaw"));
		data.m_pitch = utils::StringToFloat(interfaces::pEngine->GetClientConVarValue(this->player->GetPlayerSlot(), "m_pitch"));

		this->circularRecording->cmdData->Write(data);
		this->PushToRecorders(data, RecorderType::Run);

		SubtickData subtickData;
		subtickData.numSubtickMoves = pc.base().subtick_moves_size();
		for (u32 j = 0; j < subtickData.numSubtickMoves && j < MAX_SUBTICK_MOVES; j++)
		{
			subtickData.subtickMoves[j].FromMove(pc.base().subtick_moves(j));
		}
		this->circularRecording->cmdSubtickData->Write(subtickData);
		this->PushToRecorders<Recorder::Vec::Cmd>(subtickData, RecorderType::Run);
		this->lastCmdNumReceived = pc.cmdNum;
	}
}

void SurfRecordingService::CheckRecorders()
{
	for (auto it = this->runRecorders.begin(); it != this->runRecorders.end();)
	{
		auto &recorder = *it;
		if (recorder.ShouldStopAndSave(g_pSurfUtils->GetServerGlobals()->curtime))
		{
			SURF_LOG_DEBUG(LogChannel::Recording, "Run recorder stopped\n");
			if (SurfRecordingService::fileWriter)
			{
				auto recorderPtr = std::make_unique<RunRecorder>(std::move(recorder));
				this->CopyWeaponsToRecorder(recorderPtr.get());
				UUID_t localUUID = recorderPtr->uuid; // capture before move
				SurfRecordingService::fileWriter->QueueWrite(
					std::move(recorderPtr),
					// Success: deliver buffer to RunSubmission state machine
					[localUUID](const UUID_t &, f32, std::vector<char> &&buffer)
					{
						RunSubmission *sub = RunSubmission::GetByUUID(localUUID);
						if (sub)
						{
							sub->OnReplayReady(std::move(buffer));
						}
					},
					// Failure: notify the player and log
					[localUUID](const char *error)
					{
						SURF_LOG_WARN(LogChannel::Recording, "Run replay serialization failed for UUID %s: %s\n", localUUID.ToString().c_str(),
									  error);
						RunSubmission *sub = RunSubmission::GetByUUID(localUUID);
						if (sub)
						{
							SurfPlayer *callbackPlayer = g_pSurfPlayerManager->ToPlayer(sub->userID);
							if (callbackPlayer)
							{
								callbackPlayer->languageService->PrintChat(true, false, "Replay - Run Replay Failed");
							}
						}
					});
			}
			it = this->runRecorders.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void SurfRecordingService::CheckWeapons()
{
	this->EnsureCircularRecorderInitialized();
	this->currentWeaponID = -1;
	auto weapon = this->player->GetPlayerPawn()->m_pWeaponServices()->m_hActiveWeapon().Get();
	if (!weapon)
	{
		return;
	}
	auto weaponEconInfo = EconInfo(weapon);
	for (i32 i = 0; i < this->weapons.size(); i++)
	{
		if (weaponEconInfo == this->weapons[i])
		{
			this->currentWeaponID = i;
			return;
		}
	}
	// New weapon, record it
	this->weapons.push_back(weaponEconInfo);
	this->currentWeaponID = static_cast<i16>(this->weapons.size() - 1);
}

void SurfRecordingService::CheckModeStyles()
{
	this->EnsureCircularRecorderInitialized();

	auto currentModeInfo = Surf::mode::GetModeInfo(this->player->modeService);
	if (this->lastKnownMode.shortModeName != currentModeInfo.shortModeName || !SURF_STREQI(this->lastKnownMode.md5, currentModeInfo.md5))
	{
		this->lastKnownMode = currentModeInfo;
		this->InsertModeChangeEvent(currentModeInfo.longModeName.c_str(), currentModeInfo.md5);
		SURF_LOG_DEBUG(LogChannel::Recording, "Mode change event: %s\n", currentModeInfo.longModeName.c_str());
	}
	bool refreshStyles = this->player->styleServices.Count() != this->lastKnownStyles.size();
	if (!refreshStyles)
	{
		for (i32 i = 0; i < this->player->styleServices.Count(); i++)
		{
			if (!SURF_STREQI(this->lastKnownStyles[i].shortName, Surf::style::GetStyleInfo(this->player->styleServices[i]).shortName)
				|| !SURF_STREQI(this->lastKnownStyles[i].md5, Surf::style::GetStyleInfo(this->player->styleServices[i]).md5))
			{
				refreshStyles = true;
				break;
			}
		}
	}
	if (refreshStyles)
	{
		this->lastKnownStyles.clear();
		FOR_EACH_VEC(this->player->styleServices, i)
		{
			auto styleService = this->player->styleServices[i];
			auto styleInfo = Surf::style::GetStyleInfo(styleService);
			this->lastKnownStyles.push_back(styleInfo);
		}

		// Rebuild style state from scratch for replay consumers.
		bool clearStyles = true;
		if (this->lastKnownStyles.empty())
		{
			// Clear-only event represents transition to no active styles.
			this->InsertStyleChangeEvent("", "", true);
		}
		else
		{
			for (auto &style : this->lastKnownStyles)
			{
				this->InsertStyleChangeEvent(style.longName, style.md5, clearStyles);
				clearStyles = false;
			}
		}
		SURF_LOG_DEBUG(LogChannel::Recording, "Style change event: %u styles\n", (unsigned int)this->lastKnownStyles.size());
	}

	if (!this->circularRecording->earliestMode.has_value())
	{
		auto modeInfo = Surf::mode::GetModeInfo(this->player->modeService);
		this->circularRecording->earliestMode = RpModeStyleInfo();
		V_strncpy(this->circularRecording->earliestMode->name, modeInfo.longModeName.c_str(), sizeof(this->circularRecording->earliestMode->name));
		V_strncpy(this->circularRecording->earliestMode->md5, modeInfo.md5, sizeof(this->circularRecording->earliestMode->md5));
	}
	if (!this->circularRecording->earliestStyles.has_value())
	{
		this->circularRecording->earliestStyles = std::vector<RpModeStyleInfo>();

		FOR_EACH_VEC(this->player->styleServices, i)
		{
			auto styleService = this->player->styleServices[i];
			auto styleInfo = Surf::style::GetStyleInfo(styleService);
			RpModeStyleInfo style = {};
			V_strncpy(style.name, styleInfo.longName, sizeof(style.name));
			V_strncpy(style.md5, styleInfo.md5, sizeof(style.md5));
			this->circularRecording->earliestStyles->push_back(style);
		}
	}
}

void SurfRecordingService::CheckCheckpoints()
{
	this->currentTickData.checkpoint.index = this->player->checkpointService->GetCurrentCpIndex();
	this->currentTickData.checkpoint.checkpointCount = this->player->checkpointService->GetCheckpointCount();
	this->currentTickData.checkpoint.teleportCount = this->player->checkpointService->GetTeleportCount();
}

void SurfRecordingService::EnsureCircularRecorderInitialized()
{
	if (!this->circularRecording)
	{
		this->circularRecording = new CircularRecorder();
		SURF_LOG_INFO(LogChannel::Recording, "Initialized circular recorder for player %s\n", this->player->GetName());
	}
}

void SurfRecordingService::InsertEvent(const RpEvent &event)
{
	this->EnsureCircularRecorderInitialized();
	this->circularRecording->rpEvents->Write(event);
	this->PushToRecorders(event, RecorderType::Run);
}

void SurfRecordingService::InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TimerEventType type, f32 time, i32 index)
{
	RpEvent event;
	event.serverTick = this->currentTickData.serverTick;
	event.type = RpEventType::RPEVENT_TIMER_EVENT;
	event.data.timer.type = type;
	event.data.timer.index = index;
	event.data.timer.time = time;
	this->InsertEvent(event);
}

void SurfRecordingService::InsertTeleportEvent(const Vector *origin, const QAngle *angles, const Vector *velocity)
{
	RpEvent event;
	event.serverTick = this->currentTickData.serverTick;
	event.type = RpEventType::RPEVENT_TELEPORT;
	event.data.teleport.hasOrigin = origin != nullptr;
	event.data.teleport.hasAngles = angles != nullptr;
	event.data.teleport.hasVelocity = velocity != nullptr;
	if (origin)
	{
		for (i32 i = 0; i < 3; i++)
		{
			event.data.teleport.origin[i] = origin->operator[](i);
		}
	}
	if (angles)
	{
		for (i32 i = 0; i < 3; i++)
		{
			event.data.teleport.angles[i] = angles->operator[](i);
		}
	}
	if (velocity)
	{
		for (i32 i = 0; i < 3; i++)
		{
			event.data.teleport.velocity[i] = velocity->operator[](i);
		}
	}
	this->InsertEvent(event);
}

void SurfRecordingService::InsertModeChangeEvent(const char *name, const char *md5)
{
	RpEvent event;
	event.serverTick = this->currentTickData.serverTick;
	event.type = RpEventType::RPEVENT_MODE_CHANGE;
	V_strncpy(event.data.modeChange.name, name, sizeof(event.data.modeChange.name));
	V_strncpy(event.data.modeChange.md5, md5, sizeof(event.data.modeChange.md5));
	this->InsertEvent(event);
}

void SurfRecordingService::InsertStyleChangeEvent(const char *name, const char *md5, bool firstStyle)
{
	RpEvent event;
	event.serverTick = this->currentTickData.serverTick;
	event.type = RpEventType::RPEVENT_STYLE_CHANGE;
	V_strncpy(event.data.styleChange.name, name, sizeof(event.data.styleChange.name));
	V_strncpy(event.data.styleChange.md5, md5, sizeof(event.data.styleChange.md5));
	event.data.styleChange.clearStyles = firstStyle;
	this->InsertEvent(event);
}

void SurfRecordingService::CopyWeaponsToRecorder(Recorder *recorder)
{
	if (!recorder)
	{
		return;
	}

	// Copy weapons that are referenced in the tick data
	std::set<i32> referencedWeaponIndices;
	for (const auto &tick : recorder->tickData)
	{
		if (tick.weapon >= 0)
		{
			referencedWeaponIndices.insert(tick.weapon);
		}
	}
	SURF_LOG_DEBUG(LogChannel::Recording, "Copying %lu referenced weapons to recorder\n", referencedWeaponIndices.size());

	for (i32 weaponIndex : referencedWeaponIndices)
	{
		SURF_LOG_INFO(LogChannel::Recording, "Pushing weapon index %d to recorder\n", weaponIndex);
		auto weapon = this->weapons[weaponIndex];
		recorder->weaponTable.push_back({weaponIndex, weapon});
	}
}

SCMD(surf_rpsave, SCFL_REPLAY)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!g_pFullFileSystem || !player)
	{
		return true;
	}

	f32 duration = args->ArgC() > 1 ? utils::StringToFloat(args->Arg(1)) : 120.0f;
	if (!(duration >= 1.0f))
	{
		player->languageService->PrintChat(true, false, "Replay - Invalid Manual Replay Duration");
		return true;
	}

	SurfPlayer *target = player->IsAlive() ? player : player->specService->GetSpectatedPlayer();
	if (!target)
	{
		return true;
	}

	// Capture player userid for the callback (don't capture player pointer as it may be invalid)
	CPlayerUserId userID = player->GetClient()->GetUserID();

	target->recordingService->WriteCircularBufferToFileAsync(
		duration, "", player,
		// Success callback
		[userID](const UUID_t &uuid, f32 replayDuration)
		{
			SurfPlayer *callbackPlayer = g_pSurfPlayerManager->ToPlayer(userID);
			if (callbackPlayer)
			{
				callbackPlayer->languageService->PrintChat(true, false, "Replay - Manual Replay Saved", uuid.ToString().c_str(), replayDuration);
				callbackPlayer->languageService->PrintConsole(false, false, "Replay - Manual Replay Saved (Console)", uuid.ToString().c_str(),
															  replayDuration);
			}
		},
		// Failure callback
		[userID](const char *error)
		{
			SurfPlayer *callbackPlayer = g_pSurfPlayerManager->ToPlayer(userID);
			if (callbackPlayer)
			{
				callbackPlayer->languageService->PrintChat(true, false, "Replay - Manual Replay Failed");
			}
		});

	return true;
}
