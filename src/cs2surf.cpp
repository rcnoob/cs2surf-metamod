#include "cs2surf.h"

#include "entity2/entitysystem.h"
#include "steam/steam_gameserver.h"

#include "sdk/cgameresourceserviceserver.h"
#include "utils/utils.h"
#include "utils/hooks.h"
#include "utils/gameconfig.h"

#include "movement/movement.h"
#include "surf/surf.h"
#include "surf/db/surf_db.h"
#include "surf/hud/surf_hud.h"
#include "surf/mode/surf_mode.h"
#include "surf/spec/surf_spec.h"
#include "surf/goto/surf_goto.h"
#include "surf/style/surf_style.h"
#include "surf/quiet/surf_quiet.h"
#include "surf/tip/surf_tip.h"
#include "surf/option/surf_option.h"
#include "surf/language/surf_language.h"
#include "surf/mappingapi/surf_mappingapi.h"
#include "surf/global/surf_global.h"
#include "surf/beam/surf_beam.h"
#include "surf/beam/surf_zone_beam.h"

#include <vendor/MultiAddonManager/public/imultiaddonmanager.h>
#include <vendor/ClientCvarValue/public/iclientcvarvalue.h>

#include "tier0/memdbgon.h"
SurfPlugin g_SurfPlugin;

IMultiAddonManager *g_pMultiAddonManager;
IClientCvarValue *g_pClientCvarValue;
CSteamGameServerAPIContext g_steamAPI;

PLUGIN_EXPOSE(SurfPlugin, g_SurfPlugin);

bool SurfPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	setlocale(LC_ALL, "en_US.utf8");
	PLUGIN_SAVEVARS();

	if (!utils::Initialize(ismm, error, maxlen))
	{
		return false;
	}
	ConVar_Register();
	hooks::Initialize();
	movement::InitDetours();
	SurfCheckpointService::Init();
	SurfTimerService::Init();
	SurfSpecService::Init();
	SurfGotoService::Init();
	SurfHUDService::Init();
	SurfLanguageService::Init();
	SurfBeamService::Init();
	SurfZoneBeamService::Init();
	Surf::misc::Init();
	SurfQuietService::Init();
	if (!Surf::mode::CheckModeCvars())
	{
		return false;
	}

	ismm->AddListener(this, this);
	Surf::mapapi::Init();
	Surf::mode::InitModeManager();
	Surf::style::InitStyleManager();

	Surf::mode::DisableReplicatedModeCvars();

	SurfOptionService::InitOptions();
	SurfTipService::Init();
	if (late)
	{
		g_steamAPI.Init();
		g_pSurfPlayerManager->OnLateLoad();
		// We need to reset the map for mapping api to properly load in.
		utils::ResetMap();
	}
	return true;
}

bool SurfPlugin::Unload(char *error, size_t maxlen)
{
	this->unloading = true;
	Surf::misc::UnrestrictTimeLimit();
	hooks::Cleanup();
	Surf::mode::EnableReplicatedModeCvars();
	utils::Cleanup();
	g_pSurfModeManager->Cleanup();
	g_pSurfStyleManager->Cleanup();
	g_pPlayerManager->Cleanup();
	SurfDatabaseService::Cleanup();
	SurfGlobalService::Cleanup();
	SurfLanguageService::Cleanup();
	SurfOptionService::Cleanup();
	ConVar_Unregister();
	return true;
}

void SurfPlugin::AllPluginsLoaded()
{
	SurfDatabaseService::Init();
	Surf::mode::LoadModePlugins();
	Surf::style::LoadStylePlugins();
	g_pSurfPlayerManager->ResetPlayers();
	this->UpdateSelfMD5();
	g_pMultiAddonManager = (IMultiAddonManager *)g_SMAPI->MetaFactory(MULTIADDONMANAGER_INTERFACE, nullptr, nullptr);
	g_pClientCvarValue = (IClientCvarValue *)g_SMAPI->MetaFactory(CLIENTCVARVALUE_INTERFACE, nullptr, nullptr);
}

void SurfPlugin::AddonInit()
{
	static_persist bool addonLoaded;
	if (g_pMultiAddonManager != nullptr && !addonLoaded)
	{
		addonLoaded = g_pMultiAddonManager->AddAddon(SURF_WORKSHOP_ADDON_ID, true);
		CConVarRef<bool> mm_cache_clients_with_addons("mm_cache_clients_with_addons");
		CConVarRef<float> mm_cache_clients_duration("mm_cache_clients_duration");
		mm_cache_clients_with_addons.Set(true);
		mm_cache_clients_duration.Set(30.0f);
	}
}

bool SurfPlugin::IsAddonMounted()
{
	if (g_pMultiAddonManager != nullptr)
	{
		return g_pMultiAddonManager->IsAddonMounted(SURF_WORKSHOP_ADDON_ID, true);
	}
	return false;
}

bool SurfPlugin::Pause(char *error, size_t maxlen)
{
	return true;
}

bool SurfPlugin::Unpause(char *error, size_t maxlen)
{
	return true;
}

void *SurfPlugin::OnMetamodQuery(const char *iface, int *ret)
{
	if (strcmp(iface, SURF_MODE_MANAGER_INTERFACE) == 0)
	{
		*ret = META_IFACE_OK;
		return g_pSurfModeManager;
	}
	else if (strcmp(iface, SURF_STYLE_MANAGER_INTERFACE) == 0)
	{
		*ret = META_IFACE_OK;
		return g_pSurfStyleManager;
	}
	else if (strcmp(iface, SURF_UTILS_INTERFACE) == 0)
	{
		*ret = META_IFACE_OK;
		return g_pSurfUtils;
	}
	else if (strcmp(iface, SURF_MAPPING_INTERFACE) == 0)
	{
		*ret = META_IFACE_OK;
		return g_pMappingApi;
	}
	*ret = META_IFACE_FAILED;

	return NULL;
}

void SurfPlugin::UpdateSelfMD5()
{
	ISmmPluginManager *pluginManager = (ISmmPluginManager *)g_SMAPI->MetaFactory(MMIFACE_PLMANAGER, nullptr, nullptr);
	const char *path;
	pluginManager->Query(g_PLID, &path, nullptr, nullptr);
	g_pSurfUtils->GetFileMD5(path, this->md5, sizeof(this->md5));
}

CGameEntitySystem *GameEntitySystem()
{
	return interfaces::pGameResourceServiceServer->GetGameEntitySystem();
}
