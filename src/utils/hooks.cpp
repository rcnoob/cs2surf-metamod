#include "utils/utils.h"
#include "utils/hooks.h"
#include "utils/addresses.h"
#include "utils/simplecmds.h"
#include "utils/gamesystem.h"
#include "utils/gameconfig.h"
#include "utils/ctimer.h"

#include "entityclass.h"
#include "gamesystems/spawngroup_manager.h"
#include "steam/steam_gameserver.h"
#include "bufferstring.h"
#include "igameeventsystem.h"
#include "igamesystem.h"
#include "entityclass.h"
#include "gamesystems/spawngroup_manager.h"
#include "utils/simplecmds.h"
#include "utils/gamesystem.h"
#include "utils/async_file_io.h"
#include "steam/steam_gameserver.h"

#include "sdk/steamnetworkingsockets.h"
#include "surf/surf.h"
#include "surf/beam/surf_beam.h"
#include "surf/option/surf_option.h"
// #include "surf/option/pref_registry.h"
#include "surf/quiet/surf_quiet.h"
#include "surf/timer/surf_timer.h"
#include "surf/timer/submission.h"
#include "surf/timer/queries/base_request.h"
#include "surf/telemetry/surf_telemetry.h"
#include "surf/trigger/surf_trigger.h"
#include "surf/db/surf_db.h"
#include "surf/mappingapi/surf_mappingapi.h"
#include "surf/global/surf_global.h"
#include "surf/profile/surf_profile.h"
#include "surf/recording/surf_recording.h"
#include "surf/replays/surf_replaysystem.h"
#include "utils/utils.h"
#include "utils/cvarquery.h"
#include "sdk/entity/cbasetrigger.h"
#include "cstrike15_usermessages.pb.h"
#include "sdk/usercmd.h"

#include "vprof.h"
#ifdef DEBUG_TPM
#include "fmtstr.h"
#endif

#include "memdbgon.h"

extern CSteamGameServerAPIContext g_steamAPI;
extern CGameConfig *g_pGameConfig;

class GameSessionConfiguration_t
{
};

class EntListener : public IEntityListener
{
	virtual void OnEntityCreated(CEntityInstance *pEntity);
	virtual void OnEntitySpawned(CEntityInstance *pEntity);
	virtual void OnEntityDeleted(CEntityInstance *pEntity);
} entityListener;

static_global bool ignoreTouchEvent {};

static MovementPlayerManager *playerManager;

#ifdef DEBUG_TPM
static bool g_traceShapeEnabled = false;
CUtlVector<TraceHistory> traceHistory;
#endif

// Entity hooks
static KHook::Return<void> StartTouchPre(CBaseEntity *pThis, CBaseEntity *pOther)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (SurfTriggerService::IsManagedByTriggerService(pThis, pOther) && !g_SurfPlugin.simulatingPhysics)
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Return<void> StartTouchPost(CBaseEntity *pThis, CBaseEntity *pOther)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (SurfTriggerService::IsManagedByTriggerService(pThis, pOther) && !g_SurfPlugin.simulatingPhysics)
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<CBaseEntity, void, CBaseEntity *> startTouchHook(StartTouchPre, StartTouchPost);

static KHook::Return<void> TouchPre(CBaseEntity *pThis, CBaseEntity *pOther)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	SurfPlayer *player = nullptr;
	if (SURF_STREQI(pThis->GetClassname(), "trigger_push") && SURF_STREQI(pOther->GetClassname(), "player"))
	{
		player = g_pSurfPlayerManager->ToPlayer(static_cast<CCSPlayerPawn *>(pOther));
	}
	else if (SURF_STREQI(pThis->GetClassname(), "player") && SURF_STREQI(pOther->GetClassname(), "trigger_push"))
	{
		player = g_pSurfPlayerManager->ToPlayer(static_cast<CCSPlayerPawn *>(pThis));
	}
	if (SurfTriggerService::IsManagedByTriggerService(pThis, pOther) && !g_SurfPlugin.simulatingPhysics)
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Return<void> TouchPost(CBaseEntity *pThis, CBaseEntity *pOther)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (SurfTriggerService::IsManagedByTriggerService(pThis, pOther) && !g_SurfPlugin.simulatingPhysics)
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<CBaseEntity, void, CBaseEntity *> touchHook(TouchPre, TouchPost);

static KHook::Return<void> EndTouchPre(CBaseEntity *pThis, CBaseEntity *pOther)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (SurfTriggerService::IsManagedByTriggerService(pThis, pOther) && !g_SurfPlugin.simulatingPhysics)
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Return<void> EndTouchPost(CBaseEntity *pThis, CBaseEntity *pOther)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (SurfTriggerService::IsManagedByTriggerService(pThis, pOther) && !g_SurfPlugin.simulatingPhysics)
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<CBaseEntity, void, CBaseEntity *> endTouchHook(EndTouchPre, EndTouchPost);

void hooks::CallOriginalStartTouch(CBaseEntity *pThis, CBaseEntity *pOther)
{
	startTouchHook.CallOriginal(pThis, pOther);
}

void hooks::CallOriginalTouch(CBaseEntity *pThis, CBaseEntity *pOther)
{
	touchHook.CallOriginal(pThis, pOther);
}

void hooks::CallOriginalEndTouch(CBaseEntity *pThis, CBaseEntity *pOther)
{
	endTouchHook.CallOriginal(pThis, pOther);
}

static KHook::Return<void> TeleportPre(CBaseEntity *pThis, const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (pThis->IsPawn())
	{
		MovementPlayer *player = g_pSurfPlayerManager->ToPlayer(static_cast<CBasePlayerPawn *>(pThis));
		if (player)
		{
			player->OnTeleport(newPosition, newAngles, newVelocity);
		}
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<CBaseEntity, void, const Vector *, const QAngle *, const Vector *> teleportHook(TeleportPre, nullptr);

static KHook::Return<void> ChangeTeamPost(CCSPlayerController *pThis, i32 team)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = g_pSurfPlayerManager->ToPlayer(pThis);
	if (player)
	{
		player->OnChangeTeamPost(team);
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<CCSPlayerController, void, i32> changeTeamHook(nullptr, ChangeTeamPost);

// ISource2GameEntities hooks

static KHook::Return<void> CheckTransmitPost(ISource2GameEntities *pThis, CCheckTransmitInfo **pInfos, int infoCount, CBitVec<16384> &unk1,
											 CBitVec<16384> &unk2, const Entity2Networkable_t **pNetworkables, const uint16 *pEntityIndicies,
											 int nEntities)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	Surf::quiet::OnCheckTransmit(pInfos, infoCount);
	SurfProfileService::OnCheckTransmit();
	// SurfHUDService::OnCheckTransmit(pInfos, infoCount);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameEntities, void, CCheckTransmitInfo **, int, CBitVec<16384> &, CBitVec<16384> &, const Entity2Networkable_t **,
					  const uint16 *, int>
	checkTransmitHook(nullptr, CheckTransmitPost);

// ISource2Server hooks

static KHook::Return<void> GameFramePre(ISource2Server *pThis, bool simulating, bool bFirstTick, bool bLastTick)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_SurfPlugin.serverGlobals = *(g_pSurfUtils->GetGlobals());
	RunSubmission::CheckAll();
	BaseRequest::CheckRequests();
	SurfTelemetryService::ActiveCheck();
	SurfBeamService::UpdateBeams();
	SurfProfileService::OnGameFrame();
	Surf::replaysystem::OnGameFrame();
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2Server, void, bool, bool, bool> gameFrameHook(GameFramePre, nullptr);

static KHook::Return<void> GameServerSteamAPIActivatedPre(ISource2Server *pThis)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_steamAPI.Init();
	g_pSurfPlayerManager->OnSteamAPIActivated();
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2Server, void> gameServerSteamAPIActivatedHook(GameServerSteamAPIActivatedPre, nullptr);

static KHook::Return<void> GameServerSteamAPIDeactivatedPre(ISource2Server *pThis)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2Server, void> gameServerSteamAPIDeactivatedHook(GameServerSteamAPIDeactivatedPre, nullptr);

// ISource2GameClients
static KHook::Return<bool> ClientConnectPre(ISource2GameClients *pThis, CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID,
											bool unk1, CBufferString *pRejectReason)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_pSurfPlayerManager->OnClientConnect(slot, pszName, xuid, pszNetworkID, unk1, pRejectReason);
	return {KHook::Action::Ignore, true};
}

static KHook::Virtual<ISource2GameClients, bool, CPlayerSlot, const char *, uint64, const char *, bool, CBufferString *>
	clientConnectHook(ClientConnectPre, nullptr);

static KHook::Return<void> OnClientConnectedPre(ISource2GameClients *pThis, CPlayerSlot slot, const char *pszName, uint64 xuid,
												const char *pszNetworkID, const char *pszAddress, bool bFakePlayer)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_pSurfPlayerManager->OnClientConnected(slot, pszName, xuid, pszNetworkID, pszAddress, bFakePlayer);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot, const char *, uint64, const char *, const char *, bool>
	onClientConnectedHook(OnClientConnectedPre, nullptr);

static KHook::Return<void> ClientFullyConnectPre(ISource2GameClients *pThis, CPlayerSlot slot)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_pSurfPlayerManager->OnClientFullyConnect(slot);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot> clientFullyConnectHook(ClientFullyConnectPre, nullptr);

static KHook::Return<void> ClientPutInServerPre(ISource2GameClients *pThis, CPlayerSlot slot, char const *pszName, int type, uint64 xuid)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_pSurfPlayerManager->OnClientPutInServer(slot, pszName, type, xuid);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot, char const *, int, uint64> clientPutInServerHook(ClientPutInServerPre, nullptr);

static KHook::Return<void> ClientActivePost(ISource2GameClients *pThis, CPlayerSlot slot, bool bLoadGame, const char *pszName, uint64 xuid)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_pSurfPlayerManager->OnClientActive(slot, bLoadGame, pszName, xuid);
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(slot);
	if (player->GetPlayerPawn())
	{
		hooks::AddEntityHooks(player->GetPlayerPawn());
	}
	else
	{
		Warning("[Surf] WARNING: Player pawn for slot %i not found!\n", slot.Get());
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot, bool, const char *, uint64> clientActiveHook(nullptr, ClientActivePost);

static KHook::Return<void> ClientDisconnectPost(ISource2GameClients *pThis, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName,
												uint64 xuid, const char *pszNetworkID)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(slot);
	if (player->GetController())
	{
		player->GetController()->m_LastTimePlayerWasDisconnectedForPawnsRemove().SetTime(0.01f);
		player->GetController()->SwitchTeam(0);
	}
	if (player->GetPlayerPawn())
	{
		hooks::RemoveEntityHooks(player->GetPlayerPawn());
	}
	else
	{
		Warning("WARNING: Player pawn for slot %i not found!\n", slot.Get());
	}
	player->timerService->OnClientDisconnect();
	player->recordingService->OnClientDisconnect();
	player->optionService->OnClientDisconnect();
	player->globalService->OnClientDisconnect();
	cvarquery::OnClientDisconnect(slot);
	// Surf::prefs::OnClientDisconnect(slot);
	g_pSurfPlayerManager->OnClientDisconnect(slot, reason, pszName, xuid, pszNetworkID);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64, const char *>
	clientDisconnectHook(nullptr, ClientDisconnectPost);

static KHook::Return<void> ClientVoicePre(ISource2GameClients *pThis, CPlayerSlot slot)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_pSurfPlayerManager->OnClientVoice(slot);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot> clientVoiceHook(ClientVoicePre, nullptr);

static KHook::Return<void> ClientCommandPre(ISource2GameClients *pThis, CPlayerSlot slot, const CCommand &args)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (Surf::misc::CheckBlockedRadioCommands(args[0]))
	{
		return {KHook::Action::Supersede};
	}
	if (scmd::OnClientCommand(slot, args))
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot, const CCommand &> clientCommandHook(ClientCommandPre, nullptr);

/*
static KHook::Return<void> ClientSvcUserMessagePre(ISource2GameClients *pThis, CPlayerSlot slot, int type, uint32 size, const void *buf)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (type != CS_UM_CustomHudClicked)
	{
		return {KHook::Action::Ignore};
	}

	CCSUsrMsg_CustomHudClicked msg;

	if (!msg.ParseFromArray(buf, size))
	{
		return {KHook::Action::Ignore};
	}

	if (CCSCustomHudLayout *layout = CCSCustomHudLayout::FromClickHandle(msg.custom_hud_layout()))
	{
		SurfMenuService::OnCustomHudClicked(slot, layout, msg.button_id().c_str());
	}

	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot, int, uint32, const void *> clientSvcUserMessageHook(ClientSvcUserMessagePre, nullptr);
*/

// INetworkServerService
static KHook::Return<void> StartupServerPost(INetworkServerService *pThis, const GameSessionConfiguration_t &config, ISource2WorldSession *,
											 const char *)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_SurfPlugin.AddonInit();
	Surf::course::ClearCourses();
	Surf::mapapi::Init();
	Surf::replaysystem::Init();
	return {KHook::Action::Ignore};
}

static KHook::Virtual<INetworkServerService, void, const GameSessionConfiguration_t &, ISource2WorldSession *, const char *>
	startupServerHook(nullptr, StartupServerPost);

// IGameEventManager2
static KHook::Return<bool> FireEventPre(IGameEventManager2 *pThis, IGameEvent *event, bool bDontBroadcast)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (event)
	{
		if (SURF_STREQI(event->GetName(), "player_death"))
		{
			CEntityInstance *instance = event->GetPlayerPawn("userid");
			SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(instance->GetEntityIndex());
			if (player)
			{
				player->timerService->OnPlayerDeath();
				player->quietService->SendFullUpdate();
			}
		}
		else if (SURF_STREQI(event->GetName(), "round_prestart"))
		{
			hooks::HookEntities();
			Surf::mapapi::OnRoundPreStart();
		}
		else if (SURF_STREQI(event->GetName(), "round_start"))
		{
			interfaces::pEngine->ServerCommand("sv_full_alltalk 1");
			SurfTimerService::OnRoundStart();
			Surf::misc::OnRoundStart();
			Surf::mapapi::OnRoundStart();
			Surf::replaysystem::OnRoundStart();
		}
		else if (SURF_STREQI(event->GetName(), "player_team"))
		{
			event->SetBool("silent", true);
		}
		else if (SURF_STREQI(event->GetName(), "player_spawn"))
		{
			CEntityInstance *instance = event->GetPlayerPawn("userid");
			if (instance)
			{
				SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(instance->GetEntityIndex());
				if (player)
				{
					player->timerService->OnPlayerSpawn();
					StartTimer<SurfPlayer *>(SurfQuietService::UpdatePistol, player, 0.05, false);
				}
			}
		}
	}
	return {KHook::Action::Ignore, true};
}

static KHook::Virtual<IGameEventManager2, bool, IGameEvent *, bool> fireEventHook(FireEventPre, nullptr);

// ICvar
static KHook::Return<void> DispatchConCommandPre(ICvar *pThis, ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (Surf::misc::CheckBlockedRadioCommands(args[0]))
	{
		return {KHook::Action::Supersede};
	}
	if (SurfOptionService::GetOptionInt("overridePlayerChat", true))
	{
		Surf::misc::ProcessConCommand(cmd, ctx, args);
	}

	if (scmd::OnDispatchConCommand(cmd, ctx, args))
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ICvar, void, ConCommandRef, const CCommandContext &, const CCommand &> dispatchConCommandHook(DispatchConCommandPre, nullptr);

// IGameEventSystem
static KHook::Return<void> PostEventPre(IGameEventSystem *pThis, CSplitScreenSlot nSlot, bool bLocalOnly, int nClientCount, const uint64 *clients,
										INetworkMessageInternal *pEvent, const CNetMessage *pData, unsigned long nSize, NetChannelBufType_t bufType)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	Surf::quiet::OnPostEvent(pEvent, pData, clients);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<IGameEventSystem, void, CSplitScreenSlot, bool, int, const uint64 *, INetworkMessageInternal *, const CNetMessage *,
					  unsigned long, NetChannelBufType_t>
	postEventHook(PostEventPre, nullptr);

// CEntitySystem
static KHook::Return<void> SpawnPre(CEntitySystem *pThis, int nCount, const EntitySpawnInfo_t *pInfo)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	Surf::mapapi::OnSpawn(nCount, pInfo);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<CEntitySystem, void, int, const EntitySpawnInfo_t *> entitySystemSpawnHook(SpawnPre, nullptr);

// CSpawnGroupMgrGameSystem hooks
static KHook::Return<ILoadingSpawnGroup *> CreateLoadingSpawnGroupPre(CSpawnGroupMgrGameSystem *pThis, SpawnGroupHandle_t hSpawnGroup,
																	  bool bSynchronouslySpawnEntities, bool bConfirmResourcesLoaded,
																	  const CUtlVector<const CEntityKeyValues *> *pKeyValues)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	Surf::mapapi::OnCreateLoadingSpawnGroupHook(pKeyValues);
	return {KHook::Action::Ignore, nullptr};
}

static KHook::Virtual<CSpawnGroupMgrGameSystem, ILoadingSpawnGroup *, SpawnGroupHandle_t, bool, bool, const CUtlVector<const CEntityKeyValues *> *>
	createLoadingSpawnGroupHook(CreateLoadingSpawnGroupPre, nullptr);

// INetworkGameServer
static KHook::Return<bool> ActivateServerPost(CNetworkGameServerBase *pThis)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (!interfaces::pEngine->IsDedicatedServer())
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(CPlayerSlot(0));
		player->Reset();
	}
	u64 id = g_pSurfUtils->GetCurrentMapWorkshopID();
	u64 size = g_pSurfUtils->GetCurrentMapSize();

	META_CONPRINTF("[Surf] Loading map %s, workshop ID %llu, size %llu\n", g_pSurfUtils->GetCurrentMapVPK().Get(), id, size);

	RunSubmission::Clear();
	Surf::misc::OnActivateServer();
	SurfDatabaseService::SetupMap();
	SurfGlobalService::OnActivateServer();
	SurfRecordingService::OnActivateServer();

	char md5[33];
	g_pSurfUtils->GetCurrentMapMD5(md5, sizeof(md5));
	META_CONPRINTF("[Surf] Map file md5: %s\n", md5);

	return {KHook::Action::Ignore, true};
}

static KHook::Virtual<CNetworkGameServerBase, bool> activateServerHook(nullptr, ActivateServerPost);

// CNetworkGameServerBase

static KHook::Return<CServerSideClientBase *> ConnectClientPre(CNetworkGameServerBase *pThis, const char *pszName, ns_address *pAddr,
															   uint32 steam_handle, C2S_CONNECT_Message *pConnectMsg, const char *pszChallenge,
															   const byte *pAuthTicket, int nAuthTicketLength, bool bIsLowViolence)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_pSurfPlayerManager->OnConnectClient(pszName, pAddr, steam_handle, pConnectMsg, pszChallenge, pAuthTicket, nAuthTicketLength, bIsLowViolence);
	return {KHook::Action::Ignore, nullptr};
}

static KHook::Return<CServerSideClientBase *> ConnectClientPost(CNetworkGameServerBase *pThis, const char *pszName, ns_address *pAddr,
																uint32 steam_handle, C2S_CONNECT_Message *pConnectMsg, const char *pszChallenge,
																const byte *pAuthTicket, int nAuthTicketLength, bool bIsLowViolence)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	g_pSurfPlayerManager->OnConnectClientPost(pszName, pAddr, steam_handle, pConnectMsg, pszChallenge, pAuthTicket, nAuthTicketLength,
											  bIsLowViolence);
	return {KHook::Action::Ignore, nullptr};
}

static KHook::Virtual<CNetworkGameServerBase, CServerSideClientBase *, const char *, ns_address *, uint32, C2S_CONNECT_Message *, const char *,
					  const byte *, int, bool>
	connectClientHook(ConnectClientPre, ConnectClientPost);

// ============================================================
// CServerSideClient hooks (cvar query)
// ============================================================

static KHook::Return<bool> ProcessRespondCvarValuePre(CServerSideClientBase *pThis, const CNetMessagePB<CCLCMsg_RespondCvarValue> &msg)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (pThis)
	{
		cvarquery::OnCvarValueResponse(pThis->GetPlayerSlot(), msg.cookie(), (cvarquery::Status)msg.status_code(), msg.name().c_str(),
									   msg.value().c_str());
	}
	return {KHook::Action::Ignore, true};
}

static KHook::Virtual<CServerSideClientBase, bool, const CNetMessagePB<CCLCMsg_RespondCvarValue> &> respondCvarValueHook(ProcessRespondCvarValuePre,
																														 nullptr);

// Every convar a client reports: the full userinfo set on connect, and each setinfo afterwards.
static KHook::Return<bool> ProcessSetConVarPre(CServerSideClientBase *pThis, const CNetMessagePB<CNETMsg_SetConVar> &msg)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (pThis)
	{
		const CMsg_CVars &list = msg.convars();
		for (i32 i = 0; i < list.cvars_size(); i++)
		{
			const CMsg_CVars_CVar &cvar = list.cvars(i);
			cvarquery::OnClientConVar(pThis->GetPlayerSlot(), cvar.name().c_str(), cvar.value().c_str());
		}
	}
	return {KHook::Action::Ignore, true};
}

static KHook::Virtual<CServerSideClientBase, bool, const CNetMessagePB<CNETMsg_SetConVar> &> setConVarHook(ProcessSetConVarPre, nullptr);

// IGameSystem
static KHook::Return<void> ServerGamePostSimulatePost(IGameSystem *pThis, const EventServerGamePostSimulate_t *)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	ProcessTimers();
	SurfRecordingService::ProcessFileWriteCompletion();
	if (g_asyncFileIO)
	{
		g_asyncFileIO->RunFrame();
	}
	SurfGlobalService::OnServerGamePostSimulate();
	return {KHook::Action::Ignore};
}

static KHook::Virtual<IGameSystem, void, const EventServerGamePostSimulate_t *> serverGamePostSimulateHook(nullptr, ServerGamePostSimulatePost);

static KHook::Return<void> BuildGameSessionManifestPost(IGameSystem *pThis, const EventBuildGameSessionManifest_t *msg)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	Warning("[CS2Surf] IGameSystem::BuildGameSessionManifest\n");
	IEntityResourceManifest *pResourceManifest = msg->m_pResourceManifest;
	if (g_SurfPlugin.IsAddonMounted())
	{
		Warning("[CS2Surf] Precache surf soundevents \n");
		pResourceManifest->AddResource(SURF_WORKSHOP_ADDON_SNDEVENT_FILE);
	}
	pResourceManifest->AddResource("particles/ui/hud/ui_map_def_utility_trail.vpcf");
	pResourceManifest->AddResource("particles/ui/annotation/ui_annotation_line_segment.vpcf");
	return {KHook::Action::Ignore};
}

static KHook::Virtual<IGameSystem, void, const EventBuildGameSessionManifest_t *> buildGameSessionManifestHook(nullptr, BuildGameSessionManifestPost);

// CCSPlayer_MovementServices virtual hooks
static KHook::Return<void> PlayerRunCommandPre(CCSPlayer_MovementServices *pThis, PlayerCommand *pCmd)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(pThis);
	Surf::replaysystem::OnPlayerRunCommandPre(player, pCmd);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<CCSPlayer_MovementServices, void, PlayerCommand *> playerRunCommandHook(PlayerRunCommandPre, nullptr);

static KHook::Return<void> FinishMovePre(CCSPlayer_MovementServices *pThis, PlayerCommand *pCmd, CMoveData *pMoveData)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(pThis);
	Surf::replaysystem::OnFinishMovePre(player, pMoveData);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<CCSPlayer_MovementServices, void, PlayerCommand *, CMoveData *> finishMoveHook(FinishMovePre, nullptr);

// Signature-based hooks
static KHook::Return<int> RecvServerBrowserPacketPost(RecvPktInfo_t &info, void *pSock)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	return {KHook::Action::Ignore};
}

static KHook::Function<int, RecvPktInfo_t &, void *> RecvServerBrowserPacket(nullptr, RecvServerBrowserPacketPost);

#ifdef DEBUG_TPM
static KHook::Return<bool> TraceShapePost(const void *physicsQuery, const Ray_t &ray, const Vector &start, const Vector &end,
										  const CTraceFilter *pTraceFilter, trace_t *pm)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (!g_traceShapeEnabled)
	{
		return {KHook::Action::Ignore};
	}
	bool ret = *(bool *)KHook::GetOriginalValuePtr();
	f32 error;
	Vector velocity;
	for (u32 i = 0; i < 2; i++)
	{
		if (g_pSurfPlayerManager->ToPlayer(i) && g_pSurfPlayerManager->ToPlayer(i)->GetMoveServices())
		{
			error = g_pSurfPlayerManager->ToPlayer(i)->GetMoveServices()->m_flAccumulatedJumpError();
			velocity = g_pSurfPlayerManager->ToPlayer(i)->currentMoveData->m_vecVelocity;
			break;
		}
	}
	traceHistory.AddToTail({start, end, ray, pm->DidHit(), pm->m_vStartPos, pm->m_vEndPos, pm->m_vHitNormal, pm->m_vHitPoint, pm->m_flHitOffset,
							pm->m_flFraction, error, velocity});
	return {KHook::Action::Ignore};
}
#endif

static KHook::Function<bool, const void *, const Ray_t &, const Vector &, const Vector &, const CTraceFilter *, trace_t *> TraceShape(
#ifdef DEBUG_TPM
	nullptr, TraceShapePost
#else
	nullptr, nullptr
#endif
);

static KHook::Return<void> CPhysicsGameSystemFrameBoundaryPost(void *pThis)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	Surf::misc::OnPhysicsGameSystemFrameBoundary(pThis);
	return {KHook::Action::Ignore};
}

static KHook::Function<void, void *> CPhysicsGameSystemFrameBoundary(nullptr, CPhysicsGameSystemFrameBoundaryPost);

static KHook::Return<void> PhysicsSimulatePre(CCSPlayerController *controller)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (controller->m_bIsHLTV)
	{
		return {KHook::Action::Supersede};
	}
	g_SurfPlugin.simulatingPhysics = true;
	playerManager->ToPlayer(controller)->OnPhysicsSimulate();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> PhysicsSimulatePost(CCSPlayerController *controller)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	if (controller->m_bIsHLTV)
	{
		return {KHook::Action::Ignore};
	}
	MovementPlayer *player = playerManager->ToPlayer(controller);
	player->OnPhysicsSimulatePost();
	g_SurfPlugin.simulatingPhysics = false;
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayerController, void> PhysicsSimulate(PhysicsSimulatePre, PhysicsSimulatePost);

static KHook::Return<i32> ProcessUsercmdsPre(CCSPlayerController *controller, PlayerCommand *cmds, int numcmds, bool paused, float margin);

static KHook::Member<CCSPlayerController, i32, PlayerCommand *, int, bool, float> ProcessUsercmds(ProcessUsercmdsPre, nullptr);

static KHook::Return<i32> ProcessUsercmdsPre(CCSPlayerController *controller, PlayerCommand *cmds, int numcmds, bool paused, float margin)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(controller);
	player->OnProcessUsercmds(cmds, numcmds);
	auto retValue = ProcessUsercmds.CallOriginal(controller, cmds, numcmds, paused, margin);
	player->OnProcessUsercmdsPost(cmds, numcmds);
	return {KHook::Action::Supersede, retValue};
}

static KHook::Return<void> SetupMovePre(CCSPlayer_MovementServices *ms, PlayerCommand *pc, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->currentMoveData = mv;
	player->moveDataPre = CMoveData(*mv);
	player->OnSetupMove(pc);
	return {KHook::Action::Ignore};
}

static KHook::Return<void> SetupMovePost(CCSPlayer_MovementServices *ms, PlayerCommand *pc, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnSetupMovePost(pc);
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, PlayerCommand *, CMoveData *> SetupMove(SetupMovePre, SetupMovePost);

static KHook::Return<void> ProcessMovementPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->currentMoveData = mv;
	player->moveDataPre = CMoveData(*mv);
	player->OnProcessMovement();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> ProcessMovementPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->moveDataPost = CMoveData(*mv);
	player->OnProcessMovementPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> ProcessMovement(ProcessMovementPre, ProcessMovementPost);

static KHook::Return<bool> PlayerMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnPlayerMove();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> PlayerMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnPlayerMovePost();
	return {KHook::Action::Ignore, false};
}

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> PlayerMove(PlayerMovePre, PlayerMovePost);

static KHook::Return<void> CheckParametersPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnCheckParameters();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> CheckParametersPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnCheckParametersPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> CheckParameters(CheckParametersPre, CheckParametersPost);

static KHook::Return<bool> CanMovePre(CCSPlayerPawnBase *pawn)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(pawn)->OnCanMove();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> CanMovePost(CCSPlayerPawnBase *pawn)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(pawn)->OnCanMovePost();
	return {KHook::Action::Ignore, false};
}

static KHook::Member<CCSPlayerPawnBase, bool> CanMove(CanMovePre, CanMovePost);

static KHook::Return<void> FullWalkMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv, bool ground)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnFullWalkMove(ground);
	return {KHook::Action::Ignore};
}

static KHook::Return<void> FullWalkMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv, bool ground)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnFullWalkMovePost(ground);
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, bool> FullWalkMove(FullWalkMovePre, FullWalkMovePost);

static KHook::Return<bool> MoveInitPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnMoveInit();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> MoveInitPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnMoveInitPost();
	return {KHook::Action::Ignore, false};
}

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> MoveInit(MoveInitPre, MoveInitPost);

static KHook::Return<bool> CheckWaterPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnCheckWater();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> CheckWaterPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnCheckWaterPost();
	return {KHook::Action::Ignore, false};
}

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> CheckWater(CheckWaterPre, CheckWaterPost);

static KHook::Return<void> WaterMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnWaterMove();
#ifdef WATER_FIX
	if (player->enableWaterFix)
	{
		player->ignoreNextCategorizePosition = true;
	}
#endif
	return {KHook::Action::Ignore};
}

static KHook::Return<void> WaterMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnWaterMovePost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> WaterMove(WaterMovePre, WaterMovePost);

static KHook::Return<void> CheckVelocityPre(CCSPlayer_MovementServices *ms, CMoveData *mv, const char *a3)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnCheckVelocity(a3);
	return {KHook::Action::Ignore};
}

static KHook::Return<void> CheckVelocityPost(CCSPlayer_MovementServices *ms, CMoveData *mv, const char *a3)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnCheckVelocityPost(a3);
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, const char *> CheckVelocity(CheckVelocityPre, CheckVelocityPost);

static KHook::Return<void> DuckPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnDuck();
	player->processingDuck = true;
	return {KHook::Action::Ignore};
}

static KHook::Return<void> DuckPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->processingDuck = false;
	player->OnDuckPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> Duck(DuckPre, DuckPost);

static KHook::Return<bool> CanUnduckPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnCanUnduck();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> CanUnduckPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	bool canUnduck = *(bool *)KHook::GetOriginalValuePtr();
	player->OnCanUnduckPost(canUnduck);
	return {KHook::Action::Ignore, canUnduck};
}

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> CanUnduck(CanUnduckPre, CanUnduckPost);

static KHook::Return<bool> LadderMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv);

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> LadderMove(LadderMovePre, nullptr);

static KHook::Return<bool> LadderMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnLadderMove();
	Vector oldVelocity = mv->m_vecVelocity;
	MoveType_t oldMoveType = player->GetPlayerPawn()->m_MoveType();
	bool result = LadderMove.CallOriginal(ms, mv);
	if (player->GetPlayerPawn()->m_lifeState() != LIFE_DEAD && !result && oldMoveType == MOVETYPE_LADDER)
	{
		player->SetMoveType(MOVETYPE_WALK, false);
	}
	if (!result && oldMoveType == MOVETYPE_LADDER)
	{
		player->RegisterTakeoff(false, true, &player->lastValidLadderOrigin);
		player->OnChangeMoveType(MOVETYPE_LADDER);
	}
	else if (result && oldMoveType != MOVETYPE_LADDER && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_LADDER
			 && !(player->GetPlayerPawn()->m_fFlags & FL_ONGROUND))
	{
		player->RegisterLanding(oldVelocity, false);
		player->OnChangeMoveType(MOVETYPE_WALK);
	}
	else if (result && oldMoveType == MOVETYPE_LADDER && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_WALK)
	{
		player->RegisterTakeoff(player->IsButtonPressed(IN_JUMP), true);
		player->OnChangeMoveType(MOVETYPE_LADDER);
	}
	if (result && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_LADDER)
	{
		player->GetOrigin(&player->lastValidLadderOrigin);
	}
	player->OnLadderMovePost();
	return {KHook::Action::Supersede, result};
}

static KHook::Return<void> CheckJumpButtonLegacyPre(CCSPlayerLegacyJump *legacy, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	CCSPlayer_MovementServices *ms = legacy->m_pMovementServices;
	MovementPlayer *player = playerManager->ToPlayer(ms);
#ifdef WATER_FIX
	if (player->enableWaterFix && ms->pawn->m_MoveType() == MOVETYPE_WALK && ms->pawn->m_flWaterLevel() > 0.5f && ms->pawn->m_fFlags & FL_ONGROUND)
	{
		if (ms->m_nButtons().m_pButtonStates[0] & IN_JUMP)
		{
			ms->m_nButtons().m_pButtonStates[1] |= IN_JUMP;
		}
	}
#endif
	player->OnCheckJumpButtonLegacy();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> CheckJumpButtonLegacyPost(CCSPlayerLegacyJump *legacy, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(legacy->m_pMovementServices)->OnCheckJumpButtonLegacyPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayerLegacyJump, void, CMoveData *> CheckJumpButtonLegacy(CheckJumpButtonLegacyPre, CheckJumpButtonLegacyPost);

static KHook::Return<void> OnJumpLegacyPre(CCSPlayerLegacyJump *legacy, CMoveData *mv);

static KHook::Member<CCSPlayerLegacyJump, void, CMoveData *> OnJumpLegacy(OnJumpLegacyPre, nullptr);

static KHook::Return<void> OnJumpLegacyPre(CCSPlayerLegacyJump *legacy, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	CCSPlayer_MovementServices *ms = legacy->m_pMovementServices;
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnJumpLegacy();
	Vector oldOutWishVel = mv->m_outWishVel;
	OnJumpLegacy.CallOriginal(legacy, mv);
	if (mv->m_outWishVel != oldOutWishVel)
	{
		player->inPerf = (!player->takeoffFromLadder && !player->oldWalkMoved);
		player->RegisterTakeoff(true);
		player->OnStopTouchGround();
	}
	player->OnJumpLegacyPost();
	return {KHook::Action::Supersede};
}

static KHook::Return<void> AirMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnAirMove();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> AirMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnAirMovePost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> AirMove(AirMovePre, AirMovePost);

static KHook::Return<void> AirAcceleratePre(CCSPlayer_MovementServices *ms, CMoveData *mv, Vector &wishdir, f32 wishspeed, f32 accel)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnAirAccelerate(wishdir, wishspeed, accel);
	return {KHook::Action::Ignore};
}

static KHook::Return<void> AirAcceleratePost(CCSPlayer_MovementServices *ms, CMoveData *mv, Vector &wishdir, f32 wishspeed, f32 accel)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnAirAcceleratePost(wishdir, wishspeed, accel);
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, Vector &, f32, f32> AirAccelerate(AirAcceleratePre, AirAcceleratePost);

static KHook::Return<void> FrictionPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnFriction();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> FrictionPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnFrictionPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> Friction(FrictionPre, FrictionPost);

static KHook::Return<void> WalkMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnWalkMove();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> WalkMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->walkMoved = true;
	player->OnWalkMovePost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> WalkMove(WalkMovePre, WalkMovePost);

static KHook::Return<void> TryPlayerMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv, Vector *pFirstDest, trace_t *pFirstTrace,
											bool *bIsSurfing);

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, Vector *, trace_t *, bool *> TryPlayerMove(TryPlayerMovePre, nullptr);

static KHook::Return<void> TryPlayerMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv, Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
#ifdef DEBUG_TPM
	traceHistory.RemoveAll();
	f32 initialError = ms->m_flAccumulatedJumpError();
	Vector initialVelocity = mv->m_vecVelocity;
	g_traceShapeEnabled = true;
	player->OnTryPlayerMove(pFirstDest, pFirstTrace, bIsSurfing);
	Vector oldVelocity = mv->m_vecVelocity;
	i32 count = traceHistory.Count();
	TryPlayerMove.CallOriginal(ms, mv, pFirstDest, pFirstTrace, bIsSurfing);
	if (traceHistory.Count() != count)
	{
		for (i32 i = 0; i + count < traceHistory.Count(); i++)
		{
			if (traceHistory[i].end != traceHistory[i + count].end)
			{
				META_CONPRINTF("Trace not matching! Previous traces (initial error %f, initial velocity %s):\n", initialError,
							   VecToString(initialVelocity));
				for (i32 j = 0; j <= i; j++)
				{
					META_CONPRINTF("Pred %f %f %f -> %f %f %f, error %f, velocity %s ", traceHistory[j].start.x, traceHistory[j].start.y,
								   traceHistory[j].start.z, traceHistory[j].end.x, traceHistory[j].end.y, traceHistory[j].end.z,
								   traceHistory[j].error, VecToString(traceHistory[j].velocity));
					if (traceHistory[j].didHit)
					{
						META_CONPRINTF("hit %s (normal %s, hitpoint %s)\n", VecToString(traceHistory[j].m_vEndPos),
									   VecToString(traceHistory[j].m_vHitNormal), VecToString(traceHistory[j].m_vHitPoint));
					}
					else
					{
						META_CONPRINTF("missed\n");
					}
					META_CONPRINTF("Real %f %f %f -> %f %f %f, error %f, velocity %s ", traceHistory[j + count].start.x,
								   traceHistory[j + count].start.y, traceHistory[j + count].start.z, traceHistory[j + count].end.x,
								   traceHistory[j + count].end.y, traceHistory[j + count].end.z, traceHistory[j + count].error,
								   VecToString(traceHistory[j + count].velocity));
					if (traceHistory[j + count].didHit)
					{
						META_CONPRINTF("hit %s (normal %s, hitpoint %s)\n", VecToString(traceHistory[j + count].m_vEndPos),
									   VecToString(traceHistory[j + count].m_vHitNormal), VecToString(traceHistory[j + count].m_vHitPoint));
					}
					else
					{
						META_CONPRINTF("missed\n");
					}
				}
				break;
			}
		}
	}
	g_traceShapeEnabled = false;
#else
	player->OnTryPlayerMove(pFirstDest, pFirstTrace, bIsSurfing);
	Vector oldVelocity = mv->m_vecVelocity;
	TryPlayerMove.CallOriginal(ms, mv, pFirstDest, pFirstTrace, bIsSurfing);
#endif
	if (mv->m_vecVelocity != oldVelocity)
	{
		player->SetCollidingWithWorld();
	}
	player->OnTryPlayerMovePost(pFirstDest, pFirstTrace, bIsSurfing);
	return {KHook::Action::Supersede};
}

static KHook::Return<void> CategorizePositionPre(CCSPlayer_MovementServices *ms, CMoveData *mv, bool bStayOnGround);

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, bool> CategorizePosition(CategorizePositionPre, nullptr);

static KHook::Return<void> CategorizePositionPre(CCSPlayer_MovementServices *ms, CMoveData *mv, bool bStayOnGround)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	MovementPlayer *player = playerManager->ToPlayer(ms);
#ifdef WATER_FIX
	if (player->enableWaterFix && player->ignoreNextCategorizePosition)
	{
		player->ignoreNextCategorizePosition = false;
		return {KHook::Action::Supersede};
	}
#endif
	player->OnCategorizePosition(bStayOnGround);
	Vector oldVelocity = mv->m_vecVelocity;
	bool oldOnGround = !!(player->GetPlayerPawn()->m_fFlags() & FL_ONGROUND);

	CategorizePosition.CallOriginal(ms, mv, bStayOnGround);

	bool ground = !!(player->GetPlayerPawn()->m_fFlags() & FL_ONGROUND);
	if (oldOnGround != ground)
	{
		if (ground)
		{
			player->RegisterLanding(oldVelocity);
			player->OnStartTouchGround();
		}
		else
		{
			player->RegisterTakeoff(false);
			player->OnStopTouchGround();
		}
	}
	player->OnCategorizePositionPost(bStayOnGround);
	return {KHook::Action::Supersede};
}

static KHook::Return<void> CheckFallingPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnCheckFalling();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> CheckFallingPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(ms)->OnCheckFallingPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> CheckFalling(CheckFallingPre, CheckFallingPost);

static KHook::Return<void> PostThinkPre(CCSPlayerPawnBase *pawn)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(pawn)->OnPostThink();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> PostThinkPost(CCSPlayerPawnBase *pawn)
{
	VPROF_BUDGET(__func__, "CS2Surf");
	playerManager->ToPlayer(pawn)->OnPostThinkPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayerPawnBase, void> PostThink(PostThinkPre, PostThinkPost);

struct SignatureHook
{
	const char *name;
	void (*configure)(void *address);
};

#define SIGNATURE_HOOK(hook) {#hook, [](void *address) { hook.Configure(address); }}

static_global const SignatureHook SIGNATURE_HOOKS[] = {
	SIGNATURE_HOOK(RecvServerBrowserPacket),
	SIGNATURE_HOOK(CPhysicsGameSystemFrameBoundary),
#ifdef DEBUG_TPM
	SIGNATURE_HOOK(TraceShape),
#endif
	SIGNATURE_HOOK(PhysicsSimulate),
	SIGNATURE_HOOK(ProcessUsercmds),
	SIGNATURE_HOOK(SetupMove),
	SIGNATURE_HOOK(ProcessMovement),
	SIGNATURE_HOOK(PlayerMove),
	SIGNATURE_HOOK(CheckParameters),
	SIGNATURE_HOOK(CanMove),
	SIGNATURE_HOOK(FullWalkMove),
	SIGNATURE_HOOK(MoveInit),
	SIGNATURE_HOOK(CheckWater),
	SIGNATURE_HOOK(WaterMove),
	SIGNATURE_HOOK(CheckVelocity),
	SIGNATURE_HOOK(Duck),
	SIGNATURE_HOOK(CanUnduck),
	SIGNATURE_HOOK(LadderMove),
	SIGNATURE_HOOK(CheckJumpButtonLegacy),
	SIGNATURE_HOOK(OnJumpLegacy),
	SIGNATURE_HOOK(AirMove),
	SIGNATURE_HOOK(AirAccelerate),
	SIGNATURE_HOOK(Friction),
	SIGNATURE_HOOK(WalkMove),
	SIGNATURE_HOOK(TryPlayerMove),
	SIGNATURE_HOOK(CategorizePosition),
	SIGNATURE_HOOK(CheckFalling),
	SIGNATURE_HOOK(PostThink),
};

#undef SIGNATURE_HOOK

// ============================================================
// hooks::Initialize
// ============================================================
bool hooks::Initialize(char *error, size_t maxlen)
{
	// Resolve every signature before anything is hooked, so outdated gamedata aborts the load instead of running with missing detours.
	void *signatureAddresses[SURF_ARRAYSIZE(SIGNATURE_HOOKS)];
	std::string missingSignatures;
	for (u32 i = 0; i < SURF_ARRAYSIZE(SIGNATURE_HOOKS); i++)
	{
		signatureAddresses[i] = g_pGameConfig->ResolveSignature(SIGNATURE_HOOKS[i].name);
		if (!signatureAddresses[i])
		{
			missingSignatures += missingSignatures.empty() ? "" : ", ";
			missingSignatures += SIGNATURE_HOOKS[i].name;
		}
	}
	if (!missingSignatures.empty())
	{
		snprintf(error, maxlen, "Failed to resolve signatures: %s", missingSignatures.c_str());
		return false;
	}

	playerManager = static_cast<MovementPlayerManager *>(g_pPlayerManager);

	// Entity hooks
	startTouchHook.Configure(g_pGameConfig->GetOffset("StartTouch"));
	touchHook.Configure(g_pGameConfig->GetOffset("Touch"));
	endTouchHook.Configure(g_pGameConfig->GetOffset("EndTouch"));
	teleportHook.Configure(g_pGameConfig->GetOffset("Teleport"));
	changeTeamHook.Configure(g_pGameConfig->GetOffset("ControllerChangeTeam"));
	playerRunCommandHook.Configure(g_pGameConfig->GetOffset("PlayerRunCommand"));
	finishMoveHook.Configure(g_pGameConfig->GetOffset("FinishMove"));

	// Interface hooks
	checkTransmitHook.Configure(&ISource2GameEntities::CheckTransmit);
	checkTransmitHook.Add(g_pSource2GameEntities);

	gameFrameHook.Configure(&ISource2Server::GameFrame);
	gameFrameHook.Add(interfaces::pServer);

	gameServerSteamAPIActivatedHook.Configure(&ISource2Server::GameServerSteamAPIActivated);
	gameServerSteamAPIActivatedHook.Add(interfaces::pServer);

	gameServerSteamAPIDeactivatedHook.Configure(&ISource2Server::GameServerSteamAPIDeactivated);
	gameServerSteamAPIDeactivatedHook.Add(interfaces::pServer);

	clientConnectHook.Configure(&ISource2GameClients::ClientConnect);
	clientConnectHook.Add(g_pSource2GameClients);

	onClientConnectedHook.Configure(&ISource2GameClients::OnClientConnected);
	onClientConnectedHook.Add(g_pSource2GameClients);

	clientFullyConnectHook.Configure(&ISource2GameClients::ClientFullyConnect);
	clientFullyConnectHook.Add(g_pSource2GameClients);

	clientPutInServerHook.Configure(&ISource2GameClients::ClientPutInServer);
	clientPutInServerHook.Add(g_pSource2GameClients);

	clientActiveHook.Configure(&ISource2GameClients::ClientActive);
	clientActiveHook.Add(g_pSource2GameClients);

	clientDisconnectHook.Configure(&ISource2GameClients::ClientDisconnect);
	clientDisconnectHook.Add(g_pSource2GameClients);

	clientVoiceHook.Configure(&ISource2GameClients::ClientVoice);
	clientVoiceHook.Add(g_pSource2GameClients);

	clientCommandHook.Configure(&ISource2GameClients::ClientCommand);
	clientCommandHook.Add(g_pSource2GameClients);

	// clientSvcUserMessageHook.Configure(&ISource2GameClients::ClientSvcUserMessage);
	// clientSvcUserMessageHook.Add(g_pSource2GameClients);

	startupServerHook.Configure(&INetworkServerService::StartupServer);
	startupServerHook.Add(g_pNetworkServerService);

	fireEventHook.Configure(&IGameEventManager2::FireEvent);
	fireEventHook.Add(interfaces::pGameEventManager);

	dispatchConCommandHook.Configure(&ICvar::DispatchConCommand);
	dispatchConCommandHook.Add(g_pCVar);

	postEventHook.Configure(&IGameEventSystem::PostEventAbstract);
	postEventHook.Add(interfaces::pGameEventSystem);

	// Hooks by searching virtual tables
	{
		void *vtable = modules::engine->FindVirtualTable("CNetworkGameServer");
		activateServerHook.Configure(&CNetworkGameServerBase::ActivateServer);
		activateServerHook.AddGlobal((CNetworkGameServerBase *)&vtable);

		connectClientHook.Configure(&CNetworkGameServerBase::ConnectClient);
		connectClientHook.AddGlobal((CNetworkGameServerBase *)&vtable);
	}

	{
		void *vtable = modules::engine->FindVirtualTable("CServerSideClient");
		respondCvarValueHook.Configure(&CServerSideClientBase::ProcessRespondCvarValue);
		respondCvarValueHook.AddGlobal((CServerSideClientBase *)&vtable);

		setConVarHook.Configure(&CServerSideClientBase::ProcessSetConVar);
		setConVarHook.AddGlobal((CServerSideClientBase *)&vtable);
	}

	{
		void *vtable = modules::server->FindVirtualTable("CEntityDebugGameSystem");
		serverGamePostSimulateHook.Configure(&IGameSystem::OnServerGamePostSimulate);
		serverGamePostSimulateHook.AddGlobal((IGameSystem *)&vtable);

		buildGameSessionManifestHook.Configure(&IGameSystem::OnBuildGameSessionManifest);
		buildGameSessionManifestHook.AddGlobal((IGameSystem *)&vtable);
	}

	{
		void *vtable = modules::server->FindVirtualTable("CGameEntitySystem");
		entitySystemSpawnHook.Configure(&CEntitySystem::Spawn);
		entitySystemSpawnHook.AddGlobal((CEntitySystem *)&vtable);
	}

	{
		void *vtable = modules::server->FindVirtualTable("CSpawnGroupMgrGameSystem");
		createLoadingSpawnGroupHook.Configure(&CSpawnGroupMgrGameSystem::CreateLoadingSpawnGroup);
		createLoadingSpawnGroupHook.AddGlobal((CSpawnGroupMgrGameSystem *)&vtable);
	}

	{
		void *vtable = modules::server->FindVirtualTable("CCSPlayer_MovementServices");
		playerRunCommandHook.AddGlobal((CCSPlayer_MovementServices *)&vtable);
		finishMoveHook.AddGlobal((CCSPlayer_MovementServices *)&vtable);
	}

	// Signature-based hooks
	for (u32 i = 0; i < SURF_ARRAYSIZE(SIGNATURE_HOOKS); i++)
	{
		SIGNATURE_HOOKS[i].configure(signatureAddresses[i]);
	}
	return true;
}

void hooks::Cleanup()
{
	startTouchHook.ClearHooks();
	touchHook.ClearHooks();
	endTouchHook.ClearHooks();
	teleportHook.ClearHooks();
	changeTeamHook.ClearHooks();

	checkTransmitHook.ClearHooks();
	gameFrameHook.ClearHooks();
	gameServerSteamAPIActivatedHook.ClearHooks();
	gameServerSteamAPIDeactivatedHook.ClearHooks();
	clientConnectHook.ClearHooks();
	onClientConnectedHook.ClearHooks();
	clientFullyConnectHook.ClearHooks();
	clientPutInServerHook.ClearHooks();
	clientActiveHook.ClearHooks();
	clientDisconnectHook.ClearHooks();
	clientVoiceHook.ClearHooks();
	clientCommandHook.ClearHooks();
	// clientSvcUserMessageHook.ClearHooks();
	startupServerHook.ClearHooks();
	fireEventHook.ClearHooks();
	dispatchConCommandHook.ClearHooks();
	postEventHook.ClearHooks();
	activateServerHook.ClearHooks();
	connectClientHook.ClearHooks();
	respondCvarValueHook.ClearHooks();
	setConVarHook.ClearHooks();
	serverGamePostSimulateHook.ClearHooks();
	buildGameSessionManifestHook.ClearHooks();
	entitySystemSpawnHook.ClearHooks();
	createLoadingSpawnGroupHook.ClearHooks();
	playerRunCommandHook.ClearHooks();
	finishMoveHook.ClearHooks();

	cvarquery::Shutdown();

	if (GameEntitySystem())
	{
		GameEntitySystem()->RemoveListenerEntity(&entityListener);
	}
}

// ============================================================
// Entity hook management
// ============================================================
void hooks::AddEntityHooks(CBaseEntity *entity)
{
	if (!V_stricmp(entity->GetClassname(), "cs_player_controller"))
	{
		changeTeamHook.Add(static_cast<CCSPlayerController *>(entity));
	}
	else if (SurfTriggerService::IsValidTrigger(entity) || !V_stricmp(entity->GetClassname(), "player"))
	{
		startTouchHook.Add(entity);
		touchHook.Add(entity);
		endTouchHook.Add(entity);
		CCSPlayerPawn *pawn = static_cast<CCSPlayerPawn *>(entity);
		if (!V_stricmp(entity->GetClassname(), "player") && g_pSurfPlayerManager->ToPlayer(pawn))
		{
			teleportHook.Add(entity);
		}
	}
}

void hooks::RemoveEntityHooks(CBaseEntity *entity)
{
	if (SurfTriggerService::IsValidTrigger(entity) || !V_stricmp(entity->GetClassname(), "player"))
	{
		startTouchHook.Remove(entity);
		touchHook.Remove(entity);
		endTouchHook.Remove(entity);
		if (!SurfTriggerService::IsValidTrigger(entity))
		{
			teleportHook.Remove(entity);
		}
	}
}

void EntListener::OnEntityCreated(CEntityInstance *pEntity) {}

void EntListener::OnEntitySpawned(CEntityInstance *pEntity)
{
	if (SurfTriggerService::IsValidTrigger(static_cast<CBaseEntity *>(pEntity)))
	{
		CBaseTrigger *trigger = static_cast<CBaseTrigger *>(pEntity);
		trigger->m_fEffects() &= ~EF_NODRAW;
		hooks::AddEntityHooks(static_cast<CBaseEntity *>(pEntity));
		Surf::mapapi::CheckEndTimerTrigger((CBaseTrigger *)pEntity);
	}
}

void EntListener::OnEntityDeleted(CEntityInstance *pEntity)
{
	if (SurfTriggerService::IsValidTrigger(static_cast<CBaseEntity *>(pEntity)))
	{
		hooks::RemoveEntityHooks(static_cast<CBaseEntity *>(pEntity));
	}
}

void hooks::HookEntities()
{
	startTouchHook.ClearHooks();
	touchHook.ClearHooks();
	endTouchHook.ClearHooks();
	teleportHook.ClearHooks();

	GameEntitySystem()->RemoveListenerEntity(&entityListener);
	for (CEntityIdentity *entID = GameEntitySystem()->m_EntityList.m_pFirstActiveEntity; entID != NULL; entID = entID->m_pNext)
	{
		hooks::AddEntityHooks(static_cast<CBaseEntity *>(entID->m_pInstance));
	}
	GameEntitySystem()->AddListenerEntity(&entityListener);
}
