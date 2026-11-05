#include "surf_db.h"
#include "vendor/sql_mm/src/public/sql_mm.h"
#include "queries/course_top.h"

void SurfDatabaseService::QueryAllRecords(const std::string &mapName, TransactionSuccessCallbackFunc onSuccess,
										  TransactionFailureCallbackFunc onFailure)
{
	std::string cleanedMapName = SurfDatabaseService::GetDatabaseConnection()->Escape(mapName.c_str());

	Transaction txn;

	char query[2048];
	// Get PB
	V_snprintf(query, sizeof(query), sql_getsrs, cleanedMapName.c_str());
	txn.queries.push_back(query);

	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}

void SurfDatabaseService::QueryRecords(const std::string &mapName, const std::string &courseName, u32 modeID, u32 count, u32 offset,
									   TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure)
{
	std::string cleanedMapName = SurfDatabaseService::GetDatabaseConnection()->Escape(mapName.c_str());
	std::string cleanedCourseName = SurfDatabaseService::GetDatabaseConnection()->Escape(courseName.c_str());

	Transaction txn;

	char query[2048];
	// Get PB
	V_snprintf(query, sizeof(query), sql_getcoursetop, cleanedMapName.c_str(), cleanedCourseName.c_str(), modeID, count, offset);
	txn.queries.push_back(query);

	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}
