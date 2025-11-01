#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>

#include <vendor/ixwebsocket/ixwebsocket/IXWebSocket.h>

#include "utils/json.h"

#include "surf/surf.h"
#include "surf/global/api.h"
#include "surf/global/handshake.h"
#include "surf/global/events.h"
#include "surf/timer/announce.h"

class SurfGlobalService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

public:
	/**
	 * Returns whether the global service is "available".
	 *
	 * That is, whether it makes sense to call any of the other functions that
	 * need an established connection to the API.
	 */
	static bool IsAvailable();

	/**
	 * Returns whether the global service might become "available" in the future.
	 */
	static bool MayBecomeAvailable();

	/**
	 * Executes a function with information about the current map.
	 *
	 * `F` should be a function that accepts a single argument of type `const Surf::API::Map*`.
	 */
	template<typename F>
	static auto WithCurrentMap(F &&f)
	{
		std::unique_lock lock(SurfGlobalService::currentMap.mutex);
		const Surf::API::Map *currentMap = nullptr;

		if (SurfGlobalService::currentMap.data.has_value())
		{
			currentMap = &*SurfGlobalService::currentMap.data;
		}

		return f(currentMap);
	}

	/**
	 * Executes a function with a list of `ModeInfo` structs we got from the API.
	 *
	 * `F` should be a function that accepts a single argument of type
	 * `const std::vector<Surf::API::handshake::HelloAck::ModeInfo>&`.
	 */
	template<typename F>
	static auto WithGlobalModes(F &&f)
	{
		std::unique_lock lock(SurfGlobalService::globalModes.mutex);
		const std::vector<Surf::API::handshake::HelloAck::ModeInfo> &modes = SurfGlobalService::globalModes.data;
		return f(modes);
	}

	/**
	 * Executes a function with a list of `StyleInfo` structs we got from the API.
	 *
	 * `F` should be a function that accepts a single argument of type
	 * `const std::vector<Surf::API::handshake::HelloAck::StyleInfo>&`.
	 */
	template<typename F>
	static auto WithGlobalStyles(F &&f)
	{
		std::unique_lock lock(SurfGlobalService::globalStyles.mutex);
		const std::vector<Surf::API::handshake::HelloAck::StyleInfo> &styles = SurfGlobalService::globalStyles.data;
		return f(styles);
	}

	/**
	 * Updates the cached world records for the current map.
	 */
	static void UpdateRecordCache();

public:
	static void Init();
	static void Cleanup();

	static void OnServerGamePostSimulate();
	static void OnActivateServer();

public:
	void OnPlayerAuthorized();
	void OnClientDisconnect();

	/**
	 * Information about the current player we received from the API when the player connected.
	 */
	struct
	{
		/**
		 * Whether the player is globally banned.
		 */
		bool isBanned {};
	} playerInfo {};

	enum class SubmitRecordResult
	{
		/**
		 * We are not connected to the API and also won't connect later.
		 */
		NotConnected,

		/**
		 * The player is not authenticated / does not have prime status.
		 */
		PlayerNotAuthenticated,

		/**
		 * The current map is not global.
		 */
		MapNotGlobal,

		/**
		 * We are not currently connected to the API, but we queued up the record to be submitted later (if possible).
		 */
		Queued,

		/**
		 * The record was submitted.
		 */
		Submitted,
	};

	/**
	 * Submits a new record to the API.
	 */
	template<typename CB>
	SubmitRecordResult SubmitRecord(u16 filterID, f64 time, std::string_view modeMD5, void *styles, std::string_view metadata, CB &&cb)
	{
		if (!this->player->IsAuthenticated() && !this->player->hasPrime)
		{
			return SubmitRecordResult::PlayerNotAuthenticated;
		}

		{
			std::unique_lock currentMapLock(SurfGlobalService::currentMap.mutex);
			if (!SurfGlobalService::currentMap.data.has_value())
			{
				META_CONPRINTF("[Surf::Global] Cannot submit record on non-global map.\n");
				return SubmitRecordResult::MapNotGlobal;
			}
		}

		Surf::API::events::NewRecord data;
		data.playerID = this->player->GetSteamId64();
		data.filterID = filterID;
		data.modeChecksum = modeMD5;
		for (const auto &style : *(std::vector<RecordAnnounce::StyleInfo> *)styles)
		{
			data.styles.push_back({style.name, style.md5});
		}
		data.time = time;
		data.metadata = metadata;

		switch (SurfGlobalService::state.load())
		{
			case SurfGlobalService::State::HandshakeCompleted:
				SurfGlobalService::SendMessage("new-record", data, cb);
				return SubmitRecordResult::Submitted;

			case SurfGlobalService::State::Disconnected:
				return SubmitRecordResult::NotConnected;

			default:
				SurfGlobalService::AddMainThreadCallback([=]() { SurfGlobalService::SendMessage("new-record", data, cb); });
				return SubmitRecordResult::Queued;
		}
	}

	/**
	 * Query the personal best of a player on a certain map, course, mode, style.
	 */
	template<typename CB>
	static bool QueryPB(u64 steamid64, std::string_view targetPlayerName, std::string_view mapName, std::string_view courseNameOrNumber,
						Surf::API::Mode mode, const CUtlVector<CUtlString> &styleNames, CB &&cb)
	{
		if (!SurfGlobalService::IsAvailable())
		{
			return false;
		}

		std::string_view event("want-personal-best");
		Surf::API::events::WantPersonalBest data = {steamid64, targetPlayerName, mapName, courseNameOrNumber, mode};
		FOR_EACH_VEC(styleNames, i)
		{
			data.styles.emplace_back(styleNames[i].Get());
		}
		return SurfGlobalService::SendMessage(event, data, std::move(cb));
	}

	/**
	 * Query the course top on a certain map, mode with a certain limit and offset.
	 */
	template<typename CB>
	static bool QueryCourseTop(std::string_view mapName, std::string_view courseNameOrNumber, Surf::API::Mode mode, u32 limit, u32 offset, CB &&cb)
	{
		if (!SurfGlobalService::IsAvailable())
		{
			return false;
		}

		std::string_view event("want-course-top");
		Surf::API::events::WantCourseTop data = {mapName, courseNameOrNumber, mode, limit, offset};
		return SurfGlobalService::SendMessage(event, data, std::move(cb));
	}

	/**
	 * Query the world record of a course on a certain mode.
	 */
	template<typename CB>
	static bool QueryWorldRecords(std::string_view mapName, std::string_view courseNameOrNumber, Surf::API::Mode mode, CB &&cb)
	{
		if (!SurfGlobalService::IsAvailable())
		{
			return false;
		}

		std::string_view event("want-world-records");
		Surf::API::events::WantWorldRecords data = {mapName, courseNameOrNumber, mode};
		return SurfGlobalService::SendMessage(event, data, std::move(cb));
	}

private:
	enum class State
	{
		/**
		 * The default state
		 */
		Uninitialized,

		/**
		 * After `Init()` has returned
		 */
		Initialized,

		/**
		 * After we receive an "open" message on the WS thread
		 */
		Connected,

		/**
		 * After we sent the 'Hello' message
		 */
		HandshakeInitiated,

		/**
		 * After we received the 'HelloAck' message
		 */
		HandshakeCompleted,

		/**
		 * After the server closed the connection for a reason that won't change
		 */
		Disconnected,

		/**
		 * After the server closed the connection, but it's still worth to retry the connection
		 */
		DisconnectedButWorthRetrying,
	};

	/**
	 * The current connection state
	 */
	static inline std::atomic<State> state = State::Uninitialized;

	static inline struct
	{
		std::mutex mutex;

		/**
		 * Callbacks to execute on the main thread as soon as possible
		 */
		std::vector<std::function<void()>> queue;

		/**
		 * Callbacks to execute on the main thread as soon as we are fully connected to the API
		 */
		std::vector<std::function<void()>> whenConnectedQueue;
	} mainThreadCallbacks {};

	// invariant: should be `nullptr` if `state == Uninitialized` and otherwise a valid pointer
	static inline ix::WebSocket *socket = nullptr;

	/**
	 * The ID we'll use for the next message we send to the API.
	 */
	static inline std::atomic<u32> nextMessageID = 1;

	/**
	 * Callbacks to execute when we receive responses to messages we sent earlier.
	 *
	 * The key is the message ID we're looking for, and the callback will be
	 * invoked with that message ID and the payload.
	 */
	static inline struct
	{
		std::mutex mutex;
		std::unordered_map<u32, std::function<void(u32, const Json &)>> queue;
	} messageCallbacks {};

	/**
	 * Information about the current map we got from the API
	 */
	static inline struct
	{
		std::mutex mutex;
		std::optional<Surf::API::Map> data;
	} currentMap {};

	/**
	 * Information about all modes the API knows about
	 */
	static inline struct
	{
		std::mutex mutex;
		std::vector<Surf::API::handshake::HelloAck::ModeInfo> data;
	} globalModes;

	/**
	 * Information about all styles the API knows about
	 */
	static inline struct
	{
		std::mutex mutex;
		std::vector<Surf::API::handshake::HelloAck::StyleInfo> data;
	} globalStyles;

	static void EnforceConVars();
	static void RestoreConVars();

	/**
	 * Callback we pass to `IXWebSocket`.
	 *
	 * This will be called on the WebSocket thread.
	 */
	static void OnWebSocketMessage(const ix::WebSocketMessagePtr &message);

	/**
	 * Initiates the handshake with the API once a connection has been established.
	 *
	 * Has to be called from the main thread.
	 */
	static void InitiateHandshake();

	/**
	 * Completes the handshake with the API.
	 *
	 * Has to be called from the main thread.
	 */
	static void CompleteHandshake(Surf::API::handshake::HelloAck &ack);

	/**
	 * Queues a callback to be executed on the main thread as soon as possible.
	 */
	template<typename CB>
	static void AddMainThreadCallback(CB &&callback)
	{
		std::unique_lock lock(SurfGlobalService::mainThreadCallbacks.mutex);
		SurfGlobalService::mainThreadCallbacks.queue.emplace_back(std::move(callback));
	}

	/**
	 * Queues a callback to be executed on the main thread as soon as we have an established connection to the API.
	 */
	template<typename CB>
	static void AddWhenConnectedCallback(CB &&callback)
	{
		std::unique_lock lock(SurfGlobalService::mainThreadCallbacks.mutex);
		SurfGlobalService::mainThreadCallbacks.whenConnectedQueue.emplace_back(std::move(callback));
	}

	/**
	 * Queues a callback to be executed when we receive a message with the given ID.
	 *
	 * The callback will be executed on the main thread.
	 */
	template<typename CB>
	static void AddMessageCallback(u32 messageID, CB &&callback)
	{
		std::unique_lock lock(SurfGlobalService::messageCallbacks.mutex);
		SurfGlobalService::messageCallbacks.queue[messageID] = std::move(callback);
	}

	/**
	 * Executes the callback with the given ID, if any.
	 *
	 * The callback will be executed on the main thread.
	 */
	static void ExecuteMessageCallback(u32 messageID, const Json &payload);

	/**
	 * Prepares a message to be sent to the API.
	 *
	 * Note: we specialize `handshake::Hello` here because the format is slightly different from all other messages
	 */
	static bool PrepareMessage(std::string_view event, u32 messageID, const Surf::API::handshake::Hello &data, Json &payload)
	{
		if (SurfGlobalService::state.load() != State::Connected)
		{
			META_CONPRINTF("[Surf::Global] WARN: called `SendMessage()` before connection was established (state=%i)\n",
						   SurfGlobalService::state.load());
			return false;
		}

		return payload.Set("id", messageID) && data.ToJson(payload);
	}

	/**
	 * Prepares a message to be sent to the API.
	 */
	template<typename T>
	static bool PrepareMessage(std::string_view event, u32 messageID, const T &data, Json &payload)
	{
		if (SurfGlobalService::state.load() != State::HandshakeCompleted)
		{
			META_CONPRINTF("[Surf::Global] WARN: called `SendMessage()` before handshake has completed (state=%i)\n",
						   SurfGlobalService::state.load());
			return false;
		}

		// clang-format off
		bool success = payload.Set("id", messageID)
			&& payload.Set("event", event)
			&& payload.Set("data", data);
		// clang-format on

		if (!success)
		{
			META_CONPRINTF("[Surf::Global] Failed to serialize message for event `%s`.\n", event);
		}

		return success;
	}

	/**
	 * Sends a message to the API.
	 */
	template<typename T>
	static bool SendMessage(std::string_view event, const T &data)
	{
		Json payload;

		if (!SurfGlobalService::PrepareMessage(event, SurfGlobalService::nextMessageID++, data, payload))
		{
			return false;
		}

		SurfGlobalService::socket->send(payload.ToString());
		return true;
	}

	/**
	 * Sends a message to the API with a callback to be executed when we get a response.
	 */
	template<typename T, typename CB>
	static bool SendMessage(std::string_view event, const T &data, CB &&callback)
	{
		u32 messageID = SurfGlobalService::nextMessageID++;
		Json payload;

		if (!SurfGlobalService::PrepareMessage(event, messageID, data, payload))
		{
			return false;
		}

		// clang-format off
		SurfGlobalService::AddMessageCallback(messageID, [callback = std::move(callback)](u32 messageID, const Json& payload)
		{
			if (!payload.IsValid())
			{
				META_CONPRINTF("[Surf::Global] WebSocket message is not valid JSON.\n");
				return;
			}

			std::remove_reference_t<typename decltype(std::function(callback))::argument_type> decoded;

			if (!payload.Get("data", decoded))
			{
				META_CONPRINTF("[Surf::Global] WebSocket message does not contain a valid `data` field.\n");
				return;
			}

			callback(decoded);
		});
		// clang-format on

		SurfGlobalService::socket->send(payload.ToString());
		return true;
	}
};
