#include "surf_db.h"
#include "surf/option/surf_option.h"

#include "vendor/sql_mm/src/public/sql_mm.h"

#include "queries/players.h"

void SurfDatabaseService::SavePrefs(const std::string &prefs)
{
	if (!SurfDatabaseService::IsReady() || !this->IsSetup())
	{
		return;
	}
	u64 steamID64 = this->player->GetSteamId64();
	std::string cleanedPrefs = SurfDatabaseService::GetDatabaseConnection()->Escape(prefs.c_str());

	Transaction txn;

	std::string query = tfm::format(sql_players_set_prefs, cleanedPrefs.c_str(), steamID64);

	txn.queries.push_back(query.c_str());

	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, OnGenericTxnSuccess, OnGenericTxnFailure);
}
