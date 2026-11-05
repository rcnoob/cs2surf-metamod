#include "utils.h"
#include "gameconfig.h"
#include "engine2/inetworkserverservice.h"
#include "engine2/networkgameserverbase.h"
#include "interfaces.h"
#include "cs2surf.h"
#include "ctimer.h"
#include "keyvalues3.h"
#include <filesystem/ifilesystem.h>
#include "checksum_md5.h"
#include "engine2/serversideclient.h"
#include "sdk/gamerules.h"

#include "memdbgon.h"

#define MAX_VELOCITY_KEY "sv_maxvelocity "

static_global char currentMapMD5[33];
static_global bool md5NeedsUpdating {};

extern CGameConfig *g_pGameConfig;

CGameConfig *SurfUtils::GetGameConfig()
{
	return g_pGameConfig;
}

const CGlobalVars *SurfUtils::GetServerGlobals()
{
	return g_SurfPlugin.simulatingPhysics ? &(g_SurfPlugin.serverGlobals) : this->GetGlobals();
}

CGlobalVars *SurfUtils::GetGlobals()
{
	INetworkGameServer *server = g_pNetworkServerService->GetIGameServer();

	if (!server)
	{
		return nullptr;
	}

	return server->GetGlobals();
}

CBaseEntity *SurfUtils::FindEntityByClassname(CEntityInstance *start, const char *name)
{
	return utils::FindEntityByClassname(start, name);
}

CBasePlayerController *SurfUtils::GetController(CBaseEntity *entity)
{
	return utils::GetController(entity);
}

CBasePlayerController *SurfUtils::GetController(CPlayerSlot slot)
{
	return utils::GetController(slot);
}

CPlayerSlot SurfUtils::GetEntityPlayerSlot(CBaseEntity *entity)
{
	return utils::GetEntityPlayerSlot(entity);
}

void SurfUtils::SendConVarValue(CPlayerSlot slot, ConVarRefAbstract conVar, const char *value)
{
	utils::SendConVarValue(slot, conVar, value);
}

void SurfUtils::SendMultipleConVarValues(CPlayerSlot slot, ConVarRefAbstract **conVars, const char **values, u32 size)
{
	utils::SendMultipleConVarValues(slot, conVars, values, size);
}

f32 SurfUtils::NormalizeDeg(f32 a)
{
	return utils::NormalizeDeg(a);
}

f32 SurfUtils::GetAngleDifference(const f32 source, const f32 target, const f32 c, bool relative)
{
	return utils::GetAngleDifference(source, target, c, relative);
}

CGameEntitySystem *SurfUtils::GetGameEntitySystem()
{
	return GameEntitySystem();
}

void SurfUtils::AddTimer(CTimerBase *timer, bool preserveMapChange)
{
	if (preserveMapChange)
	{
		g_PersistentTimers.AddToTail(timer);
	}
	else
	{
		g_NonPersistentTimers.AddToTail(timer);
	}
}

void SurfUtils::RemoveTimer(CTimerBase *timer)
{
	FOR_EACH_VEC(g_PersistentTimers, i)
	{
		if (g_PersistentTimers.Element(i) == timer)
		{
			g_PersistentTimers.Remove(i);
			return;
		}
	}
	FOR_EACH_VEC(g_NonPersistentTimers, i)
	{
		if (g_NonPersistentTimers.Element(i) == timer)
		{
			g_NonPersistentTimers.Remove(i);
			return;
		}
	}
}

CUtlVector<CServerSideClientBase *> *SurfUtils::GetClientList()
{
	// There is no game server before the first map loads.
	if (!g_pNetworkServerService || !g_pNetworkServerService->GetIGameServer())
	{
		return nullptr;
	}
	return &static_cast<CNetworkGameServerBase *>(g_pNetworkServerService->GetIGameServer())->m_Clients;
}

// The engine keeps the client object in its slot after a disconnect, so only hand it out while someone occupies the slot.
CServerSideClient *SurfUtils::GetClientBySlot(CPlayerSlot slot)
{
	CUtlVector<CServerSideClientBase *> *clients = this->GetClientList();
	if (!clients || slot.Get() < 0 || slot.Get() >= clients->Count())
	{
		return nullptr;
	}
	CServerSideClient *client = static_cast<CServerSideClient *>(clients->Element(slot.Get()));
	return client && client->IsConnected() ? client : nullptr;
}

std::string SurfUtils::GetCurrentMapName(bool *result)
{
	CNetworkGameServerBase *networkGameServer = (CNetworkGameServerBase *)g_pNetworkServerService->GetIGameServer();
	if (networkGameServer)
	{
		if (result)
		{
			*result = true;
		}
		return networkGameServer->GetMapName();
	}

	auto currentMap = g_SurfPlugin.GetCurrentMap();
	if (currentMap.length() > 0)
	{
		if (result)
		{
			*result = true;
		}
		return currentMap.data();
	}

	if (result)
	{
		*result = false;
	}
	return "";
}

u64 SurfUtils::GetCurrentMapWorkshopID()
{
	std::string directory = this->GetCurrentMapDirectory();
	if (directory.find("*workshop") != std::string::npos)
	{
		return atoll(V_UnqualifiedFileName(directory.c_str()));
	}
	return 0;
}

std::string SurfUtils::GetCurrentMapVPK()
{
	std::string map = this->GetCurrentMapName();
	if (map.empty())
	{
		return "";
	}
	CUtlVector<CUtlString> paths;
	char mapName[1024];

	g_SMAPI->PathFormat(mapName, sizeof(mapName), "maps/%s.vpk", map.c_str());

	g_pFullFileSystem->FindFileAbsoluteList(paths, mapName, "GAME");
	if (paths.Count() > 0)
	{
		std::string realPath = paths[0].Get();
		if (realPath.starts_with("vpk:"))
		{
			realPath = realPath.substr(sizeof("vpk:") - 1);
			return utils::StripExtension(utils::StripFilename(realPath)) + ".vpk";
		}
		return realPath;
	}
	return "";
}

std::string SurfUtils::GetCurrentMapDirectory()
{
	return utils::DirName(this->GetCurrentMapVPK());
}

u64 SurfUtils::GetCurrentMapSize()
{
	std::string currentMap = this->GetCurrentMapVPK();
	u32 size = 0;
	if (g_pFullFileSystem->FileExists(currentMap.c_str()))
	{
		size = g_pFullFileSystem->Size(currentMap.c_str());
	}
	else
	{
		std::string newFileName = utils::StripExtension(currentMap) + "_dir.vpk";
		size = g_pFullFileSystem->Size(newFileName.c_str());
		for (u32 i = 0; g_pFullFileSystem->FileExists(newFileName.c_str()); i++)
		{
			newFileName = tfm::format("%s_%03i.vpk", utils::StripExtension(currentMap), i);
			size += g_pFullFileSystem->Size(newFileName.c_str());
		}
	}
	return size;
}

std::string SurfUtils::GetCurrentMapConfig()
{
	std::string map = this->GetCurrentMapName();
	if (map.empty())
	{
		return "";
	}
	CUtlVector<CUtlString> paths;
	char mapName[1024];

	g_SMAPI->PathFormat(mapName, sizeof(mapName), "cfg/maps/%s.cfg", map.c_str());

	g_pFullFileSystem->FindFileAbsoluteList(paths, mapName, "GAME");

	if (paths.Count() > 0)
	{
		return paths[0].Get();
	}
	return "";
}

u32 SurfUtils::GetCurrentMapMaxVelocity()
{
	std::string configPath = this->GetCurrentMapConfig();
	if (configPath.empty())
	{
		return 0;
	}

	FileHandle_t fp = g_pFullFileSystem->Open(configPath.c_str(), "r");
	u32 maxVel = 0;
	if (fp)
	{
		int size = g_pFullFileSystem->Size(fp);
		std::string buffer(size, '\0');

		int bytesRead = g_pFullFileSystem->Read(buffer.data(), size, fp);
		g_pFullFileSystem->Close(fp);
		buffer.resize(bytesRead > 0 ? bytesRead : 0);

		std::istringstream stream(buffer);
		std::string line;
		const std::string_view key = MAX_VELOCITY_KEY;

		while (std::getline(stream, line))
		{
			// strip windows line endings
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}

			if (line.starts_with(key) && utils::IsNumeric(line.c_str() + key.size()))
			{
				maxVel = atoi(line.c_str() + key.size());
				break;
			}
		}
	}
	return maxVel;
}

bool SurfUtils::UpdateCurrentMapMD5()
{
	return this->GetFileMD5(this->GetCurrentMapVPK().c_str(), currentMapMD5, sizeof(currentMapMD5));
}

bool SurfUtils::GetCurrentMapMD5(char *buffer, i32 size)
{
	if (md5NeedsUpdating)
	{
		md5NeedsUpdating = this->UpdateCurrentMapMD5();
	}
	if (!md5NeedsUpdating)
	{
		V_strncpy(buffer, currentMapMD5, size);
		return true;
	}
	return false;
}

bool SurfUtils::GetFileMD5(const char *filePath, char *buffer, i32 size)
{
	u8 chunk[8192];
	FileHandle_t file = g_pFullFileSystem->OpenEx(filePath, "rb");
	if (file)
	{
		i64 sizeRemaining = g_pFullFileSystem->Size(file);
		i32 bytesRead;
		MD5Context_t ctx;
		unsigned char digest[MD5_DIGEST_LENGTH];
		memset(&ctx, 0, sizeof(MD5Context_t));

		MD5Init(&ctx);
		while (sizeRemaining > 0)
		{
			bytesRead = g_pFullFileSystem->Read(chunk, sizeRemaining > sizeof(chunk) ? sizeof(chunk) : sizeRemaining, file);
			sizeRemaining -= bytesRead;
			if (bytesRead > 0)
			{
				MD5Update(&ctx, chunk, bytesRead);
			}
			if (g_pFullFileSystem->EndOfFile(file))
			{
				g_pFullFileSystem->Close(file);
				file = NULL;
				break;
			}
			else if (!g_pFullFileSystem->IsOk(file))
			{
				if (file)
				{
					g_pFullFileSystem->Close(file);
				}
				return false;
			}
		}

		MD5Final(digest, &ctx);
		char *data = MD5_Print(digest, sizeof(digest));
		V_strncpy(buffer, data, size);
		if (file)
		{
			g_pFullFileSystem->Close(file);
		}
		return true;
	}

	// Try and get the MD5 for multifile vpks.
	if (V_strlen(filePath) >= 4 && SURF_STREQI(filePath + V_strlen(filePath) - 4, ".vpk"))
	{
		i32 index = 0;
		std::string originalPath = filePath;
		std::string newFile = utils::StripExtension(originalPath) + "_dir.vpk";
		file = g_pFullFileSystem->OpenEx(newFile.c_str(), "rb");
		if (file)
		{
			i64 sizeRemaining = g_pFullFileSystem->Size(file);
			i32 bytesRead;
			MD5Context_t ctx;
			unsigned char digest[MD5_DIGEST_LENGTH];
			memset(&ctx, 0, sizeof(MD5Context_t));

			MD5Init(&ctx);
			while (sizeRemaining > 0)
			{
				bytesRead = g_pFullFileSystem->Read(chunk, sizeRemaining > sizeof(chunk) ? sizeof(chunk) : sizeRemaining, file);
				sizeRemaining -= bytesRead;
				if (bytesRead > 0)
				{
					MD5Update(&ctx, chunk, bytesRead);
				}
				if (g_pFullFileSystem->EndOfFile(file))
				{
					g_pFullFileSystem->Close(file);
					newFile = tfm::format("%s_%03i.vpk", utils::StripExtension(originalPath), index);
					file = g_pFullFileSystem->OpenEx(newFile.c_str(), "rb");
					if (!file)
					{
						break;
					}
					sizeRemaining = g_pFullFileSystem->Size(file);
					index++;
				}
				else if (!g_pFullFileSystem->IsOk(file))
				{
					if (file)
					{
						g_pFullFileSystem->Close(file);
					}
					return false;
				}
			}

			MD5Final(digest, &ctx);
			char *data = MD5_Print(digest, sizeof(digest));
			V_strncpy(buffer, data, size);
			if (file)
			{
				g_pFullFileSystem->Close(file);
			}
			return true;
		}
		return false;
	}
	return false;
}

CCSGameRules *SurfUtils::GetGameRules()
{
	CCSGameRulesProxy *proxy = NULL;
	proxy = (CCSGameRulesProxy *)utils::FindEntityByClassname(proxy, "cs_gamerules");
	if (proxy)
	{
		return proxy->m_pGameRules();
	}
	return nullptr;
}

u32 SurfUtils::GetPlayerCount()
{
	u32 count = 0;
	auto clients = this->GetClientList();
	if (clients)
	{
		FOR_EACH_VEC(*clients, i)
		{
			CServerSideClientBase *client = clients->Element(i);

			if (client && client->IsConnected() && !client->IsFakeClient() && !client->IsHLTV())
			{
				count++;
			}
		}
	}
	return count;
}

void SurfUtils::AddTriangleOverlay(Vector const &p1, Vector const &p2, Vector const &p3, u8 r, u8 g, u8 b, u8 a, bool noDepthTest, f64 flDuration)
{
	void *debugoverlay = interfaces::pServer->GetDebugOverlays();
	if (debugoverlay)
	{
		CALL_VIRTUAL(void, g_pGameConfig->GetOffset("DebugTriangle"), debugoverlay, &p1, &p2, &p3, r, g, b, a, noDepthTest, flDuration);
	}
}

void SurfUtils::ClearOverlays()
{
	void *debugoverlay = interfaces::pServer->GetDebugOverlays();
	if (debugoverlay)
	{
		CALL_VIRTUAL(void, g_pGameConfig->GetOffset("ClearOverlays"), debugoverlay);
	}
}
