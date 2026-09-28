#pragma once
#include "surf/surf.h"

class SurfCheckpointService : public SurfBaseService
{
public:
	SurfCheckpointService(SurfPlayer *player) : SurfBaseService(player)
	{
		this->checkpoints = CUtlVector<Checkpoint>(1, 0);
	}

	static void Init();
	virtual void Reset() override;

	// Checkpoint stuff
	struct Checkpoint
	{
		Vector origin;
		Vector velocity;
		QAngle angles;
		f64 time;
		Vector ladderNormal;
		bool onLadder {};
		CHandle<CBaseEntity> groundEnt;
	};

	// UndoTeleport stuff
	struct UndoTeleportData : public Checkpoint
	{
		bool teleportOnGround {};
	};

private:
	i32 currentCpIndex {};
	u32 tpCount {};
	bool holdingStill {};
	f32 teleportTime {};
	CUtlVector<Checkpoint> checkpoints;
	UndoTeleportData undoTeleportData;

	bool hasCustomStartPosition {};
	Checkpoint customStartPosition;
	Checkpoint lastTeleportedCheckpoint {};

public:
	void OnPlayerPreferenceLoaded();
	void ResetCheckpoints(bool playSound = false, bool resetTeleports = true);
	void SetCheckpoint();

	void UndoTeleport();
	void DoTeleport(const Checkpoint cp, bool respawn = false);
	void DoTeleport(i32 index);
	void TpHoldPlayerStill();
	void TpToCheckpoint();
	void TpToPrevCp();
	void TpToNextCp();

	u32 GetTeleportCount()
	{
		return this->tpCount;
	}

	i32 GetCurrentCpIndex()
	{
		if (this->checkpoints.Count() > 0)
		{
			return this->currentCpIndex + 1;
		}
		else
		{
			return this->currentCpIndex;
		}
	}

	i32 GetCheckpointCount()
	{
		return this->checkpoints.Count();
	}

	void SetStartPosition();
	void ClearStartPosition();

	bool HasCustomStartPosition()
	{
		return this->hasCustomStartPosition;
	}

	void TpToStartPosition();

	void PlayCheckpointSound();
	void PlayTeleportSound();
	void PlayCheckpointResetSound();
};
