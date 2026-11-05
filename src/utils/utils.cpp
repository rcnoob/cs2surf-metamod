#include "utils.h"
#include "cs2surf.h"
#include "player/player.h"
#include "sdk/entity/cbasetrigger.h"
#include "sdk/recipientfilters.h"
#include "sdk/navphysicsinterface.h"
#include "addresses.h"
#include "logging.h"
#include "patches.h"
#include "gameconfig.h"

#include "module.h"
#include "virtual.h"

#include "networkbasetypes.pb.h"

#include "convar.h"
#include "engine2/igameeventsystem.h"
#include "gametrace.h"
#include "bspflags.h"
#include "filesystem/ifilesystem.h"
#include "interfaces/interfaces.h"
#include "networksystem/inetworkmessages.h"
#include "steam/steam_gameserver.h"

#include "tier0/dbg.h"
#include "tier0/memdbgon.h"
#include <filesystem>

#define FCVAR_FLAGS_TO_REMOVE (FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY | FCVAR_DEFENSIVE)

#define RESOLVE_SIG(gameConfig, name, type, variable, result) \
	type *variable = (decltype(variable))gameConfig->ResolveSignature(name); \
	if (!variable) \
	{ \
		Warning("Failed to find address for %s!\n", #name); \
		result = false; \
	}

CGameConfig *g_pGameConfig = NULL;
SurfUtils *g_pSurfUtils = NULL;

#define SERVER_VERSION_KEY "ServerVersion="
static_global u32 serverVersion;

bool utils::Initialize(ISmmAPI *ismm, char *error, size_t maxlen)
{
	CBufferStringGrowable<256> gamedirpath;
	interfaces::pEngine->GetGameDir(gamedirpath);

	std::string gamedirname = CGameConfig::GetDirectoryName(gamedirpath.Get());
	const char *gamedataPath = "addons/cs2surf/gamedata/cs2surf-core.games.txt";

	g_pGameConfig = new CGameConfig(gamedirname, gamedataPath);
	char conf_error[255] = "";
	if (!g_pGameConfig->Init(g_pFullFileSystem, conf_error, sizeof(conf_error)))
	{
		snprintf(error, maxlen, "Could not read %s: %s", g_pGameConfig->GetPath().c_str(), conf_error);
		Warning("%s\n", error);
		return false;
	}

	// Convoluted way of having GameEventManager regardless of lateloading
	if (!(interfaces::pGameEventManager = (IGameEventManager2 *)g_pGameConfig->ResolveSignatureFromMov("GameEventManager")))
	{
		return false;
	}

	bool sigResolved = true;
	RESOLVE_SIG(g_pGameConfig, "GetLegacyGameEventListener", GetLegacyGameEventListener_t, GetLegacyGameEventListener, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "SnapViewAngles", SnapViewAngles_t, SnapViewAngles, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "EmitSound", EmitSoundFunc_t, EmitSound, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "CCSPlayerController_SwitchTeam", SwitchTeam_t, SwitchTeam, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "CBasePlayerController_SetPawn", SetPawn_t, SetPawn, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "CreateEntityByName", CreateEntityByName_t, CreateEntityByName, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "DispatchSpawn", DispatchSpawn_t, DispatchSpawn, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "RemoveEntity", RemoveEntity_t, RemoveEntity, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "DebugDrawMesh", DebugDrawMesh_t, DebugDrawMesh, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "CreateBot", CreateBot_t, CreateBot, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "SetOrAddAttributeValueByName", SetOrAddAttributeValueByName_t, SetOrAddAttributeValueByName, sigResolved);
	RESOLVE_SIG(g_pGameConfig, "SetModel", SetModel_t, SetModel, sigResolved);

	if (!sigResolved)
	{
		snprintf(error, maxlen, "Failed to resolve one or more signatures.");
		Warning("%s\n", error);
		return false;
	}

	if (!InitPatches(g_pGameConfig))
	{
		snprintf(error, maxlen, "Failed to initialize patches.");
		Warning("%s\n", error);
		return false;
	}

	g_pSurfUtils = new SurfUtils(GetLegacyGameEventListener, SnapViewAngles, EmitSound, SwitchTeam, SetPawn, CreateEntityByName, DispatchSpawn,
								 RemoveEntity, DebugDrawMesh, CreateBot, SetOrAddAttributeValueByName, SetModel);

	utils::UnlockConVars();
	utils::UnlockConCommands();
	utils::UpdateServerVersion();
	return true;
}

void utils::Cleanup()
{
	delete g_pGameConfig;
	g_pGameConfig = NULL;
	delete g_pSurfUtils;
	g_pSurfUtils = NULL;
	modules::Cleanup();
}

CBaseEntity *utils::FindEntityByClassname(CEntityInstance *start, const char *name)
{
	if (!GameEntitySystem())
	{
		return NULL;
	}
	EntityInstanceByClassIter_t iter(start, name);

	return static_cast<CBaseEntity *>(iter.Next());
}

bool utils::IsVectorInBox(const Vector &point, const Vector &mins, const Vector &maxs)
{
	return (point.x >= mins.x && point.x <= maxs.x && point.y >= mins.y && point.y <= maxs.y && point.z >= mins.z && point.z <= maxs.z);
}

void utils::UnlockConVars()
{
	if (!g_pCVar)
	{
		return;
	}

	ConVarRefAbstract ref(ConVarRef((u16)0));

	// Can't use FOR_EACH_CONVAR here (?) as it would skip cvars with certain flags, so just loop through the handles
	while (ref.IsValidRef())
	{
		if (ref.IsConVarDataAvailable())
		{
			ref.RemoveFlags(FCVAR_FLAGS_TO_REMOVE);
		}
		ref = {ConVarRef(ref.GetAccessIndex() + 1)};
	}
}

void utils::UnlockConCommands()
{
	if (!g_pCVar)
	{
		return;
	}

	ConCommandRef conCommand = {(u16)0};
	ConCommandData *conCommandData = conCommand.GetRawData();
	ConCommandData *invalidData = g_pCVar->GetConCommandData(ConCommandRef());
	while (conCommandData != invalidData)
	{
		if (conCommand.IsFlagSet(FCVAR_FLAGS_TO_REMOVE))
		{
			conCommand.RemoveFlags(FCVAR_FLAGS_TO_REMOVE);
		}
		conCommand = {(uint16)(conCommand.GetAccessIndex() + 1)};
		conCommandData = conCommand.GetRawData();
	}
}

CBasePlayerController *utils::GetController(CBaseEntity *entity)
{
	CCSPlayerController *controller = nullptr;
	if (!V_stricmp(entity->GetClassname(), "observer"))
	{
		CBasePlayerPawn *pawn = static_cast<CBasePlayerPawn *>(entity);
		if (!pawn->m_hController().IsValid() || pawn->m_hController.Get() == 0)
		{
			for (i32 i = 0; i <= MAX_PLAYERS; i++)
			{
				controller = (CCSPlayerController *)utils::GetController(CPlayerSlot(i));
				if (controller && controller->m_hObserverPawn() && controller->m_hObserverPawn().Get() == entity)
				{
					return controller;
				}
			}
			return nullptr;
		}
		return pawn->m_hController.Get();
	}
	if (entity->IsPawn())
	{
		CBasePlayerPawn *pawn = static_cast<CBasePlayerPawn *>(entity);
		if (!pawn->m_hController().IsValid() || pawn->m_hController.Get() == 0)
		{
			// Seems like the pawn lost its controller, we can try looping through the controllers to find this pawn instead.
			for (i32 i = 0; i <= MAX_PLAYERS; i++)
			{
				controller = (CCSPlayerController *)utils::GetController(CPlayerSlot(i));
				if (controller && controller->m_hPlayerPawn() && controller->m_hPlayerPawn().Get() == entity)
				{
					return controller;
				}
			}
			return nullptr;
		}
		return pawn->m_hController.Get();
	}
	else if (entity->IsController())
	{
		return static_cast<CBasePlayerController *>(entity);
	}
	else
	{
		return nullptr;
	}
}

CBasePlayerController *utils::GetController(CPlayerSlot slot)
{
	if (!GameEntitySystem() || slot.Get() < 0 || slot.Get() > MAX_PLAYERS)
	{
		return nullptr;
	}
	CBaseEntity *ent = static_cast<CBaseEntity *>(GameEntitySystem()->GetEntityInstance(CEntityIndex(slot.Get() + 1)));
	if (!ent)
	{
		return nullptr;
	}
	return ent->IsController() ? static_cast<CBasePlayerController *>(ent) : nullptr;
}

CPlayerSlot utils::GetEntityPlayerSlot(CBaseEntity *entity)
{
	CBasePlayerController *controller = utils::GetController(entity);
	if (!controller)
	{
		return -1;
	}
	else
	{
		return controller->m_pEntity->m_EHandle.GetEntryIndex() - 1;
	}
}

void utils::PlaySoundToClient(CPlayerSlot player, const char *sound, f32 volume)
{
	if (strncmp(sound, "surf.", strlen("surf.")) == 0 && !g_SurfPlugin.IsAddonMounted())
	{
		return;
	}

	if (g_SurfPlugin.unloading)
	{
		return;
	}

	CReliableSingleUserRecipientFilter filter(player.Get());
	EmitSound_t soundParams;
	soundParams.m_pSoundName = sound;
	soundParams.m_flVolume = volume;
	g_pSurfUtils->EmitSound(filter, player.Get() + 1, soundParams);
}

f32 utils::NormalizeDeg(f32 a)
{
	a = fmod(a, 360.0);
	if (a >= 180.0)
	{
		a -= 360.0;
	}
	else if (a < -180.0)
	{
		a += 360.0;
	}
	return a;
}

f32 utils::GetAngleDifference(const f32 source, const f32 target, const f32 c, bool relative)
{
	if (relative)
	{
		return fmod((fmod(target - source, 2 * c) + 3 * c), 2 * c) - c;
	}
	return fmod(fabs(target - source) + c, 2 * c) - c;
}

bool utils::SetConVarValue(CPlayerSlot slot, const char *name, const char *value, bool replicate, bool triggerCallback)
{
	if (!name || !value)
	{
		assert(0);
		return false;
	}

	ConVarRefAbstract cvarRef(name);
	if (!cvarRef.IsValidRef() || !cvarRef.IsConVarDataAvailable())
	{
		assert(0);
		SURF_LOG_WARN(LogChannel::General, "Failed to find %s!\n", name);
		return false;
	}

	if (triggerCallback)
	{
		cvarRef.SetString(value);
	}
	else
	{
		CVValue_t newValue;
		cvarRef.TypeTraits()->Construct(&newValue);
		cvarRef.TypeTraits()->StringToValue(value, &newValue);
		cvarRef.TypeTraits()->Copy(cvarRef.GetConVarData()->Value(-1), newValue);
		cvarRef.TypeTraits()->Destruct(&newValue);
	}

	if (replicate)
	{
		SendConVarValue(slot, cvarRef, value);
	}

	return true;
}

bool utils::SetConVarValue(CPlayerSlot slot, ConVarRefAbstract conVarRef, const char *value, bool replicate, bool triggerCallback)
{
	if (!conVarRef.IsValidRef() || !conVarRef.IsConVarDataAvailable())
	{
		assert(0);
		// If the ref is not valid, then the name is not available here, so this will silently fail.
		return false;
	}

	if (triggerCallback)
	{
		conVarRef.SetString(value);
	}
	else
	{
		CVValue_t newValue;
		conVarRef.TypeTraits()->Construct(&newValue);
		conVarRef.TypeTraits()->StringToValue(value, &newValue);
		conVarRef.TypeTraits()->Copy(conVarRef.GetConVarData()->Value(-1), newValue);
		conVarRef.TypeTraits()->Destruct(&newValue);
	}

	if (replicate)
	{
		SendConVarValue(slot, conVarRef, value);
	}

	return true;
}

bool utils::SetConVarValue(CPlayerSlot slot, ConVarRefAbstract conVarRef, const CVValue_t *value, bool replicate, bool triggerCallback)
{
	if (!conVarRef.IsValidRef() || !conVarRef.IsConVarDataAvailable())
	{
		assert(0);
		// If the ref is not valid, then the name is not available here, so this will silently fail.
		return false;
	}

	CBufferString buf;
	conVarRef.TypeTraits()->ValueToString(value, buf);
	if (triggerCallback)
	{
		conVarRef.SetString(buf);
	}
	else
	{
		conVarRef.TypeTraits()->Copy(conVarRef.GetConVarData()->Value(-1), *value);
	}

	if (replicate)
	{
		SendConVarValue(slot, conVarRef, buf.Get());
	}

	return true;
}

void utils::SendConVarValue(CPlayerSlot slot, const char *conVar, const char *value)
{
	INetworkMessageInternal *netmsg = g_pNetworkMessages->FindNetworkMessagePartial("SetConVar");
	auto msg = netmsg->AllocateMessage()->ToPB<CNETMsg_SetConVar>();
	CMsg_CVars_CVar *cvar = msg->mutable_convars()->add_cvars();
	cvar->set_name(conVar);
	cvar->set_value(value);
	CReliableSingleUserRecipientFilter filter(slot.Get());
	interfaces::pGameEventSystem->PostEventAbstract(0, false, &filter, netmsg, msg, 0);
	delete msg;
}

void utils::SendMultipleConVarValues(CPlayerSlot slot, const char **cvars, const char **values, u32 size)
{
	INetworkMessageInternal *netmsg = g_pNetworkMessages->FindNetworkMessagePartial("SetConVar");
	auto msg = netmsg->AllocateMessage()->ToPB<CNETMsg_SetConVar>();
	for (u32 i = 0; i < size; i++)
	{
		CMsg_CVars_CVar *cvar = msg->mutable_convars()->add_cvars();
		cvar->set_name(cvars[i]);
		cvar->set_value(values[i]);
	}
	CReliableSingleUserRecipientFilter filter(slot.Get());
	interfaces::pGameEventSystem->PostEventAbstract(0, false, &filter, netmsg, msg, 0);
	delete msg;
}

void utils::SendConVarValue(CPlayerSlot slot, ConVarRefAbstract conVarRef, const char *value)
{
	if (!conVarRef.IsValidRef() || !conVarRef.IsConVarDataAvailable())
	{
		return;
	}
	utils::SendConVarValue(slot, conVarRef.GetName(), value);
}

void utils::SendConVarValue(CPlayerSlot slot, ConVarRefAbstract conVarRef, const CVValue_t *value)
{
	if (!conVarRef.IsValidRef() || !conVarRef.IsConVarDataAvailable())
	{
		return;
	}
	CBufferString valueStr;
	conVarRef.GetConVarData()->TypeTraits()->ValueToString(value, valueStr);
	utils::SendConVarValue(slot, conVarRef.GetName(), valueStr.Get());
}

void utils::SendMultipleConVarValues(CPlayerSlot slot, ConVarRefAbstract **conVarRefs, const char **values, u32 size)
{
	INetworkMessageInternal *netmsg = g_pNetworkMessages->FindNetworkMessagePartial("SetConVar");
	auto msg = netmsg->AllocateMessage()->ToPB<CNETMsg_SetConVar>();
	for (u32 i = 0; i < size; i++)
	{
		if (!conVarRefs[i]->IsValidRef())
		{
			delete msg;
			return;
		}
		CMsg_CVars_CVar *cvar = msg->mutable_convars()->add_cvars();
		cvar->set_name(conVarRefs[i]->GetName());
		cvar->set_value(values[i]);
	}
	CReliableSingleUserRecipientFilter filter(slot.Get());
	interfaces::pGameEventSystem->PostEventAbstract(0, false, &filter, netmsg, msg, 0);
	delete msg;
}

void utils::SendMultipleConVarValues(CPlayerSlot slot, ConVarRefAbstract **conVarRefs, const CVValue_t *values, u32 size)
{
	INetworkMessageInternal *netmsg = g_pNetworkMessages->FindNetworkMessagePartial("SetConVar");
	auto msg = netmsg->AllocateMessage()->ToPB<CNETMsg_SetConVar>();
	for (u32 i = 0; i < size; i++)
	{
		if (!conVarRefs[i]->IsValidRef())
		{
			delete msg;
			return;
		}
		CBufferString buf;
		conVarRefs[i]->TypeTraits()->ValueToString(&values[i], buf);
		CMsg_CVars_CVar *cvar = msg->mutable_convars()->add_cvars();
		cvar->set_name(conVarRefs[i]->GetName());
		cvar->set_value(buf.Get());
	}
	CSingleUserRecipientFilter filter(slot.Get());
	interfaces::pGameEventSystem->PostEventAbstract(0, false, &filter, netmsg, msg, 0);
	delete msg;
}

bool utils::IsSpawnValid(const Vector &origin)
{
	bbox_t bounds = {{-16.0f, -16.0f, 0.0f}, {16.0f, 16.0f, 72.0f}};
	CTraceFilter filter;
	filter.m_bHitSolid = true;
	filter.m_bHitSolidRequiresGenerateContacts = true;
	filter.m_bShouldIgnoreDisabledPairs = true;
	filter.m_nCollisionGroup = COLLISION_GROUP_DEBRIS;
	filter.m_nInteractsWith = 0x2c3011;
	filter.m_unk101 = true;
	filter.m_nObjectSetMask = RNQUERY_OBJECTS_ALL;
	filter.m_nInteractsAs = 0x40000;
	trace_t tr;
	INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), origin, origin, &filter, &tr);
	if (tr.m_flFraction != 1.0 || tr.m_bStartInSolid)
	{
		return false;
	}
	return true;
}

bool utils::FindValidSpawn(Vector &origin, QAngle &angles)
{
	bool foundValidSpawn = false;
	bool searchCT = false;
	Vector spawnOrigin;
	QAngle spawnAngles;
	CBaseEntity *spawnEntity = NULL;
	while (!foundValidSpawn)
	{
		if (searchCT)
		{
			spawnEntity = FindEntityByClassname(spawnEntity, "info_player_counterterrorist");
		}
		else
		{
			spawnEntity = FindEntityByClassname(spawnEntity, "info_player_terrorist");
		}

		if (spawnEntity != NULL)
		{
			spawnOrigin = spawnEntity->m_CBodyComponent->m_pSceneNode->m_vecAbsOrigin;
			spawnAngles = spawnEntity->m_CBodyComponent->m_pSceneNode->m_angRotation;
			if (utils::IsSpawnValid(spawnOrigin))
			{
				origin = spawnOrigin;
				angles = spawnAngles;
				foundValidSpawn = true;
			}
		}
		else if (!searchCT)
		{
			searchCT = true;
		}
		else
		{
			break;
		}
	}
	return foundValidSpawn;
}

bool utils::CanSeeBox(Vector origin, Vector mins, Vector maxs)
{
	Vector traceDest;

	for (int i = 0; i < 3; i++)
	{
		mins[i] += 0.03125;
		maxs[i] -= 0.03125;
		traceDest[i] = Clamp(origin[i], mins[i], maxs[i]);
	}
	CTraceFilter filter(MASK_PLAYERSOLID, COLLISION_GROUP_PLAYER_MOVEMENT, false);
	trace_t trace;
	INavPhysicsInterface::TraceLine(origin, traceDest, &filter, &trace);

	return !trace.DidHit();
}

bool utils::FindValidPositionAroundCenter(Vector center, Vector distFromCenter, Vector extraOffset, Vector &originDest, QAngle &anglesDest)
{
	Vector testOrigin;
	i32 x, y;

	for (u32 i = 0; i < 3; i++)
	{
		x = i == 2 ? -1 : i;
		for (int j = 0; j < 3; j++)
		{
			y = j == 2 ? -1 : j;
			for (int z = -1; z <= 1; z++)
			{
				testOrigin = center;
				testOrigin[0] = testOrigin[0] + (distFromCenter[0] + extraOffset[0]) * x + 32.0f * x * 0.5;
				testOrigin[1] = testOrigin[1] + (distFromCenter[1] + extraOffset[1]) * y + 32.0f * y * 0.5;
				testOrigin[2] = testOrigin[2] + (distFromCenter[2] + extraOffset[2]) * z + 72.0f * z;

				if (utils::IsSpawnValid(testOrigin) && utils::CanSeeBox(testOrigin, center - distFromCenter, center + distFromCenter))
				{
					originDest = testOrigin;
					// Always look towards the center.
					Vector offsetVector;
					offsetVector[0] = -(distFromCenter[0] + extraOffset[0]) * x;
					offsetVector[1] = -(distFromCenter[1] + extraOffset[1]) * y;
					offsetVector[2] = -(distFromCenter[2] + extraOffset[2]) * z;
					VectorAngles(offsetVector, anglesDest);
					anglesDest[2] = 0.0; // Roll should always be 0.0
					return true;
				}
			}
		}
	}
	return false;
}

bool utils::FindValidPositionForTrigger(CBaseTrigger *trigger, Vector &originDest, QAngle &anglesDest)
{
	if (!trigger)
	{
		return false;
	}
	// Let's just assume this trigger isn't rotated at all...
	Vector mins = trigger->m_pCollision()->m_vecMins();
	Vector maxs = trigger->m_pCollision()->m_vecMaxs();
	Vector origin = trigger->m_CBodyComponent->m_pSceneNode->m_vecAbsOrigin();
	Vector center = origin + (maxs + mins) / 2;
	Vector distFromCenter = (maxs - mins) / 2;

	// If the center or the bottom center is valid (which should be the case most of the time), then we can skip all the complicated stuff.
	Vector bottomCenter = center;
	bottomCenter.z -= distFromCenter.z - 0.03125;
	if (utils::IsSpawnValid(bottomCenter))
	{
		originDest = bottomCenter;
		anglesDest = vec3_angle;
		return true;
	}

	if (utils::IsSpawnValid(center))
	{
		bbox_t bounds = {{-16.0f, -16.0f, 0.0f}, {16.0f, 16.0f, 72.0f}};
		CTraceFilter filter;
		filter.m_bHitSolid = true;
		filter.m_bHitSolidRequiresGenerateContacts = true;
		filter.m_bShouldIgnoreDisabledPairs = true;
		filter.m_nCollisionGroup = COLLISION_GROUP_DEBRIS;
		filter.m_nInteractsWith = 0x2c3011;
		filter.m_unk101 = true;
		filter.m_nObjectSetMask = RNQUERY_OBJECTS_ALL;
		filter.m_nInteractsAs = 0x40000;
		trace_t tr;
		INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), center, bottomCenter, &filter, &tr);
		originDest = tr.m_vEndPos;
		anglesDest = vec3_angle;
		return true;
	}

	// TODO: This is mostly ported from GOKZ with no testing done on these. Might need to test this eventually.
	Vector extraOffset = {-33.03125f, -33.03125f, -72.03125f}; // Negative because we want to go inwards.
	if (FindValidPositionAroundCenter(center, distFromCenter, extraOffset, originDest, anglesDest))
	{
		return true;
	}
	// Test the positions right next to the trigger if the tests above fail.
	// This can fail when the trigger has a cover brush over it.
	extraOffset = {0.03125f, 0.03125f, 0.03125f};
	return FindValidPositionAroundCenter(center, distFromCenter, extraOffset, originDest, anglesDest);
}

void utils::ResetMapIfEmpty()
{
	// There are players in the server already, do not restart
	if (g_pSurfUtils->GetPlayerCount() > 0)
	{
		return;
	}

	// Don't restart if the server is just up to map reload loops.
	if (g_pSurfUtils->GetGlobals() && g_pSurfUtils->GetGlobals()->curtime < 30.0f)
	{
		return;
	}

	// Another way the map reload can loop forever...
	if (CommandLine()->HasParm("-servertime"))
	{
		return;
	}

	SURF_LOG_INFO(LogChannel::General, "Server is empty, triggering map reload...\n");
	utils::ResetMap();
}

void utils::ResetMap()
{
	char cmd[MAX_PATH + 12]; // "changelevel " takes 12 characters
	if (g_pSurfUtils->GetCurrentMapWorkshopID() == 0)
	{
		if (!g_pSurfUtils->GetGlobals() || !g_pSurfUtils->GetGlobals()->mapname.ToCStr() || g_pSurfUtils->GetGlobals()->mapname.ToCStr()[0] == 0)
		{
			SURF_LOG_WARN(LogChannel::General, "Warning: Map name is empty, cannot reload the current map! Defaulting to de_dust2...\n");
			V_snprintf(cmd, sizeof(cmd), "changelevel de_dust2");
		}
		else
		{
			V_snprintf(cmd, sizeof(cmd), "changelevel %s", g_pSurfUtils->GetGlobals()->mapname.ToCStr());
		}
	}
	else
	{
		V_snprintf(cmd, sizeof(cmd), "host_workshop_map %llu", g_pSurfUtils->GetCurrentMapWorkshopID());
	}

	interfaces::pEngine->ServerCommand(cmd);
}

bool utils::IsServerSecure()
{
	if (!SteamGameServer())
	{
		return !(CommandLine()->HasParm("-insecure") || CommandLine()->HasParm("-tools"));
	}
	return SteamGameServer()->BSecure();
}

void utils::UpdateServerVersion()
{
	FileHandle_t fp = g_pFullFileSystem->Open("steam.inf", "r");
	if (fp)
	{
		std::string line = g_pFullFileSystem->ReadLine(fp).Get();
		while (!line.empty())
		{
			if (line.starts_with(SERVER_VERSION_KEY))
			{
				serverVersion = atoi(line.c_str() + strlen(SERVER_VERSION_KEY));
				break;
			}
			line = g_pFullFileSystem->ReadLine(fp).Get();
		}
		g_pFullFileSystem->Close(fp);
	}
}

u32 utils::GetServerVersion()
{
	return serverVersion;
}

bool utils::WriteBufferToFile(const char *relativePath, const std::vector<char> &buffer)
{
	char absPath[1024];
	V_snprintf(absPath, sizeof(absPath), "%s/csgo/%s", Plat_GetGameDirectory(), relativePath);

	// Ensure directory exists
	char dir[1024];
	V_ExtractFilePath(absPath, dir, sizeof(dir));
	if (dir[0])
	{
		std::filesystem::create_directories(dir);
	}

	char tmpPath[1024];
	V_snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", absPath);

	FILE *fp = fopen(tmpPath, "wb");
	if (!fp)
	{
		SURF_LOG_WARN(LogChannel::General, "Failed to open file for writing: %s\n", tmpPath);
		return false;
	}
	fwrite(buffer.data(), 1, buffer.size(), fp);
	fclose(fp);

#ifdef _WIN32
	if (!MoveFileExA(tmpPath, absPath, MOVEFILE_REPLACE_EXISTING))
#else
	if (rename(tmpPath, absPath) != 0)
#endif
	{
		remove(tmpPath);
		return false;
	}
	return true;
}

bool utils::ReadBufferFromFile(const char *relativePath, std::vector<char> &outBuffer)
{
	char absPath[1024];
	V_snprintf(absPath, sizeof(absPath), "%s/csgo/%s", Plat_GetGameDirectory(), relativePath);

	FILE *fp = fopen(absPath, "rb");
	if (!fp)
	{
		SURF_LOG_WARN(LogChannel::General, "Failed to open file for reading: %s\n", absPath);
		return false;
	}

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	outBuffer.resize(size);
	fread(outBuffer.data(), 1, size, fp);
	fclose(fp);
	return true;
}

void utils::RemoveFile(const char *relativePath)
{
	char absPath[1024];
	V_snprintf(absPath, sizeof(absPath), "%s/csgo/%s", Plat_GetGameDirectory(), relativePath);
	if (remove(absPath) != 0)
	{
		SURF_LOG_WARN(LogChannel::General, "Failed to remove %s\n", absPath);
	}
}

bool utils::RenameFile(const char *oldRelativePath, const char *newRelativePath)
{
	char absOld[1024], absNew[1024];
	V_snprintf(absOld, sizeof(absOld), "%s/csgo/%s", Plat_GetGameDirectory(), oldRelativePath);
	V_snprintf(absNew, sizeof(absNew), "%s/csgo/%s", Plat_GetGameDirectory(), newRelativePath);
#ifdef _WIN32
	if (!MoveFileExA(absOld, absNew, MOVEFILE_REPLACE_EXISTING))
#else
	if (rename(absOld, absNew) != 0)
#endif
	{
		SURF_LOG_WARN(LogChannel::General, "Failed to rename %s to %s\n", absOld, absNew);
		return false;
	}
	return true;
}

bool utils::ParseSteamID2(std::string_view steamID, u64 &out)
{
	if (steamID.size() <= 10)
	{
		return false;
	}

	// clang-format off

	out = 0b0000000100010000000000000000000100000000000000000000000000000000
		| (atoll(&steamID[10]) << 1)
		| atoll(&steamID[8]);

	// clang-format on

	return true;
}
