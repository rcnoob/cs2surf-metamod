
#pragma once

#define SURF_MAPPING_INTERFACE "SurfMappingInterface"

#define SURF_NO_MAPAPI_VERSION           0
#define SURF_NO_MAPAPI_COURSE_DESCRIPTOR "Default"
#define SURF_NO_MAPAPI_COURSE_NAME       "Main"

#define SURF_MAPAPI_VERSION 2

#define SURF_MAX_CHECKPOINT_ZONES   100
#define SURF_MAX_STAGE_ZONES        100
#define SURF_MAX_COURSE_COUNT       128
#define SURF_MAX_COURSE_NAME_LENGTH 65

#define INVALID_CHECKPOINT_NUMBER 0
#define INVALID_STAGE_NUMBER      0
#define INVALID_COURSE_NUMBER     0
#define INVALID_MAXVEL_NUMBER     0

struct SurfCourse;
class SurfPlayer;

enum SurfTriggerType
{
	SURFTRIGGER_DISABLED = 0,
	SURFTRIGGER_MODIFIER,

	SURFTRIGGER_ZONE_START,
	SURFTRIGGER_ZONE_END,
	SURFTRIGGER_ZONE_CHECKPOINT,
	SURFTRIGGER_ZONE_STAGE,
	SURFTRIGGER_ZONE_BONUS_START,
	SURFTRIGGER_ZONE_BONUS_END,

	SURFTRIGGER_TELEPORT,

	SURFTRIGGER_DESTINATION,

	SURFTRIGGER_PUSH,
	SURFTRIGGER_COUNT,
};

// SURFTRIGGER_MODIFIER
struct SurfMapModifier
{
	f32 gravity;
	f32 jumpFactor;
};

// SURFTRIGGER_ZONE_*
struct SurfMapZone
{
	char courseDescriptor[128];
	i32 number; // not used on start/end zones
	i32 bonus;
};

// SURFTRIGGER_TELEPORT
struct SurfMapTeleport
{
	char destination[128];
	char landmark[128];
	f32 delay;
	bool useDestinationAngles;
	bool resetSpeed;
	bool reorientPlayer;
	bool relative;
};

// SURFTRIGGER_PUSH
struct SurfMapPush
{
	// Cannot use Vector here as it is not a POD type.
	f32 impulse[3];

	enum SurfMapPushCondition : u32
	{
		SURF_PUSH_START_TOUCH = 1,
		SURF_PUSH_TOUCH = 2,
		SURF_PUSH_END_TOUCH = 4,
		SURF_PUSH_JUMP_EVENT = 8,
		SURF_PUSH_JUMP_BUTTON = 16,
		SURF_PUSH_ATTACK = 32,
		SURF_PUSH_ATTACK2 = 64,
		SURF_PUSH_USE = 128,
		SURF_PUSH_LEGACY = 256,
	};

	u32 pushConditions;
	bool setSpeed[3];
	bool cancelOnTeleport;
	f32 cooldown;
	f32 delay;
	int speed;
};

struct SurfCourseDescriptor

{
	SurfCourseDescriptor(i32 hammerId = -1, const char *targetName = "", u32 guid = 0, i32 courseID = INVALID_COURSE_NUMBER,
						 const char *courseName = "", u32 courseMaxVelocity = INVALID_MAXVEL_NUMBER)
		: hammerId(hammerId), guid(guid), id(courseID), maxVel(courseMaxVelocity)
	{
		V_snprintf(entityTargetname, sizeof(entityTargetname), "%s", targetName);
		V_snprintf(name, sizeof(name), "%s", courseName);
	}

	char entityTargetname[128] {};
	i32 hammerId = -1;

	bool hasStartPosition = false;
	Vector startPosition;
	QAngle startAngles;

	void SetStartPosition(Vector origin, QAngle angles)
	{
		hasStartPosition = true;
		startPosition = origin;
		startAngles = angles;
	}

	bool hasEndPosition = false;
	Vector endPosition;
	QAngle endAngles;

	i32 checkpointCount {};
	i32 stageCount {};

	// Shared identifiers
	u32 guid {};

	// Mapper assigned course ID.
	i32 id;
	// Mapper assigned course name.
	char name[SURF_MAX_COURSE_NAME_LENGTH] {};
	// Mapper assigned course max velocity.
	u32 maxVel;

	bool HasMatchingIdentifiers(i32 id, const char *name) const
	{
		return this->id == id && (!V_stricmp(this->name, name));
	}

	std::string GetName() const
	{
		return name;
	}

	// ID used for local database.
	u32 localDatabaseID {};

	// ID used for global database.
	u32 globalDatabaseID {};
};

struct SurfTrigger
{
	SurfTriggerType type;
	CEntityHandle entity;
	i32 hammerId;
	Vector mins;
	Vector maxs;
	Vector origin;
	QAngle rotation;

	union
	{
		SurfMapModifier modifier;
		SurfMapZone zone;
		SurfMapTeleport teleport;
		SurfMapPush push;
	};
};

namespace Surf::mapapi
{
	// These namespace'd functions are called when relevant game events happen, and are somewhat in order.
	void Init();
	void OnCreateLoadingSpawnGroupHook(const CUtlVector<const CEntityKeyValues *> *pKeyValues);
	void OnSpawn(int count, const EntitySpawnInfo_t *info);
	void OnRoundPreStart();
	void OnRoundStart();
	const SurfTrigger *IsPositionInOrAboveTimerZone(const Vector &position);

	void CheckEndTimerTrigger(CBaseTrigger *trigger);
	// This is const, unlike the trigger returned from Mapi_FindSurfTrigger.
	const SurfTrigger *GetSurfTrigger(CBaseTrigger *trigger);
	const SurfTrigger *GetSurfDestination(CBaseEntity *entity);

	const SurfCourseDescriptor *GetCourseDescriptorFromTrigger(CBaseTrigger *trigger);
	const SurfCourseDescriptor *GetCourseDescriptorFromTrigger(const SurfTrigger *trigger);

	inline bool IsTimerTrigger(SurfTriggerType triggerType)
	{
		static_assert(SURFTRIGGER_ZONE_START == 2 && SURFTRIGGER_ZONE_BONUS_END == 7,
					  "Don't forget to change this function when changing the SurfTriggerType enum!!!");
		return triggerType >= SURFTRIGGER_ZONE_START && triggerType <= SURFTRIGGER_ZONE_BONUS_END;
	}

	inline bool IsPushTrigger(SurfTriggerType triggerType)
	{
		return triggerType == SURFTRIGGER_PUSH;
	}

	inline bool IsTeleportTrigger(SurfTriggerType triggerType)
	{
		return triggerType == SURFTRIGGER_TELEPORT;
	}
} // namespace Surf::mapapi

// Exposed interface to modes.
class MappingInterface
{
public:
	virtual bool IsTriggerATimerZone(CBaseTrigger *trigger);
	bool GetJumpstatArea(Vector &pos, QAngle &angles);
};

extern MappingInterface *g_pMappingApi;

namespace Surf::course
{
	// Clear the list of current courses.
	void ClearCourses();

	// Get the number of courses on this map.
	u32 GetCourseCount();

	// Get a course's information given its map-defined course id.
	const SurfCourseDescriptor *GetCourseByCourseID(i32 id);

	// Get a course's information given its local course id.
	const SurfCourseDescriptor *GetCourseByLocalCourseID(u32 id);

	// Get a course's information given its global course id.
	const SurfCourseDescriptor *GetCourseByGlobalCourseID(u32 id);

	// Get a course's information given its name.
	const SurfCourseDescriptor *GetCourse(const char *courseName, bool caseSensitive = true);

	// Get a course's information given its GUID.
	const SurfCourseDescriptor *GetCourse(u32 guid);

	// Get the first course's information sorted by map-defined ID.
	const SurfCourseDescriptor *GetFirstCourse();

	// Setup all the courses to the local database.
	void SetupLocalCourses();

	// Update the course's database ID given its name.
	bool UpdateCourseLocalID(const char *courseName, u32 databaseID);

	// Update the course's global ID given its map-defined name and ID.
	bool UpdateCourseGlobalID(const char *courseName, u32 globalID);

}; // namespace Surf::course
