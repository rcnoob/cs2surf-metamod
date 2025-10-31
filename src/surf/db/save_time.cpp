#include "surf_db.h"
#include "surf/mode/surf_mode.h"
#include "surf/style/surf_style.h"
#include "surf/timer/surf_timer.h"
#include "queries/save_time.h"
#include "queries/times.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Surf::Database;

void SurfDatabaseService::SaveTime(u64 steamID, u32 courseID, i32 modeID, f64 time, u64 styleIDs, std::string_view metadata, 
									TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure)
{
	if (!SurfDatabaseService::IsReady())
	{
		return;
	}

	char query[1024];
	Transaction txn;
	V_snprintf(query, sizeof(query), sql_times_insert, steamID, courseID, modeID, styleIDs, time, metadata.data());
	txn.queries.push_back(query);
	if (styleIDs != 0)
	{
		SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, OnGenericTxnSuccess, OnGenericTxnFailure);
	}
	else
	{
		// Get Top 2 PBs
		V_snprintf(query, sizeof(query), sql_getpb, courseID, steamID, modeID, styleIDs, 2);
		txn.queries.push_back(query);
		// Get Rank
		V_snprintf(query, sizeof(query), sql_getmaprank, courseID, modeID, steamID, courseID, modeID);
		txn.queries.push_back(query);
		// Get Number of Players with Times
		V_snprintf(query, sizeof(query), sql_getlowestmaprank, courseID, modeID);
		txn.queries.push_back(query);

		SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
	}
}
