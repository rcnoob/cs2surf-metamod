#include "../surf.h"
#include "cs2surf.h"
#include "surf_hud.h"
#include "sdk/datatypes.h"
#include "utils/utils.h"
#include "utils/simplecmds.h"

#include "surf/option/surf_option.h"
#include "surf/timer/surf_timer.h"
#include "surf/language/surf_language.h"

#include "tier0/memdbgon.h"

#define HUD_ON_GROUND_THRESHOLD 0.07f

static_global class SurfTimerServiceEventListener_HUD : public SurfTimerServiceEventListener
{
	virtual void OnTimerStopped(SurfPlayer *player, u32 courseGUID) override;
	virtual void OnTimerEndPost(SurfPlayer *player, u32 courseGUID, f32 time) override;
} timerEventListener;

static_global class SurfOptionServiceEventListener_HUD : public SurfOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(SurfPlayer *player)
	{
		player->hudService->ResetShowPanel();
	}
} optionEventListener;

void SurfHUDService::Init()
{
	SurfTimerService::RegisterEventListener(&timerEventListener);
	SurfOptionService::RegisterEventListener(&optionEventListener);
}

void SurfHUDService::Reset()
{
	this->showPanel = this->player->optionService->GetPreferenceBool("showPanel", true);
	this->timerStoppedTime = {};
	this->currentTimeWhenTimerStopped = {};
}

std::string SurfHUDService::GetSpeedText(const char *language)
{
	Vector velocity, baseVelocity;
	this->player->GetVelocity(&velocity);
	this->player->GetBaseVelocity(&baseVelocity);
	velocity += baseVelocity;
	// Keep the takeoff velocity on for a while after landing so the speed values flicker less.
	if ((this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND
		 && g_pSurfUtils->GetServerGlobals()->curtime - this->player->landingTime > HUD_ON_GROUND_THRESHOLD)
		|| (this->player->GetPlayerPawn()->m_MoveType == MOVETYPE_LADDER && !player->IsButtonPressed(IN_JUMP)))
	{
		return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Speed Text", velocity.Length2D());
	}
	return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Speed Text (Takeoff)", velocity.Length2D(),
													   this->player->takeoffVelocity.Length2D());
}

std::string SurfHUDService::GetKeyText(const char *language)
{
	// clang-format off

	return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Key Text",
		this->player->IsButtonPressed(IN_MOVELEFT) ? 'A' : '_',
		this->player->IsButtonPressed(IN_FORWARD) ? 'W' : '_',
		this->player->IsButtonPressed(IN_BACK) ? 'S' : '_',
		this->player->IsButtonPressed(IN_MOVERIGHT) ? 'D' : '_',
		this->player->IsButtonPressed(IN_DUCK) ? 'C' : '_',
		this->player->IsButtonPressed(IN_JUMP) ? 'J' : '_'
	);

	// clang-format on
}

std::string SurfHUDService::GetTimerText(const char *language)
{
	if (this->player->timerService->GetTimerRunning() || this->ShouldShowTimerAfterStop())
	{
		char timeText[128];

		// clang-format off

		f64 time = this->player->timerService->GetTimerRunning()
			? player->timerService->GetTime()
			: this->currentTimeWhenTimerStopped;


		SurfTimerService::FormatTime(time, timeText, sizeof(timeText));
		return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Timer Text",
			timeText,
			player->timerService->GetTimerRunning() ? "" : SurfLanguageService::PrepareMessageWithLang(language, "HUD - Stopped Text").c_str(),
			player->timerService->GetPaused() ? SurfLanguageService::PrepareMessageWithLang(language, "HUD - Paused Text").c_str() : ""
		);
		// clang-format on
	}
	return std::string("");
}

void SurfHUDService::DrawPanels(SurfPlayer *player, SurfPlayer *target)
{
	if (!target->hudService->IsShowingPanel())
	{
		return;
	}
	const char *language = target->languageService->GetLanguage();

	std::string keyText = player->hudService->GetKeyText(language);
	std::string timerText = player->hudService->GetTimerText(language);
	std::string speedText = player->hudService->GetSpeedText(language);

	// clang-format off
	std::string centerText = SurfLanguageService::PrepareMessageWithLang(language, "HUD - Center Text", 
		keyText.c_str(), timerText.c_str(), speedText.c_str());
	std::string alertText = SurfLanguageService::PrepareMessageWithLang(language, "HUD - Alert Text", 
		keyText.c_str(), timerText.c_str(), speedText.c_str());
	std::string htmlText = SurfLanguageService::PrepareMessageWithLang(language, "HUD - Html Center Text",
		keyText.c_str(), timerText.c_str(), speedText.c_str());

	// clang-format on

	centerText = centerText.substr(0, centerText.find_last_not_of('\n') + 1);
	alertText = alertText.substr(0, alertText.find_last_not_of('\n') + 1);
	htmlText = htmlText.substr(0, htmlText.find_last_not_of('\n') + 1);

	// Remove trailing newlines just in case a line is empty.
	if (!centerText.empty())
	{
		target->PrintCentre(false, false, centerText.c_str());
	}
	if (!alertText.empty())
	{
		target->PrintAlert(false, false, alertText.c_str());
	}
	if (!htmlText.empty())
	{
		target->PrintHTMLCentre(false, false, htmlText.c_str());
	}
}

void SurfHUDService::ResetShowPanel()
{
	this->showPanel = this->player->optionService->GetPreferenceBool("showPanel", true);
}

void SurfHUDService::TogglePanel()
{
	this->showPanel = !this->showPanel;
	this->player->optionService->SetPreferenceBool("showPanel", this->showPanel);
	if (!this->showPanel)
	{
		utils::PrintAlert(this->player->GetController(), "#SFUI_EmptyString");
		utils::PrintCentre(this->player->GetController(), "#SFUI_EmptyString");
		this->player->languageService->PrintHTMLCentre(false, false, "HUD - HTML Panel Disabled");
	}
}

void SurfHUDService::OnTimerStopped(f64 currentTimeWhenTimerStopped)
{
	// g_pSurfUtils->GetServerGlobals() becomes invalid when the plugin is unloading.
	if (g_SurfPlugin.unloading)
	{
		return;
	}
	this->timerStoppedTime = g_pSurfUtils->GetServerGlobals()->curtime;
	this->currentTimeWhenTimerStopped = currentTimeWhenTimerStopped;
}

void SurfTimerServiceEventListener_HUD::OnTimerStopped(SurfPlayer *player, u32 courseGUID)
{
	player->hudService->OnTimerStopped(player->timerService->GetTime());
}

void SurfTimerServiceEventListener_HUD::OnTimerEndPost(SurfPlayer *player, u32 courseGUID, f32 time)
{
	player->hudService->OnTimerStopped(time);
}

SCMD(surf_panel, SCFL_HUD)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->hudService->TogglePanel();
	if (player->hudService->IsShowingPanel())
	{
		player->languageService->PrintChat(true, false, "HUD Option - Info Panel - Enable");
	}
	else
	{
		player->languageService->PrintChat(true, false, "HUD Option - Info Panel - Disable");
	}
	return MRES_SUPERCEDE;
}
