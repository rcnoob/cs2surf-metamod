#include "surf_style_halfsideways.h"
#include "sdk/usercmd.h"

#include "utils/addresses.h"
#include "utils/interfaces.h"
#include "utils/gameconfig.h"

SurfHalfSidewaysStylePlugin g_SurfHalfSidewaysStylePlugin;

CGameConfig *g_pGameConfig = NULL;
SurfUtils *g_pSurfUtils = NULL;
SurfStyleManager *g_pStyleManager = NULL;
StyleServiceFactory g_StyleFactory = [](SurfPlayer *player) -> SurfStyleService * { return new SurfHalfSidewaysStyleService(player); };
PLUGIN_EXPOSE(SurfHalfSidewaysStylePlugin, g_SurfHalfSidewaysStylePlugin);

const char *incompatibleStyles[] = {"SW", "OnlyW"};

bool SurfHalfSidewaysStylePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
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

bool SurfHalfSidewaysStylePlugin::Unload(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool SurfHalfSidewaysStylePlugin::Pause(char *error, size_t maxlen)
{
	g_pStyleManager->UnregisterStyle(g_PLID);
	return true;
}

bool SurfHalfSidewaysStylePlugin::Unpause(char *error, size_t maxlen)
{
	if (!g_pStyleManager->RegisterStyle(g_PLID, STYLE_NAME_SHORT, STYLE_NAME, g_StyleFactory))
	{
		return false;
	}
	return true;
}

void SurfHalfSidewaysStyleService::Init() {}

const CVValue_t *SurfHalfSidewaysStyleService::GetTweakedConvarValue(const char *name)
{
	return nullptr;
}

void SurfHalfSidewaysStyleService::Cleanup() {}

void SurfHalfSidewaysStyleService::OnSetupMove(CUserCmd *pc)
{
	auto base = pc->mutable_base();
	auto subtickMoves = base->mutable_subtick_moves();

	// first scan all subtick moves in CUserCmd to look for non HSW inputs
	bool hasForwardBack = false;
	bool hasSide = false;

	for (const auto &move : *subtickMoves)
	{
		uint64 button = move.button();
		if (button & (IN_FORWARD | IN_BACK))
		{
			hasForwardBack = true;
		}
		if (button & (IN_MOVELEFT | IN_MOVERIGHT))
		{
			hasSide = true;
		}
	}

	// also check current move values
	if (base->forwardmove() != 0.0f)
	{
		hasForwardBack = true;
	}
	if (base->leftmove() != 0.0f)
	{
		hasSide = true;
	}

	// then remove non HSW inputs if found
	if (hasSide && !hasForwardBack)
	{
		auto iterator = subtickMoves->begin();
		while (iterator != subtickMoves->end())
		{
			if (iterator->button() & (IN_MOVELEFT | IN_MOVERIGHT))
			{
				iterator = subtickMoves->erase(iterator);
			}
			else
			{
				iterator++;
			}
		}
		base->set_leftmove(0.0f);
		this->player->DisableButton(IN_MOVELEFT);
		this->player->DisableButton(IN_MOVERIGHT);
	}
	else if (hasForwardBack && !hasSide)
	{
		auto iterator = subtickMoves->begin();
		while (iterator != subtickMoves->end())
		{
			if (iterator->button() & (IN_FORWARD | IN_BACK))
			{
				iterator = subtickMoves->erase(iterator);
			}
			else
			{
				iterator++;
			}
		}
		base->set_forwardmove(0.0f);
		this->player->DisableButton(IN_FORWARD);
		this->player->DisableButton(IN_BACK);
	}
}
