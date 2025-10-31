#include "announce.h"
#include "surf/db/surf_db.h"
#include "surf/global/surf_global.h"
#include "surf/global/events.h"
#include "surf/language/surf_language.h"
#include "surf/mode/surf_mode.h"
#include "surf/style/surf_style.h"

#include "vendor/sql_mm/src/public/sql_mm.h"

CConVar<bool> surf_debug_announce_global("surf_debug_announce_global", FCVAR_NONE, "Print debug info for record announcements.", false);

RecordAnnounce::RecordAnnounce(SurfPlayer *player)
	: uid(RecordAnnounce::idCount++), timestamp(g_pSurfUtils->GetServerGlobals()->realtime), userID(player->GetClient()->GetUserID()),
	  time(player->timerService->GetTime())
{
	this->local = SurfDatabaseService::IsReady() && SurfDatabaseService::IsMapSetUp();
	this->global = player->hasPrime && SurfGlobalService::IsAvailable();
	if (surf_debug_announce_global.Get() && !this->global)
	{
		if (!player->hasPrime)
		{
			META_CONPRINTF("[Surf::Global - %u] Player %s does not have Prime, will not submit globally.\n", uid, player->GetName());
		}
		if (!SurfGlobalService::IsAvailable())
		{
			META_CONPRINTF("[Surf::Global - %u] Global service is not available, will not submit globally.\n", uid);
		}
	}
	// Setup player
	this->player.name = player->GetName();
	this->player.steamid64 = player->GetSteamId64();

	// Setup mode
	auto mode = Surf::mode::GetModeInfo(player->modeService);
	this->mode.name = mode.shortModeName;
	Surf::API::Mode apiMode;
	this->global = Surf::API::DecodeModeString(this->mode.name, apiMode);
	if (!this->global)
	{
		if (surf_debug_announce_global.Get())
		{
			META_CONPRINTF("[Surf::Global - %u] Mode '%s' is not a valid global mode, will not submit globally.\n", uid, this->mode.name.c_str());
		}
	}
	this->mode.md5 = mode.md5;
	if (mode.databaseID <= 0)
	{
		this->local = false;
	}
	else
	{
		this->mode.localID = mode.databaseID;
	}

	// Setup map
	this->map.name = g_pSurfUtils->GetServerGlobals()->mapname.ToCStr();
	char md5[33];
	g_pSurfUtils->GetCurrentMapMD5(md5, sizeof(md5));
	this->map.md5 = md5;

	// Setup course
	assert(player->timerService->GetCourse());

	this->course.name = player->timerService->GetCourse()->GetName().Get();
	this->course.localID = player->timerService->GetCourse()->localDatabaseID;

	// clang-format off

	SurfGlobalService::WithCurrentMap([&](const Surf::API::Map *currentMap)
	{
		this->global = currentMap != nullptr;

		if (currentMap == nullptr)
		{
			return;
		}

		const Surf::API::Map::Course *course = nullptr;

		for (const Surf::API::Map::Course &c : currentMap->courses)
		{
			if (SURF_STREQ(c.name.c_str(), this->course.name.c_str()))
			{
				course = &c;
				break;
			}
		}

		if (course == nullptr)
		{
			if (surf_debug_announce_global.Get())
			{
				META_CONPRINTF("[Surf::Global - %u] Course '%s' not found on global map '%s', will not submit globally.\n", uid, this->course.name.c_str(),
							   currentMap->name.c_str());
				META_CONPRINTF("[Surf::Global - %u] Available courses:\n", uid);
				for (const Surf::API::Map::Course &c : currentMap->courses)
				{
					META_CONPRINTF(" - %s\n", c.name.c_str());
				}
			}
			global = false;
		}
		else
		{
			/*
			this->globalFilterID = (apiMode == Surf::API::Mode::Classic)
				? course->filters.classic.id
				: course->filters.vanilla.id;
			*/
			this->globalFilterID = course->filters._64tick.id;
		}
	});

	// clang-format on

	// Setup styles
	FOR_EACH_VEC(player->styleServices, i)
	{
		auto style = Surf::style::GetStyleInfo(player->styleServices[i]);
		this->styles.push_back({player->styleServices[i]->GetStyleShortName(), style.md5});
		if (style.databaseID < 0)
		{
			this->local = false;
		}
		this->styleIDs |= (1ull << style.databaseID);
	}

	// Metadata
	this->metadata = player->timerService->GetCurrentRunMetadata().Get();

	// Previous GPBs
	if (global)
	{
		auto pbData = player->timerService->GetGlobalCachedPB(player->timerService->GetCourse(), mode.id);
		if (pbData)
		{
			this->oldGPB.overall.time = pbData->overall.pbTime;
			this->oldGPB.overall.points = pbData->overall.points;
		}
	}

	if (local)
	{
		this->SubmitLocal();
	}
	if (global)
	{
		this->SubmitGlobal();
	}
}

void RecordAnnounce::SubmitGlobal()
{
	auto callback = [uid = this->uid](Surf::API::events::NewRecordAck &ack)
	{
		META_CONPRINTF("[Surf::Global - %u] Record submitted under ID %d\n", uid, ack.recordId);

		RecordAnnounce *rec = RecordAnnounce::Get(uid);
		if (!rec)
		{
			return;
		}
		rec->globalResponse.received = true;
		rec->globalResponse.recordId = ack.recordId;
		// TODO: Remove this 0.1 when API sends the correct rating
		rec->globalResponse.playerRating = ack.playerRating * 0.1f;
		rec->globalResponse.overall.rank = ack.overallData.rank;
		rec->globalResponse.overall.points = ack.overallData.points;
		rec->globalResponse.overall.maxRank = ack.overallData.leaderboardSize;

		// cache should not be overwritten by styled runs
		if (rec->styles.empty())
		{
			rec->UpdateGlobalCache();
		}
	};

	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(this->userID);

	// Dirty hack since nested forward declaration isn't possible.
	SurfGlobalService::SubmitRecordResult submissionResult = player->globalService->SubmitRecord(
		this->globalFilterID, this->time, this->mode.md5, (void *)(&this->styles), this->metadata.c_str(), callback);

	if (surf_debug_announce_global.Get())
	{
		META_CONPRINTF("[Surf::Global - %u] Global record submission result: %d\n", uid, static_cast<int>(submissionResult));
	}
	switch (submissionResult)
	{
		case SurfGlobalService::SubmitRecordResult::PlayerNotAuthenticated: /* fallthrough */
		case SurfGlobalService::SubmitRecordResult::MapNotGlobal:           /* fallthrough */
		case SurfGlobalService::SubmitRecordResult::Queued:
		{
			this->global = false;
			break;
		};
		case SurfGlobalService::SubmitRecordResult::Submitted:
		{
			this->global = true;
			break;
		};
	}
}

void RecordAnnounce::UpdateGlobalCache()
{
	SurfGlobalService::UpdateRecordCache();
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(this->userID);
	if (player && this->globalResponse.received)
	{
		const SurfCourseDescriptor *course = Surf::course::GetCourse(this->course.name.c_str());
		auto mode = Surf::mode::GetModeInfo(this->mode.name.c_str());
		if (mode.id > -2)
		{
			META_CONPRINTF("this->time=%f this->oldGPB.overall.time=%f", this->time, this->oldGPB.overall.time);

			if (this->time < this->oldGPB.overall.time)
			{
				player->timerService->InsertPBToCache(this->time, course, mode.id, true, true, this->metadata.c_str(),
													  this->globalResponse.overall.points);
			}
		}
	}
}

void RecordAnnounce::SubmitLocal()
{
	auto onFailure = [uid = this->uid](std::string, int)
	{
		RecordAnnounce *rec = RecordAnnounce::Get(uid);
		if (!rec)
		{
			return;
		}
		rec->local = false;
	};
	auto onSuccess = [uid = this->uid](std::vector<ISQLQuery *> queries)
	{
		RecordAnnounce *rec = RecordAnnounce::Get(uid);
		if (!rec)
		{
			return;
		}
		rec->localResponse.received = true;

		ISQLResult *result = queries[1]->GetResultSet();
		rec->localResponse.overall.firstTime = result->GetRowCount() == 1;
		if (!rec->localResponse.overall.firstTime)
		{
			result->FetchRow();
			f32 pb = result->GetFloat(0);
			// Close enough. New time is new PB.
			if (fabs(pb - rec->time) < EPSILON)
			{
				result->FetchRow();
				f32 oldPB = result->GetFloat(0);
				rec->localResponse.overall.pbDiff = rec->time - oldPB;
			}
			else // Didn't beat PB
			{
				rec->localResponse.overall.pbDiff = rec->time - pb;
			}
		}
		result = queries[2]->GetResultSet();
		result->FetchRow();
		rec->localResponse.overall.rank = result->GetInt(0);
		result = queries[3]->GetResultSet();
		result->FetchRow();
		rec->localResponse.overall.maxRank = result->GetInt(0);

		rec->UpdateLocalCache();
	};
	SurfDatabaseService::SaveTime(this->player.steamid64, this->course.localID, this->mode.localID, this->time, this->styleIDs,
								this->metadata, onSuccess, onFailure);
}

void RecordAnnounce::UpdateLocalCache()
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(this->userID);
	if (player)
	{
		player->timerService->UpdateLocalPBCache();
	}
	SurfTimerService::UpdateLocalRecordCache();
}

void RecordAnnounce::AnnounceRun()
{
	char formattedTime[32];
	SurfTimerService::FormatTime(time, formattedTime, sizeof(formattedTime));

	CUtlString combinedModeStyleText;
	combinedModeStyleText.Format("{purple}%s{grey}", this->mode.name.c_str());
	for (auto style : this->styles)
	{
		combinedModeStyleText += " +{grey2}";
		combinedModeStyleText.Append(style.name.c_str());
		combinedModeStyleText += "{grey}";
	}

	for (u32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		// Print basic information
		// Surf | GameChaos finished "blocks2006" in 10:06.84 [VNL | PRO]
		if (!player->IsInGame())
		{
			continue;
		}
		player->languageService->PrintChat(true, false, "Beat Course Info - Basic", this->player.name.c_str(), this->course.name.c_str(),
										   formattedTime, combinedModeStyleText.Get());
	}
}

void RecordAnnounce::AnnounceLocal()
{
	for (u32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);

		if (!player->IsInGame())
		{
			continue;
		}
		// Print server ranking information if available.

		char formattedDiffTime[32];
		SurfTimerService::FormatDiffTime(this->localResponse.overall.pbDiff, formattedDiffTime, sizeof(formattedDiffTime));


		// clang-format off
        std::string diffText = this->localResponse.overall.firstTime ? 
            "" : player->languageService->PrepareMessage("Personal Best Difference", this->localResponse.overall.pbDiff < 0 ? "{green}" : "{red}", formattedDiffTime);

		// clang-format on

		player->languageService->PrintChat(true, false, "Beat Course Info - Local", this->localResponse.overall.rank,
											this->localResponse.overall.maxRank, diffText.c_str());
	}
}

void RecordAnnounce::AnnounceGlobal()
{
	for (u32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);

		if (!player->IsInGame())
		{
			continue;
		}
		bool hasOldPB = this->oldGPB.overall.time;

		f64 pbDiff = this->time - this->oldGPB.overall.time;
		// Players can't lose points for being slower than PB.
		f64 pointsDiff = MAX(this->globalResponse.overall.points - this->oldGPB.overall.points, 0.0f);

		char formattedDiffTime[32];
		SurfTimerService::FormatDiffTime(pbDiff, formattedDiffTime, sizeof(formattedDiffTime));

		// clang-format off
        std::string diffText = hasOldPB
            ? player->languageService->PrepareMessage("Personal Best Difference", pbDiff < 0 ? "{green}" : "{red}", formattedDiffTime)
            : "";
		// clang-format on

		player->languageService->PrintChat(true, false, "Beat Course Info - Global", this->globalResponse.overall.rank,
											this->globalResponse.overall.maxRank, diffText.c_str());

		player->languageService->PrintChat(true, false, "Beat Course Info - Global Points", this->globalResponse.overall.points,
											pointsDiff, this->globalResponse.playerRating);
	}
}
