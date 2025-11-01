#include "base_request.h"
#include "surf/db/surf_db.h"
#include "surf/global/surf_global.h"
#include "surf/global/events.h"

#include "utils/utils.h"
#include "utils/simplecmds.h"

#include "vendor/sql_mm/src/public/sql_mm.h"

struct PBRequest : public BaseRequest
{
	using BaseRequest::BaseRequest;
	static constexpr u64 pbFeatures =
		RequestFeature::Course | RequestFeature::Map | RequestFeature::Mode | RequestFeature::Player | RequestFeature::Style;

	struct
	{
		bool hasPB {};
		f32 runTime {};
		u32 rank {};
		u32 maxRank {};
		u32 points {}; // Global only.

	} pbData, gpbData;

	bool globallyBanned = false;
	bool queryLocalRanking = true;

	virtual void Init(u64 features, const CCommand *args, bool queryLocal, bool queryGlobal) override
	{
		BaseRequest::Init(features, args, queryLocal, queryGlobal);
		if (this->styleList.Count() > 0)
		{
			this->queryLocalRanking = false;
		}
		else if (this->targetSteamID64)
		{
			for (u32 i = 1; i < MAXPLAYERS + 1; i++)
			{
				SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
				if (this->targetSteamID64 == player->GetSteamId64())
				{
					if (player->databaseService->isCheater)
					{
						this->queryLocalRanking = false;
					}
					return;
				}
			}
		}
	}

	virtual void PrintInstructions()
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);
		if (!player)
		{
			return;
		}
		player->languageService->PrintChat(true, false, "PB Command Usage");
		player->languageService->PrintConsole(false, false, "PB Command Usage - Console");
	}

	virtual void QueryLocal()
	{
		if (this->requestingLocalPlayer || this->requestingFirstCourse || this->requestingGlobalPlayer)
		{
			return;
		}

		SurfPlayer *callingPlayer = g_pSurfPlayerManager->ToPlayer(userID);
		if (!callingPlayer)
		{
			this->Invalidate();
			return;
		}

		if (this->localStatus == ResponseStatus::ENABLED)
		{
			this->localStatus = ResponseStatus::PENDING;
			this->queryLocalRanking ? this->ExecuteStandardLocalQuery() : this->ExecuteRanklessLocalQuery();
		}
	}

	virtual void QueryGlobal()
	{
		if (this->requestingLocalPlayer || this->requestingFirstCourse)
		{
			return;
		}
		if (this->globalStatus != ResponseStatus::ENABLED)
		{
			// If the local query is waiting for a response from the global service...
			if (this->requestingGlobalPlayer && this->localStatus == ResponseStatus::ENABLED)
			{
				this->localStatus = ResponseStatus::DISABLED;
				this->requestingGlobalPlayer = false;
			}
			return;
		}
		this->globalStatus = ResponseStatus::PENDING;
		auto callback = [uid = this->uid](Surf::API::events::PersonalBest &pb)
		{
			PBRequest *req = (PBRequest *)PBRequest::Find(uid);
			if (!req)
			{
				return;
			}

			if (req->requestingGlobalPlayer && req->localStatus == ResponseStatus::ENABLED && pb.player.has_value())
			{
				req->requestingGlobalPlayer = false;
				req->queryLocalRanking = !pb.player->isBanned;
			}

			if (pb.map.has_value() && pb.course.has_value())
			{
				req->mapName = pb.map->name.c_str();
				req->courseName = pb.course->name.c_str();
				req->globalStatus = ResponseStatus::RECEIVED;
			}
			else
			{
				req->globalStatus = ResponseStatus::DISABLED;
				return;
			}

			req->gpbData.hasPB = pb.overall.has_value();
			if (req->gpbData.hasPB)
			{
				req->gpbData.runTime = pb.overall->time;
				req->gpbData.rank = pb.overall->rank;
				req->gpbData.maxRank = pb.overall->maxRank;
				req->gpbData.points = pb.overall->points;
			}
		};
		SurfGlobalService::QueryPB(this->targetSteamID64, std::string_view(this->targetPlayerName.Get(), this->targetPlayerName.Length()),
								   std::string_view(this->mapName.Get(), this->mapName.Length()),
								   std::string_view(this->courseName.Get(), this->courseName.Length()), this->apiMode, this->styleList, callback);
	}

	virtual void Reply()
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);
		if (!player)
		{
			return;
		}
		if (localStatus != ResponseStatus::RECEIVED && globalStatus != ResponseStatus::RECEIVED)
		{
			player->languageService->PrintChat(true, false, "PB Request - Failed (Generic)");
			return;
		}

		CUtlString combinedModeStyleText;
		combinedModeStyleText.Format("{purple}%s{grey}", modeName.Get());
		FOR_EACH_VEC(styleList, i)
		{
			combinedModeStyleText += " +{grey2}";
			combinedModeStyleText.Append(styleList[i].Get());
			combinedModeStyleText += "{grey}";
		}

		// Player on surf_map (Main) [VNL]
		player->languageService->PrintChat(true, false, "PB Header", targetPlayerName.Get(), mapName.Get(), courseName.Get(),
										   combinedModeStyleText.Get());

		if (!this->pbData.hasPB && !this->gpbData.hasPB)
		{
			player->languageService->PrintChat(true, false, "PB Time - No Times");
		}
		else
		{
			if (this->localStatus == ResponseStatus::RECEIVED)
			{
				this->ReplyLocal();
			}
			if (this->globalStatus == ResponseStatus::RECEIVED)
			{
				this->ReplyGlobal();
			}
		}
	}

	void ReplyGlobal()
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);

		char overallTime[32];
		SurfTimerService::FormatTime(this->gpbData.runTime, overallTime, sizeof(overallTime));

		if (!globallyBanned)
		{
			if (this->gpbData.hasPB)
			{
				// Surf | Global: 12.34 [Overall / 10000 pts]
				player->languageService->PrintChat(true, false, "PB Time - Overall (Global)", overallTime, this->gpbData.rank, this->gpbData.maxRank,
												   this->gpbData.points);
			}
		}
		else
		{
			if (this->gpbData.hasPB)
			{
				// Surf | Global: 12.34 [Overall]
				player->languageService->PrintChat(true, false, "PB Time - Overall Rankless (Global)", overallTime);
			}
		}
	}

	void ReplyLocal()
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);

		char overallTime[32];
		SurfTimerService::FormatTime(this->pbData.runTime, overallTime, sizeof(overallTime));

		if (!globallyBanned)
		{
			if (!this->pbData.hasPB)
			{
				player->languageService->PrintChat(true, false, "PB Time - No Times");
			}
			else
			{
				// Surf | Server: 12.34 [Overall]
				player->languageService->PrintChat(true, false, "PB Time - Overall (Server)", overallTime, this->pbData.rank, this->pbData.maxRank);
			}
		}
		else
		{
			if (!this->pbData.hasPB)
			{
				player->languageService->PrintChat(true, false, "PB Time - No Times");
			}
			else
			{
				// Surf | Server: 12.34 [Overall]
				player->languageService->PrintChat(true, false, "PB Time - Overall Rankless (Server)", overallTime);
			}
		}
	}

	void ExecuteStandardLocalQuery()
	{
		u64 uid = this->uid;

		auto onQuerySuccess = [uid](std::vector<ISQLQuery *> queries)
		{
			PBRequest *req = (PBRequest *)PBRequest::Find(uid);
			if (req)
			{
				ISQLResult *result = queries[0]->GetResultSet();
				if (result && result->GetRowCount() > 0)
				{
					req->pbData.hasPB = true;
					if (result->FetchRow())
					{
						req->pbData.runTime = result->GetFloat(0);
					}
					if ((result = queries[1]->GetResultSet()) && result->FetchRow())
					{
						req->pbData.rank = result->GetInt(0);
					}
					if ((result = queries[2]->GetResultSet()) && result->FetchRow())
					{
						req->pbData.maxRank = result->GetInt(0);
					}
				}
				req->localStatus = ResponseStatus::RECEIVED;
			}
		};

		auto onQueryFailure = [uid](std::string, int)
		{
			PBRequest *req = (PBRequest *)PBRequest::Find(uid);
			if (req)
			{
				req->localStatus = ResponseStatus::DISABLED;
			}
		};

		SurfDatabaseService::QueryPB(targetSteamID64, this->mapName, this->courseName, this->localModeID, onQuerySuccess, onQueryFailure);
	}

	void ExecuteRanklessLocalQuery()
	{
		u64 uid = this->uid;
		auto onQuerySuccess = [uid](std::vector<ISQLQuery *> queries)
		{
			PBRequest *req = (PBRequest *)PBRequest::Find(uid);
			if (req)
			{
				ISQLResult *result = queries[0]->GetResultSet();
				if (result && result->GetRowCount() > 0)
				{
					req->pbData.hasPB = true;
					if (result->FetchRow())
					{
						req->pbData.runTime = result->GetFloat(0);
					}
				}
			}
		};

		auto onQueryFailure = [uid](std::string, int)
		{
			PBRequest *req = (PBRequest *)PBRequest::Find(uid);
			if (req)
			{
				req->localStatus = ResponseStatus::DISABLED;
			}
		};
		SurfDatabaseService::QueryPBRankless(targetSteamID64, mapName, courseName, localModeID, localStyleIDs, onQuerySuccess, onQueryFailure);
	}
};

SCMD(surf_pb, SCFL_RECORD | SCFL_GLOBAL)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	PBRequest::Create<PBRequest>(player, PBRequest::pbFeatures, true, true, args);
	return MRES_SUPERCEDE;
}

SCMD(surf_spb, SCFL_RECORD)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	PBRequest::Create<PBRequest>(player, PBRequest::pbFeatures, true, false, args);
	return MRES_SUPERCEDE;
}

SCMD(surf_gpb, SCFL_RECORD | SCFL_GLOBAL)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	PBRequest::Create<PBRequest>(player, PBRequest::pbFeatures, false, true, args);
	return MRES_SUPERCEDE;
}
