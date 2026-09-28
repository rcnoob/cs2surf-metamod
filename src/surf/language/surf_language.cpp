#include "surf_language.h"
#include "utils/utils.h"
#include "utils/simplecmds.h"
#include "KeyValues.h"
#include "interfaces/interfaces.h"
#include "filesystem.h"
#include "utils/ctimer.h"
#include "surf/option/surf_option.h"
#include "surf/checkpoint/surf_checkpoint.h"
#include "surf/timer/surf_timer.h"

#include "utils/cvarquery.h"
#include <vendor/MultiAddonManager/public/imultiaddonmanager.h>

extern IMultiAddonManager *g_pMultiAddonManager;

static_global class SurfOptionServiceEventListener_Language : public SurfOptionServiceEventListener
{
	virtual void OnPlayerPreferenceLoaded(SurfPlayer *player)
	{
		player->languageService->OnPlayerPreferenceLoaded();
	}
} optionEventListener;

static_global KeyValues *translationKV;
static_global KeyValues *languagesKV;
static_global KeyValues *addonsKV;

void SurfLanguageService::Init()
{
	SurfLanguageService::LoadConfigFiles();
	SurfOptionService::RegisterEventListener(&optionEventListener);
}

void SurfLanguageService::LoadConfigFiles()
{
	if (translationKV)
	{
		delete translationKV;
	}
	if (languagesKV)
	{
		delete languagesKV;
	}
	if (addonsKV)
	{
		delete addonsKV;
	}
	translationKV = new KeyValues("Phrases");
	translationKV->UsesEscapeSequences(true);
	languagesKV = new KeyValues("Languages");
	languagesKV->UsesEscapeSequences(true);
	addonsKV = new KeyValues("Addons");
	addonsKV->UsesEscapeSequences(true);
	SurfLanguageService::LoadTranslations();
	SurfLanguageService::LoadLanguages();
}

void SurfLanguageService::Cleanup()
{
	if (translationKV)
	{
		delete translationKV;
	}
	if (languagesKV)
	{
		delete languagesKV;
		if (addonsKV)
		{
			delete addonsKV;
		}
	}
}

static_global std::vector<SurfLanguageService::AvailableLanguage> availableLanguages;

const std::vector<SurfLanguageService::AvailableLanguage> &SurfLanguageService::GetAvailableLanguages()
{
	if (!availableLanguages.empty() || !translationKV)
	{
		return availableLanguages;
	}

	std::vector<i32> phraseCounts;
	i32 totalPhrases = 0;
	for (KeyValues *phrase = translationKV->GetFirstSubKey(); phrase; phrase = phrase->GetNextKey())
	{
		totalPhrases++;
		for (KeyValues *lang = phrase->GetFirstSubKey(); lang; lang = lang->GetNextKey())
		{
			const char *code = lang->GetName();
			if (!code || code[0] == '#')
			{
				continue;
			}
			i32 index = -1;
			for (i32 i = 0; i < (i32)availableLanguages.size(); i++)
			{
				if (V_stricmp(availableLanguages[i].code.Get(), code) == 0)
				{
					index = i;
					break;
				}
			}
			if (index < 0)
			{
				// config.txt maps a Steam language name to each code; use the first one as the label.
				CUtlString steamName = code;
				for (KeyValues *named = languagesKV ? languagesKV->GetFirstSubKey() : nullptr; named; named = named->GetNextKey())
				{
					if (V_stricmp(named->GetString(nullptr, ""), code) == 0)
					{
						steamName = named->GetName();
						break;
					}
				}
				availableLanguages.push_back({code, steamName, 0});
				phraseCounts.push_back(0);
				index = (i32)availableLanguages.size() - 1;
			}
			phraseCounts[index]++;
		}
	}
	for (i32 i = 0; i < (i32)availableLanguages.size(); i++)
	{
		availableLanguages[i].coverage = totalPhrases > 0 ? phraseCounts[i] * 100 / totalPhrases : 0;
	}
	return availableLanguages;
}

void SurfLanguageService::LoadLanguages()
{
	char fullPath[1024];
	g_SMAPI->PathFormat(fullPath, sizeof(fullPath), "%s/addons/cs2surf/translations/config.txt", g_SMAPI->GetBaseDir());
	if (!languagesKV->LoadFromFile(g_pFullFileSystem, fullPath, nullptr))
	{
		META_CONPRINT("Failed to load translation config file.\n");
	}
	g_SMAPI->PathFormat(fullPath, sizeof(fullPath), "%s/addons/cs2surf/translations/menu-addons.txt", g_SMAPI->GetBaseDir());
	if (!addonsKV->LoadFromFile(g_pFullFileSystem, fullPath, nullptr))
	{
		META_CONPRINT("Failed to load addon config file.\n");
	}
}

void SurfLanguageService::LoadTranslations()
{
	char buffer[1024];
	g_SMAPI->PathFormat(buffer, sizeof(buffer), "addons/cs2surf/translations/*.phrases.txt");
	FileFindHandle_t findHandle = {};
	const char *fileName = g_pFullFileSystem->FindFirst(buffer, &findHandle);
	if (fileName)
	{
		do
		{
			char fullPath[1024];
			g_SMAPI->PathFormat(fullPath, sizeof(fullPath), "%s/addons/cs2surf/translations/%s", g_SMAPI->GetBaseDir(), fileName);
			if (!translationKV->LoadFromFile(g_pFullFileSystem, fullPath, nullptr))
			{
				META_CONPRINTF("Failed to load %s\n", fileName);
			}
			fileName = g_pFullFileSystem->FindNext(findHandle);
		} while (fileName);
		g_pFullFileSystem->FindClose(findHandle);
	}
}

void SurfLanguageService::OnPlayerPreferenceLoaded()
{
	const char *language = this->player->optionService->GetPreferenceStr("preferredLanguage");
	bool shouldReconnect = !(this->player->checkpointService->GetCheckpointCount() || this->player->timerService->GetTimerRunning());
	if (language[0])
	{
		SurfLanguageService::UpdateLanguage(this->player->GetSteamId64(false), language, LanguageInfo::CacheLevel::CACHE_PREF, shouldReconnect);
		if (!shouldReconnect)
		{
			this->player->languageService->PrintChat(false, false, "Language Change - Manual Menu Change Required");
		}
	}
}

const char *SurfLanguageService::GetLanguage()
{
	return SurfLanguageService::clientLanguageInfos[this->player->GetSteamId64(false)].language;
}

const char *SurfLanguageService::GetTranslatedFormat(const char *language, const char *phrase)
{
	if (!translationKV->FindKey(phrase))
	{
		// META_CONPRINTF("Warning: Phrase '%s' not found, returning orignal message!\n", phrase);
		return phrase;
	}
	const char *outFormat = translationKV->FindKey(phrase)->GetString(language);
	if (outFormat[0] == '\0')
	{
		if (!V_stricmp(language, "#format"))
		{
			// It is fine to have no format.
			return NULL;
		}
		// META_CONPRINTF("Warning: Phrase '%s' not found for language %s!\n", phrase, language);
		return translationKV->FindKey(phrase)->GetString(SURF_DEFAULT_LANGUAGE);
	}
	return outFormat;
}

void SurfLanguageService::UpdateLanguage(u64 xuid, const char *langKey, LanguageInfo::CacheLevel cacheLevel, bool shouldReconnect)
{
	// Manual override > Loaded preference > Queried ConVar
	auto &langInfo = SurfLanguageService::clientLanguageInfos[xuid];
	const char *addon = addonsKV->GetString(langKey, addonsKV->GetString(SURF_DEFAULT_LANGUAGE));
	if (langInfo.cacheLevel > cacheLevel)
	{
		return;
	}
	langInfo.cacheLevel = cacheLevel;
	if (!SURF_STREQILEN(langInfo.lastAddon, addon, sizeof(langInfo.lastAddon)))
	{
		if (SURF_STREQI(langInfo.lastAddon, SURF_WORKSHOP_ADDON_ID))
		{
			META_CONPRINTF("[Surf::Language] Adding %s for client %lli\n", addon, xuid);
		}
		else
		{
			META_CONPRINTF("[Surf::Language] Adding %s and removing %s for client %lli\n", addon, langInfo.lastAddon, xuid);
		}
		if (g_pMultiAddonManager)
		{
			g_pMultiAddonManager->RemoveClientAddon(langInfo.lastAddon, xuid);
			g_pMultiAddonManager->AddClientAddon(addon, xuid, true);
		}
		V_strncpy(langInfo.lastAddon, addon, sizeof(langInfo.lastAddon));
	}
	V_strncpy(langInfo.language, langKey, sizeof(langInfo.language));
}

void SurfLanguageService::OnPlayerConnect(u64 steamID64)
{
	if (!steamID64 || SurfLanguageService::clientLanguageInfos[steamID64].cacheLevel > LanguageInfo::CacheLevel::CACHE_NONE)
	{
		return;
	}
	this->UpdateLanguage(steamID64, SurfOptionService::GetOptionStr("defaultLanguage", SURF_DEFAULT_LANGUAGE), LanguageInfo::CacheLevel::CACHE_NONE,
						 false);
	{
		// clang-format off
		cvarquery::Query(this->player->GetPlayerSlot(), "cl_language",
			[steamID64](CPlayerSlot nSlot, cvarquery::Status eStatus, const char *pszCvarName, const char *pszCvarValue)
			{
				if (eStatus == cvarquery::Status::ValueIntact)
				{
					const char* langKey = languagesKV->GetString(pszCvarValue, pszCvarValue);
					META_CONPRINTF("[Surf::Language] Received client convar value: %s\n", langKey);
					SurfLanguageService::UpdateLanguage(steamID64, langKey, LanguageInfo::CacheLevel::CACHE_CVAR, true);
				}
		});
		// clang-format on
	}
}

SurfLanguageService::LanguageInfo::LanguageInfo()
{
	V_strncpy(this->language, SurfOptionService::GetOptionStr("defaultLanguage", SURF_DEFAULT_LANGUAGE), sizeof(this->language));
}

SCMD(surf_language, SCFL_PREFERENCE)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	char language[32] {};
	V_snprintf(language, sizeof(language), "%s", args->Arg(1));
	V_strlower(language);
	bool shouldReconnect = !(player->checkpointService->GetCheckpointCount() || player->timerService->GetTimerRunning());
	SurfLanguageService::UpdateLanguage(player->GetSteamId64(false), language, SurfLanguageService::LanguageInfo::CacheLevel::CACHE_OVERRIDE, true);
	player->optionService->SetPreferenceStr("preferredLanguage", language);
	if (!shouldReconnect)
	{
		player->languageService->PrintChat(true, false, "Switch Language", language);
		player->languageService->PrintChat(false, false, "Language Change - Manual Menu Change Required");
	}
	return true;
}

CON_COMMAND_F(surf_reload_translations, "Reload translation configuration files", FCVAR_NONE)
{
	SurfLanguageService::LoadConfigFiles();
}
