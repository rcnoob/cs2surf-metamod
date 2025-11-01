#include "base_request.h"
#include "surf/timer/surf_timer.h"
#include "surf/db/surf_db.h"
#include "surf/global/surf_global.h"
#include "surf/global/events.h"

#include "utils/simplecmds.h"

#include "vendor/sql_mm/src/public/sql_mm.h"

struct TopRecordRequest : public BaseRequest
{
	using BaseRequest::BaseRequest;
	static constexpr u64 trFeatures = RequestFeature::Course | RequestFeature::Map | RequestFeature::Mode;

	struct RecordData
	{
		bool hasRecord {};
		CUtlString holder;
		f32 runTime {};

	} srData, wrData;

	virtual void PrintInstructions() override
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);
		if (!player)
		{
			return;
		}
		player->languageService->PrintChat(true, false, "WR/SR Command Usage");
		player->languageService->PrintConsole(false, false, "WR/SR Command Usage - Console");
	}

	virtual void QueryGlobal()
	{
		if (this->requestingFirstCourse)
		{
			return;
		}
		if (this->globalStatus == ResponseStatus::ENABLED)
		{
			auto callback = [uid = this->uid](Surf::API::events::WorldRecords &wrs)
			{
				TopRecordRequest *req = (TopRecordRequest *)TopRecordRequest::Find(uid);
				if (!req)
				{
					return;
				}

				if (wrs.map.has_value() && wrs.course.has_value())
				{
					req->mapName = wrs.map->name.c_str();
					req->courseName = wrs.course->name.c_str();
					req->globalStatus = ResponseStatus::RECEIVED;
				}
				else
				{
					req->globalStatus = ResponseStatus::DISABLED;
					return;
				}

				req->wrData.hasRecord = wrs.overall.has_value();
				if (req->wrData.hasRecord)
				{
					req->wrData.holder = wrs.overall->player.name.c_str();
					req->wrData.runTime = wrs.overall->time;
				}
			};
			this->globalStatus = ResponseStatus::PENDING;
			SurfGlobalService::QueryWorldRecords(std::string_view(this->mapName.Get(), this->mapName.Length()),
												 std::string_view(this->courseName.Get(), this->courseName.Length()), this->apiMode, callback);
		}
	}

	virtual void QueryLocal()
	{
		if (this->requestingFirstCourse)
		{
			return;
		}
		if (this->localStatus == ResponseStatus::ENABLED)
		{
			this->localStatus = ResponseStatus::PENDING;

			SurfPlayer *callingPlayer = g_pSurfPlayerManager->ToPlayer(userID);
			if (!callingPlayer)
			{
				this->Invalidate();
				return;
			}

			u64 uid = this->uid;

			auto onQuerySuccess = [uid](std::vector<ISQLQuery *> queries)
			{
				TopRecordRequest *req = (TopRecordRequest *)TopRecordRequest::Find(uid);
				if (!req)
				{
					return;
				}
				req->localStatus = ResponseStatus::RECEIVED;
				ISQLResult *result = queries[0]->GetResultSet();
				if (result && result->GetRowCount() > 0)
				{
					req->srData.hasRecord = true;
					if (result->FetchRow())
					{
						req->srData.holder = result->GetString(2);
						req->srData.runTime = result->GetFloat(3);
					}
				}
			};

			auto onQueryFailure = [uid](std::string, int)
			{
				TopRecordRequest *req = (TopRecordRequest *)TopRecordRequest::Find(uid);
				if (req)
				{
					req->localStatus = ResponseStatus::DISABLED;
				}
			};

			SurfDatabaseService::QueryRecords(this->mapName, this->courseName, this->localModeID, 1, 0, onQuerySuccess, onQueryFailure);
		}
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
			player->languageService->PrintChat(true, false, "Top Record Request - Failed (Generic)");
			return;
		}
		if (this->localStatus == ResponseStatus::RECEIVED)
		{
			this->ReplyLocal();
		}
		if (this->globalStatus == ResponseStatus::RECEIVED)
		{
			this->ReplyGlobal();
		}
	}

	void ReplyGlobal()
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);
		// Local stuff

		char standardTime[32];
		SurfTimerService::FormatTime(wrData.runTime, standardTime, sizeof(standardTime));

		// Global Records on surf_map (Main) [VNL]
		player->languageService->PrintChat(true, false, "WR Header", mapName.Get(), courseName.Get(), modeName.Get());
		if (!wrData.hasRecord)
		{
			player->languageService->PrintChat(true, false, "No Times");
		}
		else
		{
			// Surf | Overall Record: 01:23.45 by Bill
			player->languageService->PrintChat(true, false, "Top Record - Overall", standardTime, wrData.holder.Get());
		}
	}

	void ReplyLocal()
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);
		// Local stuff

		char standardTime[32];
		SurfTimerService::FormatTime(srData.runTime, standardTime, sizeof(standardTime));

		// Server Records on surf_map (Main) [VNL]
		player->languageService->PrintChat(true, false, "SR Header", mapName.Get(), courseName.Get(), modeName.Get());
		if (!srData.hasRecord)
		{
			player->languageService->PrintChat(true, false, "No Times");
		}
		else
		{
			// Surf | Overall Record: 01:23.45 by Bill
			player->languageService->PrintChat(true, false, "Top Record - Overall", standardTime, srData.holder.Get());
		}
	}
};

SCMD(surf_wr, SCFL_RECORD | SCFL_GLOBAL)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	TopRecordRequest::Create<TopRecordRequest>(player, TopRecordRequest::trFeatures, true, true, args);
	return MRES_SUPERCEDE;
}

SCMD(surf_sr, SCFL_RECORD)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	TopRecordRequest::Create<TopRecordRequest>(player, TopRecordRequest::trFeatures, true, false, args);
	return MRES_SUPERCEDE;
}
