#include "surf_style_10000vel.h"

#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"

Surf10000VelStylePlugin g_Surf10000VelStylePlugin;

CGameConfig *g_pGameConfig = NULL;
SurfUtils *g_pSurfUtils = NULL;
SurfStyleManager *g_pStyleManager = NULL;
StyleServiceFactory g_StyleFactory = [](SurfPlayer *player) -> SurfStyleService * { return new Surf10000VelStyleService(player); };
PLUGIN_EXPOSE(Surf10000VelStylePlugin, g_Surf10000VelStylePlugin);

CConVarRef<float> sv_maxvelocity("sv_maxvelocity");
const char *incompatibleStyles[] = {"5000vel", "3500vel"};

bool Surf10000VelStylePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	// Load mode
	int success;
	g_pStyleManager = (SurfStyleManager *)g_SMAPI->MetaFactory(SURF_STYLE_MANAGER_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", SURF_STYLE_MANAGER_INTERFACE);
		return false;
	}
	g_pSurfUtils = (SurfUtils *)g_SMAPI->MetaFactory(SURF_UTILS_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", SURF_UTILS_INTERFACE);
		return false;
	}
	modules::Initialize();
	if (!interfaces::Initialize(ismm, error, maxlen))
	{
		V_snprintf(error, maxlen, "Failed to initialize interfaces");
		return false;
	}

	if (nullptr == (g_pGameConfig = g_pSurfUtils->GetGameConfig()))
	{
		V_snprintf(error, maxlen, "Failed to get game config");
		return false;
	}

	if (!g_pStyleManager->RegisterStyle(g_PLID, STYLE_NAME_SHORT, STYLE_NAME, g_StyleFactory, incompatibleStyles,
										sizeof(incompatibleStyles) / sizeof(incompatibleStyles[0])))
	{
		V_snprintf(error, maxlen, "Failed to register style");
		return false;
	}
	ConVar_Register();

	return true;
}

bool Surf10000VelStylePlugin::Unload(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool Surf10000VelStylePlugin::Pause(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool Surf10000VelStylePlugin::Unpause(char *error, size_t maxlen)
{
	if (!g_pStyleManager->RegisterStyle(g_PLID, STYLE_NAME_SHORT, STYLE_NAME, g_StyleFactory))
	{
		return false;
	}
	return true;
}

void Surf10000VelStyleService::Init()
{
	g_pSurfUtils->SendConVarValue(this->player->GetPlayerSlot(), sv_maxvelocity, "10000");
}

const CVValue_t *Surf10000VelStyleService::GetTweakedConvarValue(const char *name)
{
	static_persist const CVValue_t sv_maxvelocity_desiredValue = 10000.f;
	if (!V_stricmp(name, "sv_maxvelocity"))
	{
		return &sv_maxvelocity_desiredValue;
	}
	return nullptr;
}

void Surf10000VelStyleService::Cleanup()
{
	// Send default 3500 maxvel, course entry will replace with map maxvel if exists
	g_pSurfUtils->SendConVarValue(this->player->GetPlayerSlot(), sv_maxvelocity, "3500");
}

void Surf10000VelStyleService::OnProcessMovement()
{
	sv_maxvelocity.Set(10000.f);
}
