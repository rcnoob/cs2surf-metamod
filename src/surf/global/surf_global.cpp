// required for ws library
#ifdef _WIN32
#pragma comment(lib, "Ws2_32.Lib")
#pragma comment(lib, "Crypt32.Lib")
#endif

#include <string_view>

#include <ixwebsocket/IXNetSystem.h>

#include "common.h"
#include "cs2surf.h"
#include "surf/surf.h"
#include "surf_global.h"
#include "surf/global/handshake.h"
#include "surf/global/events.h"
#include "surf/mode/surf_mode.h"
#include "surf/option/surf_option.h"
#include "surf/timer/surf_timer.h"

bool SurfGlobalService::IsAvailable()
{
	return SurfGlobalService::state.load() == SurfGlobalService::State::HandshakeCompleted;
}

bool SurfGlobalService::MayBecomeAvailable()
{
	return SurfGlobalService::state.load() != SurfGlobalService::State::Disconnected;
}

void SurfGlobalService::UpdateRecordCache()
{
	u16 currentMapID = 0;

	{
		std::unique_lock lock(SurfGlobalService::currentMap.mutex);

		if (!SurfGlobalService::currentMap.data.has_value())
		{
			return;
		}

		currentMapID = SurfGlobalService::currentMap.data->id;
	}

	std::string_view event("want-world-records-for-cache");
	Surf::API::events::WantWorldRecordsForCache data {currentMapID};
	auto callback = [](Surf::API::events::WorldRecordsForCache &records)
	{
		for (const Surf::API::Record &record : records.records)
		{
			const SurfCourseDescriptor *course = Surf::course::GetCourseByGlobalCourseID(record.course.id);

			if (course == nullptr)
			{
				SURF_LOG_WARN(LogChannel::Global, "Could not find current course?\n");
				continue;
			}

			PluginId modeID = Surf::mode::GetModeInfo(record.mode).id;

			SurfTimerService::InsertRecordToCache(record.time, course, modeID, true);
		}
	};

	switch (SurfGlobalService::state.load())
	{
		case SurfGlobalService::State::HandshakeCompleted:
			SurfGlobalService::SendMessage(event, data, callback);
			break;

		case SurfGlobalService::State::Disconnected:
			break;

		default:
			SurfGlobalService::AddWhenConnectedCallback([=]() { SurfGlobalService::SendMessage(event, data, callback); });
	}
}

void SurfGlobalService::Init()
{
	if (SurfGlobalService::state.load() != SurfGlobalService::State::Uninitialized)
	{
		return;
	}

	SURF_LOG_INFO(LogChannel::Global, "Initializing GlobalService...\n");

	std::string url = SurfOptionService::GetOptionStr("apiUrl", SurfOptionService::GetOptionStr("apiUrl", "https://api.placeholder.org"));
	std::string_view key = SurfOptionService::GetOptionStr("apiKey");

	if (url.empty())
	{
		SURF_LOG_INFO(LogChannel::Global, "`apiUrl` is empty. GlobalService will be disabled.\n");
		SurfGlobalService::state.store(SurfGlobalService::State::Disconnected);
		return;
	}

	if (url.size() < 4 || url.substr(0, 4) != "http")
	{
		SURF_LOG_INFO(LogChannel::Global, "`apiUrl` is invalid. GlobalService will be disabled.\n");
		SurfGlobalService::state.store(SurfGlobalService::State::Disconnected);
		return;
	}

	if (key.empty())
	{
		SURF_LOG_INFO(LogChannel::Global, "`apiKey` is empty. GlobalService will be disabled.\n");
		SurfGlobalService::state.store(SurfGlobalService::State::Disconnected);
		return;
	}

	url.replace(0, 4, "ws");

	if (url.size() < 9 || url.substr(url.size() - 9) != "/auth/cs2")
	{
		if (url.substr(url.size() - 1) != "/")
		{
			url += "/";
		}

		url += "auth/cs2";
	}

	ix::initNetSystem();

	SurfGlobalService::socket = new ix::WebSocket();
	SurfGlobalService::socket->setUrl(url);

	// ix::WebSocketHttpHeaders headers;
	// headers["Authorization"] = "Bearer ";
	// headers["Authorization"] += key;
	SurfGlobalService::socket->setExtraHeaders({
		{"Authorization", std::string("Bearer ") + key.data()},
	});

	SurfGlobalService::socket->setOnMessageCallback(SurfGlobalService::OnWebSocketMessage);
	SurfGlobalService::socket->start();

	SurfGlobalService::EnforceConVars();

	SurfGlobalService::state.store(SurfGlobalService::State::Initialized);
}

void SurfGlobalService::Cleanup()
{
	if (SurfGlobalService::state.load() == SurfGlobalService::State::Uninitialized)
	{
		return;
	}

	SURF_LOG_INFO(LogChannel::Global, "Cleaning up GlobalService...\n");

	SurfGlobalService::RestoreConVars();

	if (SurfGlobalService::socket != nullptr)
	{
		SurfGlobalService::socket->stop();
		delete SurfGlobalService::socket;
		SurfGlobalService::socket = nullptr;
	}

	SurfGlobalService::state.store(SurfGlobalService::State::Uninitialized);

	ix::uninitNetSystem();
}

void SurfGlobalService::OnServerGamePostSimulate()
{
	std::vector<std::function<void()>> callbacks;

	{
		std::unique_lock lock(SurfGlobalService::mainThreadCallbacks.mutex, std::defer_lock);

		if (lock.try_lock())
		{
			SurfGlobalService::mainThreadCallbacks.queue.swap(callbacks);

			if (SurfGlobalService::state.load() == SurfGlobalService::State::HandshakeCompleted)
			{
				callbacks.reserve(callbacks.size() + SurfGlobalService::mainThreadCallbacks.whenConnectedQueue.size());
				callbacks.insert(callbacks.end(), SurfGlobalService::mainThreadCallbacks.whenConnectedQueue.begin(),
								 SurfGlobalService::mainThreadCallbacks.whenConnectedQueue.end());
				SurfGlobalService::mainThreadCallbacks.whenConnectedQueue.clear();
			}
		}
	}

	for (const std::function<void()> &callback : callbacks)
	{
		callback();
	}
}

void SurfGlobalService::OnActivateServer()
{
	switch (SurfGlobalService::state.load())
	{
		case SurfGlobalService::State::Uninitialized:
			SurfGlobalService::Init();
			break;

		case SurfGlobalService::State::HandshakeCompleted:
		{
			bool mapNameOk = false;
			std::string currentMapName = g_pSurfUtils->GetCurrentMapName(&mapNameOk);

			if (!mapNameOk)
			{
				SURF_LOG_INFO(LogChannel::Global, "Failed to get current map name. Cannot send `map-change` event.\n");
				return;
			}

			std::string_view event("map-change");
			Surf::API::events::MapChange data(currentMapName.c_str());

			// clang-format off
			SurfGlobalService::SendMessage(event, data, [currentMapName](Surf::API::events::MapInfo& mapInfo)
			{
				if (mapInfo.data.has_value())
				{
					SURF_LOG_INFO(LogChannel::Global, "%s is approved.\n", mapInfo.data->name.c_str());
				}
				else
				{
					SURF_LOG_INFO(LogChannel::Global, "%s is not approved.\n", currentMapName.c_str());
				}

				{
					std::unique_lock lock(SurfGlobalService::currentMap.mutex);
					SurfGlobalService::currentMap.data = std::move(mapInfo.data);
				}
			});
			// clang-format on
		}
		break;
	}
}

void SurfGlobalService::OnPlayerAuthorized()
{
	if (!this->player->IsConnected())
	{
		return;
	}

	u64 steamID = this->player->GetSteamId64();

	std::string_view event("player-join");
	Surf::API::events::PlayerJoin data;
	data.steamID = steamID;
	data.name = this->player->GetName();
	data.ipAddress = this->player->GetIpAddress();

	auto callback = [steamID](Surf::API::events::PlayerJoinAck &ack)
	{
		SurfPlayer *player = g_pSurfPlayerManager->SteamIdToPlayer(steamID);

		if (player == nullptr)
		{
			return;
		}

		player->globalService->playerInfo.isBanned = ack.isBanned;
		player->optionService->InitializeGlobalPrefs(ack.preferences.ToString());

		// clang-format off
		u16 currentMapID = SurfGlobalService::WithCurrentMap([](const Surf::API::Map* currentMap)
		{
			return (currentMap == nullptr) ? 0 : currentMap->id;
		});
		// clang-format on

		if (currentMapID != 0)
		{
			std::string_view event("want-player-records");
			Surf::API::events::WantPlayerRecords data;
			data.mapID = currentMapID;
			data.playerID = steamID;

			auto callback = [steamID](Surf::API::events::PlayerRecords &records)
			{
				SurfPlayer *player = g_pSurfPlayerManager->SteamIdToPlayer(steamID);

				if (player == nullptr)
				{
					return;
				}

				for (const Surf::API::Record &record : records.records)
				{
					const SurfCourseDescriptor *course = Surf::course::GetCourseByGlobalCourseID(record.course.id);

					if (course == nullptr)
					{
						continue;
					}

					PluginId modeID = Surf::mode::GetModeInfo(record.mode).id;

					if (record.points != 0)
					{
						player->timerService->InsertPBToCache(record.time, course, modeID, true, "", record.points);
					}
				}
			};

			switch (SurfGlobalService::state.load())
			{
				case SurfGlobalService::State::HandshakeInitiated:
					SurfGlobalService::AddWhenConnectedCallback([=]() { SurfGlobalService::SendMessage(event, data, callback); });
					break;

				case SurfGlobalService::State::HandshakeCompleted:
					SurfGlobalService::SendMessage(event, data, callback);
					break;

				case SurfGlobalService::State::Disconnected:
					META_CONPRINTF("[Surf::Global] Cannot fetch player PBs if we are disconnected from the API. (state=%i)\n");
					break;

				default:
					// handshake hasn't been initiated yet, so by the time that
					// happens, player will be sent as part of the handshake
					break;
			}
		}
	};

	switch (SurfGlobalService::state.load())
	{
		case SurfGlobalService::State::HandshakeInitiated:
			SurfGlobalService::AddWhenConnectedCallback([=]() { SurfGlobalService::SendMessage(event, data, callback); });
			break;

		case SurfGlobalService::State::HandshakeCompleted:
			SurfGlobalService::SendMessage(event, data, callback);
			break;

		case SurfGlobalService::State::Disconnected:
			break;

		default:
			// handshake hasn't been initiated yet, so by the time that
			// happens, player will be sent as part of the handshake
			break;
	}
}

void SurfGlobalService::OnClientDisconnect()
{
	if (!this->player->IsConnected() || !this->player->IsAuthenticated())
	{
		return;
	}

	if (SurfGlobalService::state.load() != SurfGlobalService::State::HandshakeCompleted)
	{
		return;
	}

	std::string getPrefsError;
	std::string getPrefsResult;
	this->player->optionService->GetPreferencesAsJSON(&getPrefsError, &getPrefsResult);

	if (!getPrefsError.empty())
	{
		SURF_LOG_WARN(LogChannel::Global, "Failed to get preferences: %s\n", getPrefsError.c_str());
		SURF_LOG_INFO(LogChannel::Global, "Not sending `player-leave` event.\n");
		return;
	}

	std::string_view event("player-leave");
	Surf::API::events::PlayerLeave data;
	data.steamID = this->player->GetSteamId64();
	data.name = this->player->GetName();
	data.preferences = Json(getPrefsResult.c_str());

	switch (SurfGlobalService::state.load())
	{
		case SurfGlobalService::State::HandshakeCompleted:
			SurfGlobalService::SendMessage(event, data);
			break;

		default:
			break;
	}
}

void SurfGlobalService::OnWebSocketMessage(const ix::WebSocketMessagePtr &message)
{
	switch (message->type)
	{
		case ix::WebSocketMessageType::Open:
		{
			SURF_LOG_INFO(LogChannel::Global, "Connection established!\n");
			SurfGlobalService::state.store(SurfGlobalService::State::Connected);
			SurfGlobalService::AddMainThreadCallback(SurfGlobalService::InitiateHandshake);
		}
		break;

		case ix::WebSocketMessageType::Close:
		{
			SURF_LOG_INFO(LogChannel::Global, "Connection closed (code %i): %s\n", message->closeInfo.code, message->closeInfo.reason.c_str());

			switch (message->closeInfo.code)
			{
				case 1000 /* NORMAL */:     /* fall-through */
				case 1001 /* GOING AWAY */: /* fall-through */
				case 1006 /* ABNORMAL */:
				{
					SurfGlobalService::socket->enableAutomaticReconnection();
					SurfGlobalService::state.store(SurfGlobalService::State::DisconnectedButWorthRetrying);
				}
				break;

				case 1008 /* POLICY VIOLATION */:
				{
					if (SurfGlobalService::state.load() == SurfGlobalService::State::HandshakeCompleted
						&& message->closeInfo.reason.find("heartbeat") != message->closeInfo.reason.size())
					{
						SurfGlobalService::socket->enableAutomaticReconnection();
						SurfGlobalService::state.store(SurfGlobalService::State::DisconnectedButWorthRetrying);
					}
					else
					{
						SurfGlobalService::socket->disableAutomaticReconnection();
						SurfGlobalService::state.store(SurfGlobalService::State::Disconnected);
					}
				}
				break;

				default:
				{
					SurfGlobalService::socket->disableAutomaticReconnection();
					SurfGlobalService::state.store(SurfGlobalService::State::Disconnected);
				}
			}
		}
		break;

		case ix::WebSocketMessageType::Error:
		{
			SURF_LOG_WARN(LogChannel::Global, "WebSocket error (code %i): %s\n", message->errorInfo.http_status, message->errorInfo.reason.c_str());

			switch (message->errorInfo.http_status)
			{
				case 401:
				{
					SurfGlobalService::socket->disableAutomaticReconnection();
					SurfGlobalService::state.store(SurfGlobalService::State::Disconnected);
				}
				break;

				case 429:
				{
					SURF_LOG_WARN(LogChannel::Global, "API rate limit reached; increasing down reconnection delay...\n");
					SurfGlobalService::socket->enableAutomaticReconnection();
					SurfGlobalService::socket->setMinWaitBetweenReconnectionRetries(10'000 /* ms */);
					SurfGlobalService::socket->setMaxWaitBetweenReconnectionRetries(30'000 /* ms */);
					SurfGlobalService::state.store(SurfGlobalService::State::DisconnectedButWorthRetrying);
				}
				break;

				case 500: /* fall-through */
				case 502:
				{
					SURF_LOG_WARN(LogChannel::Global, "API encountered an internal error\n");
					SurfGlobalService::socket->enableAutomaticReconnection();
					SurfGlobalService::socket->setMinWaitBetweenReconnectionRetries(10 * 60'000 /* ms */);
					SurfGlobalService::socket->setMaxWaitBetweenReconnectionRetries(30 * 60'000 /* ms */);
					SurfGlobalService::state.store(SurfGlobalService::State::DisconnectedButWorthRetrying);
				}
				break;

				default:
				{
					SurfGlobalService::socket->disableAutomaticReconnection();
					SurfGlobalService::state.store(SurfGlobalService::State::Disconnected);
				}
			}
		}
		break;

		case ix::WebSocketMessageType::Message:
		{
			SURF_LOG_INFO(LogChannel::Global, "Received WebSocket message:\n-----\n%s\n------\n", message->str.c_str());

			Json payload(message->str);

			switch (SurfGlobalService::state.load())
			{
				case SurfGlobalService::State::HandshakeInitiated:
				{
					Surf::API::handshake::HelloAck helloAck;

					if (!helloAck.FromJson(payload))
					{
						SURF_LOG_WARN(LogChannel::Global, "Failed to decode 'HelloAck'\n");
						break;
					}

					SurfGlobalService::AddMainThreadCallback([ack = std::move(helloAck)]() mutable { SurfGlobalService::CompleteHandshake(ack); });
				}
				break;

				case SurfGlobalService::State::HandshakeCompleted:
				{
					if (!payload.IsValid())
					{
						SURF_LOG_WARN(LogChannel::Global, "WebSocket message is not valid JSON.\n");
						break;
					}

					u32 messageID = 0;

					if (!payload.Get("id", messageID))
					{
						SURF_LOG_WARN(LogChannel::Global, "Ignoring message without valid ID\n");
						break;
					}

					SurfGlobalService::AddMainThreadCallback([=]() { SurfGlobalService::ExecuteMessageCallback(messageID, payload); });
				}
				break;
			}
		}
		break;

		case ix::WebSocketMessageType::Ping:
		{
			SURF_LOG_INFO(LogChannel::Global, "Ping!\n");
		}
		break;

		case ix::WebSocketMessageType::Pong:
		{
			SURF_LOG_INFO(LogChannel::Global, "Pong!\n");
		}
		break;
	}
}

void SurfGlobalService::InitiateHandshake()
{
	bool mapNameOk = false;
	std::string currentMapName = g_pSurfUtils->GetCurrentMapName(&mapNameOk);

	if (!mapNameOk)
	{
		SURF_LOG_WARN(LogChannel::Global, "Failed to get current map name. Cannot initiate handshake.\n");
		return;
	}

	std::string_view event("hello");
	Surf::API::handshake::Hello data(g_SurfPlugin.GetMD5(), currentMapName.c_str());

	for (Player *player : g_pPlayerManager->players)
	{
		if (player && player->IsAuthenticated())
		{
			data.AddPlayer(player->GetSteamId64(), player->GetName());
		}
	}

	SurfGlobalService::SendMessage(event, data);
	SurfGlobalService::state.store(State::HandshakeInitiated);
}

void SurfGlobalService::CompleteHandshake(Surf::API::handshake::HelloAck &ack)
{
	SurfGlobalService::state.store(State::HandshakeCompleted);

	// clang-format off
	std::thread([heartbeatInterval = std::chrono::milliseconds(static_cast<i64>(ack.heartbeatInterval * 800))]()
	{
		while (SurfGlobalService::state.load() == State::HandshakeCompleted) {
			SurfGlobalService::socket->ping("");
			SURF_LOG_INFO(LogChannel::Global, "Sent heartbeat. (interval=%lu)\n", std::chrono::duration_cast<std::chrono::seconds>(heartbeatInterval).count());
			std::this_thread::sleep_for(heartbeatInterval);
		}
	}).detach();
	// clang-format on

	{
		std::unique_lock lock(SurfGlobalService::currentMap.mutex);
		SurfGlobalService::currentMap.data = std::move(ack.mapInfo);
	}

	{
		std::unique_lock lock(SurfGlobalService::globalModes.mutex);
		SurfGlobalService::globalModes.data = std::move(ack.modes);
	}

	{
		std::unique_lock lock(SurfGlobalService::globalStyles.mutex);
		SurfGlobalService::globalStyles.data = std::move(ack.styles);
	}

	SURF_LOG_INFO(LogChannel::Global, "Completed handshake!\n");
}

void SurfGlobalService::ExecuteMessageCallback(u32 messageID, const Json &payload)
{
	std::function<void(u32, const Json &)> callback;

	{
		std::unique_lock lock(SurfGlobalService::messageCallbacks.mutex);
		std::unordered_map<u32, std::function<void(u32, const Json &)>> &callbacks = SurfGlobalService::messageCallbacks.queue;

		if (auto found = callbacks.extract(messageID); !found.empty())
		{
			callback = found.mapped();
		}
	}

	if (callback)
	{
		SURF_LOG_INFO(LogChannel::Global, "Executing callback #%i\n", messageID);
		callback(messageID, payload);
	}
}
