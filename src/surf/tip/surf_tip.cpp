#include "surf_tip.h"
#include "surf/timer/surf_timer.h"
#include "surf/language/surf_language.h"

#include <vendor/MultiAddonManager/public/imultiaddonmanager.h>

static_global KeyValues *pTipKeyValues;
static_global CUtlVector<const char *> tipNames;
static_global f64 tipInterval;
static_global i32 nextTipIndex;
static_global CTimer<> *tipTimer;

static_global class SurfTimerServiceEventListener_Tip : public SurfTimerServiceEventListener
{
	virtual void OnTimerStartPost(SurfPlayer *player, u32 courseGUID) override;
} timerEventListener;

extern IMultiAddonManager *g_pMultiAddonManager;

void SurfTipService::Init()
{
	LoadTips();
	ShuffleTips();
	tipTimer = StartTimer(PrintTips, true);
	SurfTimerService::RegisterEventListener(&timerEventListener);
}

void SurfTipService::Reset()
{
	this->showTips = true;
	this->teamJoinedAtLeastOnce = false;
	this->timerStartedAtLeastOnce = false;
}

void SurfTipService::ToggleTips()
{
	this->showTips = !this->showTips;
	player->languageService->PrintChat(true, false, this->showTips ? "Option - Tips - Enable" : "Option - Tips - Disable");
}

bool SurfTipService::ShouldPrintTip()
{
	return this->showTips;
}

void SurfTipService::PrintTip()
{
	this->player->languageService->PrintChat(true, false, tipNames[nextTipIndex]);
}

void SurfTipService::LoadTips()
{
	pTipKeyValues = new KeyValues("Tips");
	pTipKeyValues->UsesEscapeSequences(true);

	char buffer[1024];
	g_SMAPI->PathFormat(buffer, sizeof(buffer), "addons/cs2surf/translations/*.*");
	FileFindHandle_t findHandle = {};
	const char *fileName = g_pFullFileSystem->FindFirst(buffer, &findHandle);
	if (fileName)
	{
		do
		{
			char fullPath[1024];
			g_SMAPI->PathFormat(fullPath, sizeof(fullPath), "%s/addons/cs2surf/translations/%s", g_SMAPI->GetBaseDir(), fileName);
			if (V_strstr(fileName, "cs2surf-tips-"))
			{
				if (!pTipKeyValues->LoadFromFile(g_pFullFileSystem, fullPath, nullptr))
				{
					SURF_LOG_WARN(LogChannel::Tip, "Failed to load %s\n", fileName);
				}
			}
			fileName = g_pFullFileSystem->FindNext(findHandle);
		} while (fileName);
		g_pFullFileSystem->FindClose(findHandle);
	}

	FOR_EACH_SUBKEY(pTipKeyValues, it)
	{
		tipNames.AddToTail(it->GetName());
	}

	tipInterval = SurfOptionService::GetOptionFloat("tipInterval", SURF_DEFAULT_TIP_INTERVAL);
	delete pTipKeyValues;
}

void SurfTipService::ShuffleTips()
{
	for (int i = tipNames.Count() - 1; i > 0; --i)
	{
		int j = RandomInt(0, i);
		V_swap(tipNames.Element(i), tipNames.Element(j));
	}
}

SCMD(surf_tips, SCFL_MISC)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->tipService->ToggleTips();
	return true;
}

f64 SurfTipService::PrintTips()
{
	for (int i = 0; i <= MAX_PLAYERS; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		if (player->tipService->ShouldPrintTip())
		{
			player->tipService->PrintTip();
		}
	}
	nextTipIndex = (nextTipIndex + 1) % tipNames.Count();
	return tipInterval;
}

void SurfTipService::OnPlayerJoinTeam(i32 team)
{
	if (this->teamJoinedAtLeastOnce || (team != CS_TEAM_CT && team != CS_TEAM_T))
	{
		return;
	}

	this->teamJoinedAtLeastOnce = true;
	if (g_pMultiAddonManager)
	{
		this->player->languageService->PrintChat(true, false, "Menu Hint");
	}
}

void SurfTipService::OnTimerStartPost()
{
	if (this->timerStartedAtLeastOnce)
	{
		return;
	}

	this->teamJoinedAtLeastOnce = true;
	// TODO: Print no cheating stuff
}

void SurfTimerServiceEventListener_Tip::OnTimerStartPost(SurfPlayer *player, u32 courseGUID)
{
	player->tipService->OnTimerStartPost();
}
