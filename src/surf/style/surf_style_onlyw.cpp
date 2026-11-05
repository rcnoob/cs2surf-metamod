#include "surf_style_onlyw.h"
#include "sdk/usercmd.h"

#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"

SurfOnlyWStylePlugin g_SurfOnlyWStylePlugin;

CGameConfig *g_pGameConfig = NULL;
SurfUtils *g_pSurfUtils = NULL;
SurfStyleManager *g_pStyleManager = NULL;
StyleServiceFactory g_StyleFactory = [](SurfPlayer *player) -> SurfStyleService * { return new SurfOnlyWStyleService(player); };
PLUGIN_EXPOSE(SurfOnlyWStylePlugin, g_SurfOnlyWStylePlugin);

const char *incompatibleStyles[] = {"HSW", "SW"};

bool SurfOnlyWStylePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
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

bool SurfOnlyWStylePlugin::Unload(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool SurfOnlyWStylePlugin::Pause(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool SurfOnlyWStylePlugin::Unpause(char *error, size_t maxlen)
{
	if (!g_pStyleManager->RegisterStyle(g_PLID, STYLE_NAME_SHORT, STYLE_NAME, g_StyleFactory))
	{
		return false;
	}
	return true;
}

void SurfOnlyWStyleService::Init() {}

const CVValue_t *SurfOnlyWStyleService::GetTweakedConvarValue(const char *name)
{
	return nullptr;
}

void SurfOnlyWStyleService::Cleanup() {}

void SurfOnlyWStyleService::OnSetupMove(CUserCmd *pc)
{
	auto subtickMoves = pc->mutable_base()->mutable_subtick_moves();
	auto iterator = subtickMoves->begin();

	while (iterator != subtickMoves->end())
	{
		uint64 button = iterator->button();
		if (button == IN_MOVELEFT || button == IN_MOVERIGHT || button == IN_BACK)
		{
			iterator = subtickMoves->erase(iterator);
		}
		else
		{
			iterator++;
		}
	}

	pc->mutable_base()->set_leftmove(0.0f);
	if (pc->mutable_base()->forwardmove() < 0.0f)
	{
		pc->mutable_base()->set_forwardmove(0.0f);
	}

	// disable buttons for HUD
	this->player->DisableButton(IN_MOVELEFT);
	this->player->DisableButton(IN_MOVERIGHT);
	this->player->DisableButton(IN_BACK);
}
