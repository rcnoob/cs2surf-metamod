constexpr char sql_getcoursetop[] = R"(
    SELECT t.ID, t.SteamID64, p.Alias, t.RunTime AS PBTime
        FROM Times t 
        INNER JOIN MapCourses mc ON mc.ID = t.MapCourseID 
        INNER JOIN Maps ON Maps.ID = mc.MapID
        INNER JOIN Players p ON p.SteamID64=t.SteamID64 
        LEFT OUTER JOIN Times t2 ON t2.SteamID64=t.SteamID64 
        AND t2.MapCourseID=t.MapCourseID AND t2.ModeID=t.ModeID
        AND t2.StyleIDFlags=t.StyleIDFlags AND t2.RunTime<t.RunTime 
        WHERE t2.ID IS NULL AND p.Cheater=0 AND Maps.Name='%s' AND mc.Name='%s' AND t.ModeID=%d AND t.StyleIDFlags=0
        ORDER BY PBTime ASC
        LIMIT %d
        OFFSET %d
)";

// Caching PBs

constexpr char sql_getsrs[] = R"(
    SELECT x.RunTime, x.MapCourseID, x.ModeID, t.Metadata
        FROM Times t
        INNER JOIN MapCourses mc ON mc.ID = t.MapCourseID
        INNER JOIN Maps m ON m.ID = mc.MapID
        INNER JOIN (
            SELECT MIN(t.RunTime) AS RunTime, t.MapCourseID, t.ModeID
                FROM Times t
                GROUP BY t.MapCourseID, t.ModeID
        ) x ON x.RunTime = t.RunTime AND x.MapCourseID = t.MapCourseID AND x.ModeID = t.ModeID
        WHERE m.Name = '%s'
)";
