#include "surf_db.h"
#include "surf/style/surf_style.h"
#include "queries/styles.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Surf::Database;

void SurfDatabaseService::UpdateStyleIDs()
{
	if (!SurfDatabaseService::IsReady())
	{
		return;
	}
	// clang-format off
	SurfDatabaseService::GetDatabaseConnection()->Query(sql_styles_fetch_all,
		[](ISQLQuery *query)
		{
			auto resultSet = query->GetResultSet();
			while (resultSet->FetchRow())
			{
				Surf::style::UpdateStyleDatabaseID(query->GetResultSet()->GetString(1), query->GetResultSet()->GetInt(0));
			}
		});
	// clang-format on
}

void SurfDatabaseService::InsertAndUpdateStyleIDs(const std::string &styleName, const std::string &shortName)
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
			V_snprintf(query, sizeof(query), sqlite_styles_insert, styleName.c_str(), shortName.c_str());
			break;
		}
		case DatabaseType::MySQL:
		{
			V_snprintf(query, sizeof(query), mysql_styles_insert, styleName.c_str(), shortName.c_str());
			break;
		}
		default:
		{
			// Should never happen.
			query[0] = 0;
		}
	}
	txn.queries.push_back(query);

	V_snprintf(query, sizeof(query), sql_styles_findid, styleName.c_str());
	txn.queries.push_back(query);
	// clang-format off
	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(
		txn, 
		[styleName](std::vector<ISQLQuery *> queries) 
		{
			auto resultSet = queries[1]->GetResultSet();
			if (resultSet->FetchRow())
			{
				Surf::style::UpdateStyleDatabaseID(styleName, queries[1]->GetResultSet()->GetInt(0));
			}
		},
		OnGenericTxnFailure);
	// clang-format on
}
