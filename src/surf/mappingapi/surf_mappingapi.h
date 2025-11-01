
#pragma once

#define SURF_MAPPING_INTERFACE "SurfMappingInterface"

#define SURF_NO_MAPAPI_VERSION           0
#define SURF_NO_MAPAPI_COURSE_DESCRIPTOR "Default"
#define SURF_NO_MAPAPI_COURSE_NAME       "Main"

#define SURF_MAPAPI_VERSION 2

#define SURF_MAX_SPLIT_ZONES        100
#define SURF_MAX_CHECKPOINT_ZONES   100
#define SURF_MAX_STAGE_ZONES        100
#define SURF_MAX_COURSE_COUNT       128
#define SURF_MAX_COURSE_NAME_LENGTH 65

#define INVALID_SPLIT_NUMBER      0
#define INVALID_CHECKPOINT_NUMBER 0
#define INVALID_STAGE_NUMBER      0
#define INVALID_COURSE_NUMBER     0

struct SurfCourse;
class SurfPlayer;

enum SurfTriggerType
{
	SURFTRIGGER_DISABLED = 0,
	SURFTRIGGER_MODIFIER,
	SURFTRIGGER_RESET_CHECKPOINTS,
	SURFTRIGGER_SINGLE_BHOP_RESET,
	SURFTRIGGER_ANTI_BHOP,

	SURFTRIGGER_ZONE_START,
	SURFTRIGGER_ZONE_END,
	SURFTRIGGER_ZONE_SPLIT,
	SURFTRIGGER_ZONE_CHECKPOINT,
	SURFTRIGGER_ZONE_STAGE,

	SURFTRIGGER_TELEPORT,
	SURFTRIGGER_MULTI_BHOP,
	SURFTRIGGER_SINGLE_BHOP,
	SURFTRIGGER_SEQUENTIAL_BHOP,

	SURFTRIGGER_PUSH,
	SURFTRIGGER_COUNT,
};

// SURFTRIGGER_MODIFIER
struct SurfMapModifier
{
	bool disablePausing;
	bool disableCheckpoints;
	bool disableTeleports;
	bool disableJumpstats;
	bool enableSlide;
	f32 gravity;
	f32 jumpFactor;
	bool forceDuck;
	bool forceUnduck;
};

// SURFTRIGGER_ANTI_BHOP
struct SurfMapAntibhop
{
	f32 time;
};

// SURFTRIGGER_ZONE_*
struct SurfMapZone
{
	char courseDescriptor[128];
	i32 number; // not used on start/end zones
};

// SURFTRIGGER_TELEPORT/_MULTI_BHOP/_SINGLE_BHOP/_SEQUENTIAL_BHOP
struct SurfMapTeleport
{
	char destination[128];
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
	};

	u32 pushConditions;
	bool setSpeed[3];
	bool cancelOnTeleport;
	f32 cooldown;
	f32 delay;
};

struct SurfCourseDescriptor

{
	SurfCourseDescriptor(i32 hammerId = -1, const char *targetName = "", bool disableCheckpoints = false, u32 guid = 0,
						 i32 courseID = INVALID_COURSE_NUMBER, const char *courseName = "")
		: hammerId(hammerId), disableCheckpoints(disableCheckpoints), guid(guid), id(courseID)
	{
		V_snprintf(entityTargetname, sizeof(entityTargetname), "%s", targetName);
		V_snprintf(name, sizeof(name), "%s", courseName);
	}

	char entityTargetname[128] {};
	i32 hammerId = -1;
	bool disableCheckpoints = false;

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

	i32 splitCount {};
	i32 checkpointCount {};
	i32 stageCount {};

	// Shared identifiers
	u32 guid {};

	// Mapper assigned course ID.
	i32 id;
	// Mapper assigned course name.
	char name[SURF_MAX_COURSE_NAME_LENGTH] {};

	bool HasMatchingIdentifiers(i32 id, const char *name) const
	{
		return this->id == id && (!V_stricmp(this->name, name));
	}

	CUtlString GetName() const
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

	union
	{
		SurfMapModifier modifier;
		SurfMapAntibhop antibhop;
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

	void CheckEndTimerTrigger(CBaseTrigger *trigger);
	// This is const, unlike the trigger returned from Mapi_FindSurfTrigger.
	const SurfTrigger *GetSurfTrigger(CBaseTrigger *trigger);

	const SurfCourseDescriptor *GetCourseDescriptorFromTrigger(CBaseTrigger *trigger);
	const SurfCourseDescriptor *GetCourseDescriptorFromTrigger(const SurfTrigger *trigger);

	inline bool IsBhopTrigger(SurfTriggerType triggerType)
	{
		return triggerType == SURFTRIGGER_MULTI_BHOP || triggerType == SURFTRIGGER_SINGLE_BHOP || triggerType == SURFTRIGGER_SEQUENTIAL_BHOP;
	}

	inline bool IsTimerTrigger(SurfTriggerType triggerType)
	{
		static_assert(SURFTRIGGER_ZONE_START == 5 && SURFTRIGGER_ZONE_STAGE == 9,
					  "Don't forget to change this function when changing the SurfTriggerType enum!!!");
		return triggerType >= SURFTRIGGER_ZONE_START && triggerType <= SURFTRIGGER_ZONE_STAGE;
	}

	inline bool IsPushTrigger(SurfTriggerType triggerType)
	{
		return triggerType == SURFTRIGGER_PUSH;
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
