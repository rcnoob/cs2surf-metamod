#include "surf_db.h"
#include "vendor/sql_mm/src/public/sql_mm.h"
#include "queries/courses.h"

void SurfDatabaseService::FindFirstCourseByMapName(const std::string &mapName, TransactionSuccessCallbackFunc onSuccess,
												   TransactionFailureCallbackFunc onFailure)
{
	auto cleanMapName = SurfDatabaseService::GetDatabaseConnection()->Escape(mapName.c_str());

	char query[2048];
	V_snprintf(query, sizeof(query), sql_mapcourses_findfirst_mapname, cleanMapName.c_str(), cleanMapName.c_str());

	Transaction txn;
	txn.queries.push_back(query);

	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}
