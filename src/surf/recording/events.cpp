#include "cs2surf.h"
#include "surf_recording.h"
#include "surf/timer/surf_timer.h"
#include "surf/checkpoint/surf_checkpoint.h"
#include "surf/replays/surf_replaysystem.h"

// Timer event listener implementation
class TimerEventListener : public SurfTimerServiceEventListener
{
public:
	virtual void OnTimerStartPost(SurfPlayer *player, u32 courseGUID) override
	{
		player->recordingService->OnTimerStart();
	}

	virtual void OnTimerEndPost(SurfPlayer *player, u32 courseGUID, f32 time) override
	{
		player->recordingService->OnTimerEnd();
	}

	virtual void OnTimerStopped(SurfPlayer *player, u32 courseGUID) override
	{
		player->recordingService->OnTimerStop();
	}

	virtual void OnTimerInvalidated(SurfPlayer *player) override
	{
		// player->PrintChat(false, false, "timerinvalidated");
	}

	virtual void OnPausePost(SurfPlayer *player) override
	{
		player->recordingService->OnPause();
	}

	virtual void OnResumePost(SurfPlayer *player) override
	{
		player->recordingService->OnResume();
	}

	virtual void OnCheckpointZoneTouchPost(SurfPlayer *player, u32 checkpoint) override
	{
		player->recordingService->OnCPZ(checkpoint);
	}

	virtual void OnStageZoneTouchPost(SurfPlayer *player, u32 stageZone) override
	{
		player->recordingService->OnStage(stageZone);
	}
} timerEventListener;

void SurfRecordingService::Init()
{
	SurfTimerService::RegisterEventListener(&timerEventListener);
	fileWriter = new ReplayFileWriter();
}

void SurfRecordingService::Shutdown()
{
	SurfTimerService::UnregisterEventListener(&timerEventListener);
	fileWriter->Stop();
	delete fileWriter;
	fileWriter = nullptr;
}

void SurfRecordingService::OnActivateServer()
{
	for (i32 i = 0; i < MAX_PLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		if (player && player->recordingService)
		{
			player->recordingService->Reset();
		}
	}
}

void SurfRecordingService::ProcessFileWriteCompletion()
{
	if (SurfRecordingService::fileWriter)
	{
		SurfRecordingService::fileWriter->RunFrame();
	}
}

void SurfRecordingService::OnProcessUsercmds(CUserCmd *cmds, i32 numCmds)
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (numCmds <= 0)
	{
		return;
	}

	// record data for replay playback
	if (this->player->IsFakeClient())
	{
		return;
	}

	this->RecordCommand(cmds, numCmds);
}

void SurfRecordingService::OnTimerStart()
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	this->runRecorders.push_back(RunRecorder(this->player));
	SURF_LOG_DEBUG(LogChannel::Recording, "Timer start\n");
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_START, this->player->timerService->GetTime(),
						   this->player->timerService->GetCourse()->id);
	// Reset currentRunUUID to invalid state at timer start
	this->currentRunUUID = UUID_t(false);
}

void SurfRecordingService::OnTimerStop()
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	SURF_LOG_DEBUG(LogChannel::Recording, "Timer stop\n");
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_STOP, this->player->timerService->GetTime());

	// Remove all active run recorders, which are the ones without desired stop time set.
	auto it = this->runRecorders.begin();
	while (it != this->runRecorders.end())
	{
		if (it->desiredStopTime < 0.0f)
		{
			it = this->runRecorders.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void SurfRecordingService::OnTimerEnd()
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	SURF_LOG_DEBUG(LogChannel::Recording, "Timer end\n");
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_END, this->player->timerService->GetTime(),
						   this->player->timerService->GetCourse()->id);

	for (auto &recorder : this->runRecorders)
	{
		if (recorder.desiredStopTime < 0.0f)
		{
			recorder.End(this->player->timerService->GetTime(), this->player->checkpointService->GetTeleportCount());
			// Generate UUID now (at timer end) so it's available for RecordAnnounce
			// File will be written later after breather time
			recorder.uuid = UUID_t(true);
			this->currentRunUUID = recorder.uuid;
		}
	}
}

void SurfRecordingService::OnPause()
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	SURF_LOG_DEBUG(LogChannel::Recording, "Timer pause\n");
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_PAUSE,
						   this->player->timerService->GetTimerRunning() ? this->player->timerService->GetTime() : 0.0f);
}

void SurfRecordingService::OnResume()
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	SURF_LOG_DEBUG(LogChannel::Recording, "Timer resume\n");
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_RESUME,
						   this->player->timerService->GetTimerRunning() ? this->player->timerService->GetTime() : 0.0f);
}

void SurfRecordingService::OnSplit(i32 split)
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	SURF_LOG_DEBUG(LogChannel::Recording, "Timer split %d\n", split);
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_SPLIT, this->player->timerService->GetTime(), split);
}

void SurfRecordingService::OnCPZ(i32 cpz)
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	SURF_LOG_DEBUG(LogChannel::Recording, "Checkpoint %d\n", cpz);
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_CPZ, this->player->timerService->GetTime(), cpz);
}

void SurfRecordingService::OnStage(i32 stage)
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	SURF_LOG_DEBUG(LogChannel::Recording, "Stage %d\n", stage);
	this->InsertTimerEvent(RpEvent::RpEventData::TimerEvent::TIMER_STAGE, this->player->timerService->GetTime(), stage);
}

void SurfRecordingService::OnTeleport(const Vector *origin, const QAngle *angles, const Vector *velocity)
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	SURF_LOG_DEBUG(LogChannel::Recording, "Teleport\n");
	this->InsertTeleportEvent(origin, angles, velocity);
}

void SurfRecordingService::OnClientDisconnect()
{
	for (auto &recorder : this->runRecorders)
	{
		if (recorder.desiredStopTime > 0.0f && fileWriter)
		{
			auto recorderPtr = std::make_unique<RunRecorder>(std::move(recorder));
			this->CopyWeaponsToRecorder(recorderPtr.get());
			fileWriter->QueueWriteToFile(std::move(recorderPtr));
		}
	}
	this->runRecorders.clear();

	// Clean up circular recorder when player disconnects
	delete this->circularRecording;
	this->circularRecording = nullptr;
}

void SurfRecordingService::OnPhysicsSimulate()
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}

	// record data for replay playback
	if (!this->player->IsAlive() || this->player->IsFakeClient())
	{
		return;
	}

	this->RecordTickData_PhysicsSimulate();
}

void SurfRecordingService::OnSetupMove(CUserCmd *pc)
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}
	if (!this->player->IsAlive() || this->player->IsFakeClient())
	{
		return;
	}
	this->RecordTickData_SetupMove(pc);
}

void SurfRecordingService::OnPhysicsSimulatePost()
{
	if (this->player->IsFakeClient() || Surf::replaysystem::IsReplayBot(this->player))
	{
		return;
	}

	this->CheckRecorders();

	// record data for replay playback
	if (!this->player->IsAlive())
	{
		return;
	}
	this->CheckWeapons();
	this->RecordTickData_PhysicsSimulatePost();
	this->CheckModeStyles();
	this->CheckCheckpoints();

	// Remove old events from circular buffer (keep 2 minutes)
	u32 currentTick = g_pSurfUtils->GetServerGlobals()->tickcount;
	if (this->circularRecording)
	{
		this->circularRecording->TrimOldData(currentTick);
	}
}
