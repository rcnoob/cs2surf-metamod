#pragma once
#include "surf/surf.h"

class SurfSpecService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

private:
	bool savedPosition;
	Vector savedOrigin;
	QAngle savedAngles;
	bool savedOnLadder;

public:
	virtual void Reset() override;
	static void Init();
	bool HasSavedPosition();
	void SavePosition();
	void LoadPosition();
	void ResetSavedPosition();

	bool IsSpectating(SurfPlayer *target);
	bool SpectatePlayer(const char *playerName);
	bool SpectatePlayer(SurfPlayer *target);
	bool CanSpectate();

	void GetSpectatorList(CUtlVector<std::string> &spectatorList);
	SurfPlayer *GetSpectatedPlayer();
	SurfPlayer *GetNextSpectator(SurfPlayer *current);
};
