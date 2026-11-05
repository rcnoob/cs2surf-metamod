#include "surf_db.h"
#include "queries/maps.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

/*
	Inserts the map information into the database.
	Retrieves the MapID of the map and stores it in a global variable.
*/

using namespace Surf::Database;

static_global bool mapSetUp = false;
i32 SurfDatabaseService::currentMapID {};

bool SurfDatabaseService::IsMapSetUp()
{
	return mapSetUp;
}

void SurfDatabaseService::SetupMap()
{
	mapSetUp = false;
	if (!SurfDatabaseService::IsReady())
	{
		SURF_LOG_INFO(LogChannel::DB, "Warning: SetupMap called too early.\n");
		return;
	}

	Transaction txn;
	char query[2048];
	std::string mapName = g_pSurfUtils->GetCurrentMapName();
	auto escapedMapName = SurfDatabaseService::GetDatabaseConnection()->Escape(mapName.c_str());
	auto databaseType = SurfDatabaseService::GetDatabaseType();
	switch (databaseType)
	{
		case DatabaseType::SQLite:
		{
			V_snprintf(query, sizeof(query), sqlite_maps_insert, escapedMapName.c_str());
			txn.queries.push_back(query);
			V_snprintf(query, sizeof(query), sqlite_maps_update, escapedMapName.c_str());
			txn.queries.push_back(query);
			break;
		}
		case DatabaseType::MySQL:
		{
			V_snprintf(query, sizeof(query), mysql_maps_upsert, escapedMapName.c_str());
			txn.queries.push_back(query);
			break;
		}
		default:
		{
			// This shouldn't happen.
			query[0] = 0;
			break;
		}
	}

	V_snprintf(query, sizeof(query), sql_maps_findid, escapedMapName.c_str(), escapedMapName.c_str());
	txn.queries.push_back(query);
	// clang-format off
	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(
		txn, 
		[databaseType, mapName](std::vector<ISQLQuery *> queries) 
		{
			std::string currentMapName = g_pSurfUtils->GetCurrentMapName();
			if (!SURF_STREQ(currentMapName.c_str(), mapName.c_str()))
			{
				SURF_LOG_WARN(LogChannel::DB, "Failed to setup map, current map name %s doesn't match %s!\n", currentMapName.c_str(), mapName.c_str());
				return;
			}
			switch (databaseType)
			{
				case DatabaseType::SQLite:
				{
					auto resultSet = queries[2]->GetResultSet();
					if (resultSet->FetchRow())
					{
						SurfDatabaseService::currentMapID = resultSet->GetInt(0);
					}
					break;
				}
				case DatabaseType::MySQL:
				{
					auto resultSet = queries[1]->GetResultSet();
					if (resultSet->FetchRow())
					{
						SurfDatabaseService::currentMapID = resultSet->GetInt(0);
					}
					break;
				}
				default:
				{
					// This shouldn't happen.
					break;
				}
			}
			mapSetUp = true;
			SURF_LOG_INFO(LogChannel::DB, "Map setup successful for %s, current map ID: %i\n", currentMapName.c_str(), SurfDatabaseService::currentMapID);
			CALL_FORWARD(eventListeners, OnMapSetup);
		},
		OnGenericTxnFailure);
	// clang-format on
}
