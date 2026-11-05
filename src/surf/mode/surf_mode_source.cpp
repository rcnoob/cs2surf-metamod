#include "surf_mode_source.h"

#define MODE_NAME_SHORT "CSS"
#define MODE_NAME       "Source"

SurfSourceModePlugin g_SurfSourceModePlugin;

CGameConfig *g_pGameConfig = NULL;
SurfUtils *g_pSurfUtils = NULL;
SurfModeManager *g_pModeManager = NULL;
MappingInterface *g_pMappingApi = NULL;
ModeServiceFactory g_ModeFactory = [](SurfPlayer *player) -> SurfModeService * { return new SurfSourceModeService(player); };
PLUGIN_EXPOSE(SurfSourceModePlugin, g_SurfSourceModePlugin);

bool SurfSourceModePlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	// Load mode
	int success;
	g_pModeManager = (SurfModeManager *)g_SMAPI->MetaFactory(SURF_MODE_MANAGER_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", SURF_MODE_MANAGER_INTERFACE);
		return false;
	}
	g_pSurfUtils = (SurfUtils *)g_SMAPI->MetaFactory(SURF_UTILS_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", SURF_UTILS_INTERFACE);
		return false;
	}
	g_pMappingApi = (MappingInterface *)g_SMAPI->MetaFactory(SURF_MAPPING_INTERFACE, &success, 0);
	if (success == META_IFACE_FAILED)
	{
		V_snprintf(error, maxlen, "Failed to find %s interface", SURF_MAPPING_INTERFACE);
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

	if (!g_pModeManager->RegisterMode(g_PLID, MODE_NAME_SHORT, MODE_NAME, g_ModeFactory))
	{
		V_snprintf(error, maxlen, "Failed to register mode");
		return false;
	}

	ConVar_Register();
	return true;
}

bool SurfSourceModePlugin::Unload(char *error, size_t maxlen)
{
	g_pModeManager->UnregisterMode(g_PLID);
	return true;
}

bool SurfSourceModePlugin::Pause(char *error, size_t maxlen)
{
	g_pModeManager->UnregisterMode(g_PLID);
	return true;
}

bool SurfSourceModePlugin::Unpause(char *error, size_t maxlen)
{
	if (!g_pModeManager->RegisterMode(g_PLID, MODE_NAME_SHORT, MODE_NAME, g_ModeFactory))
	{
		return false;
	}
	return true;
}

void SurfSourceModeService::Reset()
{
	this->hasValidDesiredViewAngle = {};
	this->lastValidDesiredViewAngle = vec3_angle;
	this->oldDuckPressed = {};
	this->forcedUnduck = {};
	this->postProcessMovementZSpeed = {};

	this->angleHistory.RemoveAll();
	this->leftPreRatio = {};
	this->rightPreRatio = {};
	this->bonusSpeed = {};
	this->maxPre = {};

	this->inTryPlayerMove = {};
	this->lastPlane = vec3_origin;
	this->stuckTraceCount = {};

	this->airMoving = {};
	this->tpmTriggerFixOrigins.RemoveAll();
}

void SurfSourceModeService::Cleanup()
{
	auto pawn = this->player->GetPlayerPawn();
	if (pawn)
	{
		pawn->m_flVelocityModifier(1.0f);
	}
}

const char *SurfSourceModeService::GetModeName()
{
	return MODE_NAME;
}

const char *SurfSourceModeService::GetModeShortName()
{
	return MODE_NAME_SHORT;
}

const CVValue_t *SurfSourceModeService::GetModeConVarValues()
{
	return modeCvarValues;
}

void SurfSourceModeService::OnSetupMove(CUserCmd *pc)
{
	u32 tickCount = g_pSurfUtils->GetServerGlobals()->tickcount;

	// 2 ticks with 128t timing, 62 ticks with 64t timing
	bool doubleTickrate = ((tickCount * 2) % 64) < 2;

	for (i32 j = 0; j < pc->mutable_base()->subtick_moves_size(); j++)
	{
		CSubtickMoveStep *subtickMove = pc->mutable_base()->mutable_subtick_moves(j);
		if (subtickMove->button() == IN_ATTACK || subtickMove->button() == IN_ATTACK2 || subtickMove->button() == IN_RELOAD)
		{
			continue;
		}
		float when = subtickMove->when();

		if (doubleTickrate)
		{
			subtickMove->set_when(when >= 0.5 ? 0.5f : 0.0f);
		}
		else
		{
			subtickMove->set_when(0.0f);
		}
	}
}

void SurfSourceModeService::OnPhysicsSimulate()
{
	CCSPlayer_MovementServices *moveServices = this->player->GetMoveServices();
	if (!moveServices)
	{
		return;
	}
	u32 tickCount = g_pSurfUtils->GetServerGlobals()->tickcount;
	bool doubleTickrate = ((tickCount * 2) % 64) < 2;
	if (!doubleTickrate)
	{
		return;
	}
	f32 subtickMoveTime = (tickCount - 0.5) * ENGINE_FIXED_TICK_INTERVAL;
	for (u32 i = 0; i < 4; i++)
	{
		if (fabs(subtickMoveTime - moveServices->m_arrForceSubtickMoveWhen[i]) < 0.001)
		{
			return;
		}
		if (subtickMoveTime > moveServices->m_arrForceSubtickMoveWhen[i])
		{
			moveServices->SetForcedSubtickMove(i, subtickMoveTime, false);
			return;
		}
	}
}

void SurfSourceModeService::OnPhysicsSimulatePost()
{
	CCSPlayer_MovementServices *moveServices = this->player->GetMoveServices();
	if (!moveServices)
	{
		return;
	}
	u32 tickCount = g_pSurfUtils->GetServerGlobals()->tickcount;
	bool doubleTickrate = ((tickCount * 2) % 64) < 2;
	if (!doubleTickrate)
	{
		return;
	}
	f32 subtickMoveTime = (tickCount + 0.5) * ENGINE_FIXED_TICK_INTERVAL;
	for (u32 i = 0; i < 4; i++)
	{
		if (fabs(subtickMoveTime - moveServices->m_arrForceSubtickMoveWhen[i]) < 0.001)
		{
			subtickMoveTime += ENGINE_FIXED_TICK_INTERVAL;
			continue;
		}
		if (subtickMoveTime > moveServices->m_arrForceSubtickMoveWhen[i])
		{
			moveServices->SetForcedSubtickMove(i, subtickMoveTime);
			subtickMoveTime += ENGINE_FIXED_TICK_INTERVAL;
		}
	}
}
