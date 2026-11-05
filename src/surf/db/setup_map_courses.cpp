#include "surf_db.h"

#include "queries/courses.h"

#include "vendor/sql_mm/src/public/sql_mm.h"

using namespace Surf::Database;

static_global bool coursesSetUp = false;

bool SurfDatabaseService::AreCoursesSetUp()
{
	return coursesSetUp;
}

void SurfDatabaseService::SetupCourses(CUtlVector<SurfCourseDescriptor *> &courses)
{
	char query[2048];
	Transaction txn;
	FOR_EACH_VEC(courses, i)
	{
		SurfCourseDescriptor *course = courses[i];
		std::string cleanCourseName = SurfDatabaseService::GetDatabaseConnection()->Escape(course->GetName().c_str());
		switch (databaseType)
		{
			case DatabaseType::SQLite:
			{
				V_snprintf(query, sizeof(query), sqlite_mapcourses_insert, SurfDatabaseService::GetMapID(), cleanCourseName.c_str(), course->id);
				txn.queries.push_back(query);
				break;
			}
			case DatabaseType::MySQL:
			{
				V_snprintf(query, sizeof(query), mysql_mapcourses_insert, SurfDatabaseService::GetMapID(), cleanCourseName.c_str(), course->id);
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
	}
	V_snprintf(query, sizeof(query), sql_mapcourses_findall, SurfDatabaseService::GetMapID());
	txn.queries.push_back(query);
	// clang-format off
	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(
		txn,
		[](std::vector<ISQLQuery *> queries) 
		{
			auto resultSet = queries.back()->GetResultSet();
			while (resultSet->FetchRow())
			{
				const char* name = resultSet->GetString(0);
				if (Surf::course::UpdateCourseLocalID(name, resultSet->GetInt(1)))
				{
					SURF_LOG_INFO(LogChannel::DB, "Course '%s' registered with ID %i\n", name, resultSet->GetInt(1));
				}
				else
				{
					SURF_LOG_INFO(LogChannel::DB, "Warning: Course '%s' with ID %i has no ingame course registered!\n", name, resultSet->GetInt(1));
				}
			}
			coursesSetUp = true;
		},
		OnGenericTxnFailure);
	// clang-format on
}
