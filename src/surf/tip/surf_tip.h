#pragma once
#include "surf/surf.h"
#include "utils/utils.h"
#include "utils/simplecmds.h"
#include "tier0/keyvalues.h"
#include "interfaces/interfaces.h"
#include "filesystem/ifilesystem.h"
#include "utils/ctimer.h"
#include "surf/option/surf_option.h"

class SurfTipService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

private:
	bool showTips {};
	bool teamJoinedAtLeastOnce {};
	bool timerStartedAtLeastOnce {};

public:
	virtual void Reset() override;
	void ToggleTips();
	static void Init();
	static f64 PrintTips();
	void OnPlayerJoinTeam(i32 team);
	void OnTimerStartPost();

private:
	bool ShouldPrintTip();
	void PrintTip();
	static void LoadTips();
	static void ShuffleTips();
};
