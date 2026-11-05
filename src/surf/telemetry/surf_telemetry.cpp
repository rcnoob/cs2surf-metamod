#include "surf_telemetry.h"
#include "utils/simplecmds.h"
#include "surf/language/surf_language.h"
#include "sdk/usercmd.h"

#define AFK_THRESHOLD 30.0f
f64 SurfTelemetryService::lastActiveCheckTime = 0.0f;

void SurfTelemetryService::OnPhysicsSimulatePost()
{
	// AFK check
	if (!this->player->GetMoveServices())
	{
		return;
	}
	if (this->player->GetMoveServices()->m_nButtons().m_pButtonStates[1] != 0
		|| this->player->GetMoveServices()->m_nButtons().m_pButtonStates[2] != 0)
	{
		this->activeStats.lastActionTime = g_pSurfUtils->GetServerGlobals()->realtime;
		return;
	}
}

void SurfTelemetryService::ActiveCheck()
{
	f64 currentTime = g_pSurfUtils->GetServerGlobals()->realtime;
	f64 duration = currentTime - SurfTelemetryService::lastActiveCheckTime;
	for (u32 i = 0; i < MAX_PLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		if (!player->IsInGame() || player->IsFakeClient() || player->IsCSTV())
		{
			continue;
		}
		player->telemetryService->activeStats.timeSpentInServer += duration;
		if (player->IsAlive())
		{
			if (currentTime - player->telemetryService->activeStats.lastActionTime > AFK_THRESHOLD)
			{
				player->telemetryService->activeStats.afkDuration += duration;
			}
			else
			{
				player->telemetryService->activeStats.activeTime += duration;
			}
		}
	}
	SurfTelemetryService::lastActiveCheckTime = currentTime;
}
