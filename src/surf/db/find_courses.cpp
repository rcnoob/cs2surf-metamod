#include "surf_db.h"
#include "vendor/sql_mm/src/public/sql_mm.h"
#include "queries/courses.h"

void SurfDatabaseService::FindFirstCourseByMapName(CUtlString mapName, TransactionSuccessCallbackFunc onSuccess,
												   TransactionFailureCallbackFunc onFailure)
{
	auto cleanMapName = SurfDatabaseService::GetDatabaseConnection()->Escape(mapName.Get());

	char query[1024];
	V_snprintf(query, sizeof(query), sql_mapcourses_findfirst_mapname, cleanMapName.c_str(), cleanMapName.c_str());

	Transaction txn;
	txn.queries.push_back(query);

	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}
