#include "surf_db.h"
#include "surf/mode/surf_mode.h"
#include "queries/modes.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Surf::Database;

void SurfDatabaseService::UpdateModeIDs()
{
	if (!SurfDatabaseService::IsReady())
	{
		return;
	}
	// clang-format off
	SurfDatabaseService::GetDatabaseConnection()->Query(sql_modes_fetch_all,
		[](ISQLQuery *query)
		{
			auto resultSet = query->GetResultSet();
			while (resultSet->FetchRow())
			{
				Surf::mode::UpdateModeDatabaseID(query->GetResultSet()->GetString(1), query->GetResultSet()->GetInt(0));
			}
		});
	// clang-format on
}

void SurfDatabaseService::InsertAndUpdateModeIDs(const std::string &modeName, const std::string &shortName)
{
	if (!SurfDatabaseService::IsReady())
	{
		return;
	}
	Transaction txn;
	char query[2048];
	switch (SurfDatabaseService::GetDatabaseType())
	{
		case DatabaseType::SQLite:
		{
			V_snprintf(query, sizeof(query), sqlite_modes_insert, modeName.c_str(), shortName.c_str());
			break;
		}
		case DatabaseType::MySQL:
		{
			V_snprintf(query, sizeof(query), mysql_modes_insert, modeName.c_str(), shortName.c_str());
			break;
		}
		default:
		{
			// Should never happen.
			query[0] = 0;
		}
	}
	txn.queries.push_back(query);

	V_snprintf(query, sizeof(query), sql_modes_findid, modeName.c_str());
	txn.queries.push_back(query);
	// clang-format off
	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(
		txn, 
		[modeName](std::vector<ISQLQuery *> queries) 
		{
			auto resultSet = queries[1]->GetResultSet();
			while (resultSet->FetchRow())
			{
				Surf::mode::UpdateModeDatabaseID(modeName, queries[1]->GetResultSet()->GetInt(0));
			}
		},
		OnGenericTxnFailure);
	// clang-format on
}
