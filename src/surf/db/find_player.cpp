#include "surf_db.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

#include "queries/players.h"

void SurfDatabaseService::FindPlayerByAlias(const std::string &playerName, TransactionSuccessCallbackFunc onSuccess,
											TransactionFailureCallbackFunc onFailure)
{
	if (!SurfDatabaseService::IsReady())
	{
		return;
	}

	Transaction txn;
	char query[2048];

	// Get player's steamID through their alias.
	std::string cleanedPlayerName = SurfDatabaseService::GetDatabaseConnection()->Escape(playerName.c_str());
	V_snprintf(query, sizeof(query), sql_players_searchbyalias, cleanedPlayerName.c_str(), cleanedPlayerName.c_str());
	txn.queries.push_back(query);

	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}
