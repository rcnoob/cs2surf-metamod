/*
	Keeps track of course descriptors along with various types of triggers, applying effects to player when necessary.
*/

#include <string>
#include <regex>

#include "surf/surf.h"
#include "surf/mode/surf_mode.h"
#include "surf/trigger/surf_trigger.h"
#include "surf/beam/surf_zone_beam.h"
#include "movement/movement.h"
#include "surf_mappingapi.h"
#include "entity2/entitykeyvalues.h"
#include "sdk/entity/cbasetrigger.h"
#include "utils/ctimer.h"
#include "surf/db/surf_db.h"
#include "surf/language/surf_language.h"
#include "utils/simplecmds.h"
#include "tier0/utlsortvector.h"
#include <memory>

#include "tier0/memdbgon.h"

#define KEY_TRIGGER_TYPE         "timer_trigger_type"
#define KEY_IS_COURSE_DESCRIPTOR "timer_course_descriptor"

using namespace Surf::course;

class CourseLessFunc
{
public:
	bool Less(const SurfCourseDescriptor *src1, const SurfCourseDescriptor *src2, void *pCtx)
	{
		return src1->id < src2->id;
	}
};

enum
{
	MAPI_ERR_TOO_MANY_TRIGGERS = 1 << 0,
	MAPI_ERR_TOO_MANY_COURSES = 1 << 1,
};

static_global struct
{
	CUtlVectorFixed<SurfCourseDescriptor, SURF_MAX_COURSE_COUNT> courseDescriptors;
	i32 mapApiVersion;
	bool apiVersionLoaded;
	bool fatalFailure;

	CUtlVectorFixed<SurfTrigger, 2048> triggers;
	bool roundIsStarting;
	i32 errorFlags;
	i32 errorCount;
	char errors[32][256];

	bool hasJumpstatArea;
	Vector jumpstatAreaPos;
	QAngle jumpstatAreaAngles;
} g_mappingApi;

static_global u32 g_mapCfgMaxVelocity = 0;
static_global CTimer<> *g_errorTimer;
static_global const char *g_errorPrefix = "{darkred} ERROR: ";
static_global const char *g_triggerNames[] = {"Disabled",       "Modifier",        "Start zone", "End zone", "Bonus start zone",
											  "Bonus end zone", "Checkpoint zone", "Stage zone", "Teleport"};

static_function MappingInterface g_mappingInterface;

MappingInterface *g_pMappingApi = &g_mappingInterface;
static_global CUtlSortVector<SurfCourseDescriptor *, CourseLessFunc> g_sortedCourses(SURF_MAX_COURSE_COUNT, SURF_MAX_COURSE_COUNT);

// TODO: add error check to make sure a course has at least 1 start zone and 1 end zone

static_function void Mapi_Error(const char *format, ...)
{
	i32 errorIndex = g_mappingApi.errorCount;
	if (errorIndex >= SURF_ARRAYSIZE(g_mappingApi.errors))
	{
		return;
	}
	else if (errorIndex == SURF_ARRAYSIZE(g_mappingApi.errors) - 1)
	{
		snprintf(g_mappingApi.errors[errorIndex], sizeof(g_mappingApi.errors[errorIndex]), "Too many errors to list!");
		return;
	}

	va_list args;
	va_start(args, format);
	vsnprintf(g_mappingApi.errors[errorIndex], sizeof(g_mappingApi.errors[errorIndex]), format, args);
	va_end(args);

	g_mappingApi.errorCount++;
}

static_function f64 Mapi_PrintErrors()
{
	if (g_mappingApi.errorFlags & MAPI_ERR_TOO_MANY_TRIGGERS)
	{
		utils::CPrintChatAll("%sToo many Mapping API triggers! Maximum is %i!", g_errorPrefix, g_mappingApi.triggers.Count());
	}
	if (g_mappingApi.errorFlags & MAPI_ERR_TOO_MANY_COURSES)
	{
		utils::CPrintChatAll("%sToo many Courses! Maximum is %i!", g_errorPrefix, g_mappingApi.courseDescriptors.Count());
	}
	for (i32 i = 0; i < g_mappingApi.errorCount; i++)
	{
		utils::CPrintChatAll("%s%s", g_errorPrefix, g_mappingApi.errors[i]);
	}

	return 60.0;
}

static_function bool Mapi_CreateCourse(i32 courseNumber = 1, const char *courseName = SURF_NO_MAPAPI_COURSE_NAME, i32 hammerId = -1,
									   const char *targetName = SURF_NO_MAPAPI_COURSE_DESCRIPTOR, u32 courseMaxVelocity = INVALID_MAXVEL_NUMBER)
{
	// Make sure we don't exceed this ridiculous value.
	// If we do, it is most likely that something went wrong, or it is caused by the mapper.
	if (g_mappingApi.courseDescriptors.Count() >= SURF_MAX_COURSE_COUNT)
	{
		assert(0);
		Mapi_Error("Failed to register course name '%s' (hammerId %i): Too many courses!", courseName, hammerId);
		return false;
	}

	auto &currentCourses = g_mappingApi.courseDescriptors;
	FOR_EACH_VEC(currentCourses, i)
	{
		if (currentCourses[i].hammerId == hammerId)
		{
			// This should only happen during start/end zone backwards compat where hammer IDs are SURF_NO_MAPAPI_VERSION, so this is not an error.
			return false;
		}
		if (SURF_STREQI(targetName, currentCourses[i].entityTargetname))
		{
			Mapi_Error("Course descriptor '%s' already existed! (registered by Hammer ID %i)", targetName, currentCourses[i].hammerId);
			return false;
		}
	}
	u32 guid = (u32)g_mappingApi.courseDescriptors.Count() + 1;

	i32 index = g_mappingApi.courseDescriptors.AddToTail({hammerId, targetName, guid, courseNumber, courseName, courseMaxVelocity});
	g_sortedCourses.Insert(&g_mappingApi.courseDescriptors[index]);
	return true;
}

// Example keyvalues:
/*
	timer_anti_bhop_time: 0.2
	timer_teleport_relative: true
	timer_teleport_reorient_player: false
	timer_teleport_reset_speed: false
	timer_teleport_use_dest_angles: false
	timer_teleport_delay: 0
	timer_teleport_destination: landmark_teleport
	timer_zone_stage_number: 1
	timer_modifier_enable_slide: false
	timer_modifier_disable_jumpstats: false
	timer_modifier_disable_teleports: false
	timer_modifier_disable_checkpoints: false
	timer_modifier_disable_pause: false
	timer_trigger_type: 10
	wait: 1
	spawnflags: 4097
	StartDisabled: false
	useLocalOffset: false
	classname: trigger_multiple
	origin: 1792.000000 768.000000 -416.000000
	angles: 0.000000 0.000000 0.000000
	scales: 1.000000 1.000000 1.000000
	hammerUniqueId: 48
	model: maps\surf_mapping_api\entities\unnamed_48.vmdl
*/
static_function void Mapi_OnTriggerMultipleSpawn(const EntitySpawnInfo_t *info)
{
	const CEntityKeyValues *ekv = info->m_pKeyValues;
	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);
	Vector origin = ekv->GetVector("origin");

	SurfTriggerType type = (SurfTriggerType)ekv->GetInt(KEY_TRIGGER_TYPE, SURFTRIGGER_DISABLED);

	if (!g_mappingApi.roundIsStarting)
	{
		// Only allow triggers and zones that were spawned during the round start phase.
		return;
	}

	if (type < SURFTRIGGER_DISABLED || type >= SURFTRIGGER_COUNT)
	{
		assert(0);
		Mapi_Error("Trigger type %i is invalid and out of range (%i-%i) for trigger with Hammer ID %i, origin (%.0f %.0f %.0f)!", type,
				   SURFTRIGGER_DISABLED, SURFTRIGGER_COUNT - 1, hammerId, origin.x, origin.y, origin.z);
		return;
	}

	SurfTrigger trigger = {};
	trigger.type = type;
	trigger.hammerId = hammerId;
	trigger.entity = info->m_pEntity->GetRefEHandle();

	switch (type)
	{
		case SURFTRIGGER_MODIFIER:
		{
			trigger.modifier.gravity = ekv->GetFloat("timer_modifier_gravity", 1);
			trigger.modifier.jumpFactor = ekv->GetFloat("timer_modifier_jump_impulse", 1.0f);
		}
		break;
		case SURFTRIGGER_ZONE_START:
		case SURFTRIGGER_ZONE_END:
		case SURFTRIGGER_ZONE_BONUS_START:
		case SURFTRIGGER_ZONE_BONUS_END:
		case SURFTRIGGER_ZONE_CHECKPOINT:
		case SURFTRIGGER_ZONE_STAGE:
		{
			const char *courseDescriptor = ekv->GetString("timer_zone_course_descriptor");

			if (!courseDescriptor || !courseDescriptor[0])
			{
				Mapi_Error("Course descriptor targetname of %s trigger is empty! Hammer ID %i, origin (%.0f %.0f %.0f)", g_triggerNames[type],
						   hammerId, origin.x, origin.y, origin.z);
				assert(0);
				return;
			}

			snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), "%s", courseDescriptor);
			if (type == SURFTRIGGER_ZONE_CHECKPOINT)
			{
				trigger.zone.number = ekv->GetInt("timer_zone_checkpoint_number", INVALID_CHECKPOINT_NUMBER);

				if (trigger.zone.number <= INVALID_CHECKPOINT_NUMBER)
				{
					Mapi_Error("Checkpoint zone number \"%i\" is invalid! Hammer ID %i, origin (%.0f %.0f %.0f)", trigger.zone.number, hammerId,
							   origin.x, origin.y, origin.z);
					assert(0);
					return;
				}
			}
			else if (type == SURFTRIGGER_ZONE_STAGE)
			{
				trigger.zone.number = ekv->GetInt("timer_zone_stage_number", INVALID_STAGE_NUMBER);

				if (trigger.zone.number <= INVALID_STAGE_NUMBER)
				{
					Mapi_Error("Stage zone number \"%i\" is invalid! Hammer ID %i, origin (%.0f %.0f %.0f)", trigger.zone.number, hammerId, origin.x,
							   origin.y, origin.z);
					assert(0);
					return;
				}
			}
			else // Start/End zones
			{
				// Note: Triggers shouldn't be rotated most of the time anyway. If that ever happens for timer triggers, it's probably unintentional.
				QAngle angles = ekv->GetQAngle("angles");

				if (angles != vec3_angle)
				{
					Mapi_Error(
						"Warning: Unexpected rotation for timer trigger, some functionalities might not work properly! Hammer ID %i, origin (%.0f "
						"%.0f %.0f)",
						hammerId, origin.x, origin.y, origin.z);
				}
			}
		}
		break;
		case SURFTRIGGER_PUSH:
		{
			Vector impulse = ekv->GetVector("timer_push_amount");
			trigger.push.impulse[0] = impulse.x;
			trigger.push.impulse[1] = impulse.y;
			trigger.push.impulse[2] = impulse.z;
			trigger.push.setSpeed[0] = ekv->GetBool("timer_push_abs_speed_x");
			trigger.push.setSpeed[1] = ekv->GetBool("timer_push_abs_speed_y");
			trigger.push.setSpeed[2] = ekv->GetBool("timer_push_abs_speed_z");
			trigger.push.cancelOnTeleport = ekv->GetBool("timer_push_cancel_on_teleport");
			trigger.push.cooldown = ekv->GetFloat("timer_push_cooldown", 0.1f);
			trigger.push.delay = ekv->GetFloat("timer_push_delay", 0.0f);

			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_start_touch") ? SurfMapPush::SURF_PUSH_START_TOUCH : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_touch") ? SurfMapPush::SURF_PUSH_TOUCH : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_end_touch") ? SurfMapPush::SURF_PUSH_END_TOUCH : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_jump_event") ? SurfMapPush::SURF_PUSH_JUMP_EVENT : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_jump_button") ? SurfMapPush::SURF_PUSH_JUMP_BUTTON : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_attack") ? SurfMapPush::SURF_PUSH_ATTACK : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_attack2") ? SurfMapPush::SURF_PUSH_ATTACK2 : 0;
			trigger.push.pushConditions |= ekv->GetBool("timer_push_condition_use") ? SurfMapPush::SURF_PUSH_USE : 0;
		}
		break;
		case SURFTRIGGER_DISABLED:
		{
			// Check for pre-mapping api triggers for backwards compatibility.
			if (g_mappingApi.mapApiVersion == SURF_NO_MAPAPI_VERSION)
			{
				CUtlString triggerName = info->m_pEntity->m_name.String();
				std::string name(triggerName.Get());

				// START/END HOOKS
				if (name.find("timer_startzone") != std::string::npos || name.find("timer_endzone") != std::string::npos)
				{
					snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), SURF_NO_MAPAPI_COURSE_DESCRIPTOR);
					trigger.type = name.find("timer_startzone") != std::string::npos ? SURFTRIGGER_ZONE_START : SURFTRIGGER_ZONE_END;
				}
				else if (name.find("map_start") != std::string::npos || name.find("map_end") != std::string::npos)
				{
					snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), SURF_NO_MAPAPI_COURSE_DESCRIPTOR);
					trigger.type = name.find("map_start") != std::string::npos ? SURFTRIGGER_ZONE_START : SURFTRIGGER_ZONE_END;
				}

				// STAGE HOOK
				std::regex stagePattern(R"((?:s|stage)(\d+)_start)");
				std::smatch match;
				int stageNum = 0;

				if (std::regex_search(name, match, stagePattern))
				{
					stageNum = std::stoi(match.str(1));
				}

				if (stageNum != 0)
				{
					snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), SURF_NO_MAPAPI_COURSE_DESCRIPTOR);

					if (stageNum == 1)
					{
						trigger.type = SURFTRIGGER_ZONE_START;
						trigger.zone.number = 1;
					}
					else
					{
						trigger.type = SURFTRIGGER_ZONE_STAGE;
						trigger.zone.number = stageNum;
					}
				}

				// CHECKPOINT HOOK
				std::regex cpPattern(R"((?:map_cp|map_checkpoint)(\d+))");
				int cpNum = 0;

				if (std::regex_search(name, match, cpPattern))
				{
					cpNum = std::stoi(match.str(1));
					snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), SURF_NO_MAPAPI_COURSE_DESCRIPTOR);
					trigger.type = SURFTRIGGER_ZONE_CHECKPOINT;
					trigger.zone.number = cpNum;
				}

				// BONUS HOOK
				std::string bonusName = "B";
				char bonusDescriptor[128];
				bool isBonusStart = false;
				bool isBonusEnd = false;

				std::regex bStartPattern(R"((?:b|bonus)(\d+)_start)");
				std::regex bEndPattern(R"((?:b|bonus)(\d+)_end)");
				int bonusNum = 0;

				if (std::regex_search(name, match, bStartPattern))
				{
					bonusNum = std::stoi(match.str(1));
					bonusName.append(match.str(1));

					snprintf(bonusDescriptor, sizeof(bonusDescriptor), "B%d", bonusNum);
					Mapi_CreateCourse(bonusNum + 1, bonusName.c_str(), g_mappingApi.courseDescriptors.Count() + 1, bonusDescriptor,
									  g_mapCfgMaxVelocity);

					isBonusStart = true;
				}

				if (std::regex_search(name, match, bEndPattern))
				{
					bonusNum = std::stoi(match.str(1));
					snprintf(bonusDescriptor, sizeof(bonusDescriptor), "B%d", bonusNum);

					isBonusEnd = true;
				}

				if (isBonusStart || isBonusEnd)
				{
					snprintf(trigger.zone.courseDescriptor, sizeof(trigger.zone.courseDescriptor), "%s", bonusDescriptor);
					trigger.type = isBonusStart ? SURFTRIGGER_ZONE_BONUS_START : SURFTRIGGER_ZONE_BONUS_END;
					trigger.zone.bonus = bonusNum;
				}
			}
			break;
			// Otherwise these are just regular trigger_multiple.
		}
		default:
		{
			// technically impossible to happen, leave an assert here anyway for debug builds.
			assert(0);
			return;
		}
		break;
	}

	g_mappingApi.triggers.AddToTail(trigger);
}

static_function void Mapi_OnInfoTargetSpawn(const CEntityKeyValues *ekv)
{
	if (!ekv->GetBool(KEY_IS_COURSE_DESCRIPTOR))
	{
		return;
	}

	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);
	Vector origin = ekv->GetVector("origin");

	i32 courseNumber = ekv->GetInt("timer_course_number", INVALID_COURSE_NUMBER);
	u32 courseMaxVelocity = ekv->GetInt("timer_course_max_velocity", INVALID_MAXVEL_NUMBER);
	const char *courseName = ekv->GetString("timer_course_name");
	const char *targetName = ekv->GetString("targetname");
	constexpr static_persist const char *targetNamePrefix = "[PR#]";
	if (SURF_STREQLEN(targetName, targetNamePrefix, strlen(targetNamePrefix)))
	{
		targetName = targetName + strlen(targetNamePrefix);
	}

	if (courseNumber <= INVALID_COURSE_NUMBER)
	{
		Mapi_Error("Course number must be bigger than %i! Course descriptor Hammer ID %i, origin (%.0f %.0f %.0f)", INVALID_COURSE_NUMBER, hammerId,
				   origin.x, origin.y, origin.z);
		return;
	}

	if (courseMaxVelocity <= INVALID_MAXVEL_NUMBER)
	{
		Mapi_Error("Course max velocity must be more than %i! Course descriptor Hammer ID %i, origin (%.0f %.0f %.0f)", INVALID_MAXVEL_NUMBER,
				   hammerId, origin.x, origin.y, origin.z);
		return;
	}

	if (!courseName[0])
	{
		Mapi_Error("Course name is empty! Course number %i. Course descriptor Hammer ID %i, origin (%.0f %.0f %.0f)", courseNumber, hammerId,
				   origin.x, origin.y, origin.z);
		return;
	}

	if (!targetName[0])
	{
		Mapi_Error("Course targetname is empty! Course name \"%s\". Course number %i. Course descriptor Hammer ID %i, origin (%.0f %.0f %.0f)",
				   courseName, courseNumber, hammerId, origin.x, origin.y, origin.z);
		return;
	}

	Mapi_CreateCourse(courseNumber, courseName, hammerId, targetName, courseMaxVelocity);
}

static_function SurfTrigger *Mapi_FindSurfTrigger(CBaseTrigger *trigger)
{
	if (!trigger->m_pEntity)
	{
		return nullptr;
	}

	CEntityHandle triggerHandle = trigger->GetRefEHandle();
	if (!trigger || !triggerHandle.IsValid() || trigger->m_pEntity->m_flags & EF_IS_INVALID_EHANDLE)
	{
		return nullptr;
	}

	FOR_EACH_VEC(g_mappingApi.triggers, i)
	{
		if (triggerHandle == g_mappingApi.triggers[i].entity)
		{
			return &g_mappingApi.triggers[i];
		}
	}

	return nullptr;
}

static_function SurfTrigger *Mapi_FindSurfDestination(CBaseEntity *entity)
{
	if (!entity || !entity->m_pEntity)
	{
		return nullptr;
	}
	CEntityHandle entityHandle = entity->GetRefEHandle();
	if (!entityHandle.IsValid() || entity->m_pEntity->m_flags & EF_IS_INVALID_EHANDLE)
	{
		return nullptr;
	}
	int entityHammerId = atoi(entity->m_sUniqueHammerID.Get());
	FOR_EACH_VEC(g_mappingApi.triggers, i)
	{
		if (g_mappingApi.triggers[i].type == SURFTRIGGER_DESTINATION && entityHandle == g_mappingApi.triggers[i].entity)
		{
			return &g_mappingApi.triggers[i];
		}
	}
	return nullptr;
}

static_function SurfCourseDescriptor *Mapi_FindCourse(const char *targetname)
{
	SurfCourseDescriptor *result = nullptr;
	if (!targetname)
	{
		return result;
	}

	FOR_EACH_VEC(g_mappingApi.courseDescriptors, i)
	{
		if (SURF_STREQI(g_mappingApi.courseDescriptors[i].entityTargetname, targetname))
		{
			result = &g_mappingApi.courseDescriptors[i];
			break;
		}
	}

	return result;
}

static_function bool Mapi_SetStartPosition(const char *descriptorName, Vector origin, QAngle angles)
{
	SurfCourseDescriptor *desc = Mapi_FindCourse(descriptorName);

	if (!desc)
	{
		return false;
	}
	desc->SetStartPosition(origin, angles);
	return true;
}

static_function void Mapi_OnInfoTeleportDestinationSpawn(const EntitySpawnInfo_t *info)
{
	const CEntityKeyValues *ekv = info->m_pKeyValues;
	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);

	SurfTrigger trigger = {};
	trigger.type = SURFTRIGGER_DESTINATION;
	trigger.hammerId = hammerId;
	trigger.entity = info->m_pEntity->GetRefEHandle();

	g_mappingApi.triggers.AddToTail(trigger);
};

static_function void Mapi_OnTriggerTeleportSpawn(const EntitySpawnInfo_t *info)
{
	const CEntityKeyValues *ekv = info->m_pKeyValues;
	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);

	SurfTrigger trigger = {};
	trigger.type = SURFTRIGGER_TELEPORT;
	trigger.hammerId = hammerId;
	trigger.entity = info->m_pEntity->GetRefEHandle();

	const char *destination = ekv->GetString("target", "");
	const char *landmark = ekv->GetString("landmark", "");

	if (!destination || !destination[0])
	{
		SURF_LOG_WARN(LogChannel::MappingAPI, "Teleport trigger (hammerID %i) has no target specified!\n", hammerId);
		return;
	}
	snprintf(trigger.teleport.destination, sizeof(trigger.teleport.destination), "%s", destination);
	snprintf(trigger.teleport.landmark, sizeof(trigger.teleport.landmark), "%s", landmark);

	if (landmark && landmark[0])
	{
		trigger.teleport.relative = true;
		trigger.teleport.useDestinationAngles = false;
	}
	else
	{
		trigger.teleport.relative = false;
		trigger.teleport.useDestinationAngles = true;
	}

	g_mappingApi.triggers.AddToTail(trigger);
};

static_function void Mapi_OnTriggerPushSpawn(const EntitySpawnInfo_t *info)
{
	const CEntityKeyValues *ekv = info->m_pKeyValues;
	i32 hammerId = ekv->GetInt("hammerUniqueId", -1);
	QAngle pushDir = ekv->GetQAngle("pushdir", QAngle(0, 0, 0));
	int speed = ekv->GetInt("speed", 0);

	SurfTrigger trigger = {};
	trigger.type = SURFTRIGGER_PUSH;
	trigger.hammerId = hammerId;
	trigger.rotation = pushDir;

	trigger.push.pushConditions |= ekv->GetBool("triggeronstarttouch") ? SurfMapPush::SURF_PUSH_START_TOUCH : SurfMapPush::SURF_PUSH_TOUCH;
	trigger.push.pushConditions |= SurfMapPush::SURF_PUSH_LEGACY;
	trigger.push.impulse[0] = 0.0f;
	trigger.push.impulse[1] = 0.0f;
	trigger.push.impulse[2] = 0.0f;
	trigger.push.speed = speed;

	CBaseEntity *baseEntity = static_cast<CBaseEntity *>(info->m_pEntity->m_pInstance);
	if (baseEntity)
	{
		uint32 flags = baseEntity->m_fFlags;

		// Once Only (set velocity rather than apply acceleration) : [128]
		bool pushOnce = (flags & 128) != 0;

		trigger.push.setSpeed[0] = pushOnce ? true : false;
		trigger.push.setSpeed[1] = pushOnce ? true : false;
		trigger.push.setSpeed[2] = pushOnce ? true : false;
	}

	trigger.push.cancelOnTeleport = false;
	trigger.push.cooldown = 0.1f;
	trigger.push.delay = 0.0f;

	trigger.entity = info->m_pEntity->GetRefEHandle();

	g_mappingApi.triggers.AddToTail(trigger);
};

void Surf::mapapi::Init()
{
	// Not `= {}`: moving a CUtlVectorFixed doesn't compile in s2sdk (its fixed memory has no Swap).
	std::destroy_at(&g_mappingApi);
	new (&g_mappingApi) decltype(g_mappingApi)();

	g_errorTimer = g_errorTimer ? g_errorTimer : StartTimer(Mapi_PrintErrors, true);
}

void Surf::mapapi::OnCreateLoadingSpawnGroupHook(const CUtlVector<const CEntityKeyValues *> *pKeyValues)
{
	if (!pKeyValues)
	{
		return;
	}

	if (g_mappingApi.apiVersionLoaded)
	{
		return;
	}

	for (i32 i = 0; i < pKeyValues->Count(); i++)
	{
		auto ekv = (*pKeyValues)[i];

		if (!ekv)
		{
			continue;
		}
		const char *classname = ekv->GetString("classname");
		if (SURF_STREQI(classname, "worldspawn"))
		{
			// We only care about the first spawn group's worldspawn because the rest might use prefabs compiled outside of mapping API.
			g_mappingApi.apiVersionLoaded = true;
			g_mappingApi.mapApiVersion = ekv->GetInt("timer_mapping_api_version", SURF_NO_MAPAPI_VERSION);
			// NOTE(GameChaos): When a new mapping api version comes out, this will change
			//  for backwards compatibility.
			if (g_mappingApi.mapApiVersion == SURF_NO_MAPAPI_VERSION)
			{
				SURF_LOG_INFO(LogChannel::MappingAPI, "Map is not compiled with Mapping API. Reverting to default behavior.\n");

				g_mapCfgMaxVelocity = g_pSurfUtils->GetCurrentMapMaxVelocity();
				if (g_mapCfgMaxVelocity <= INVALID_MAXVEL_NUMBER)
				{
					g_mapCfgMaxVelocity = 3500;
				}

				// Manually create a SURF_NO_MAPAPI_COURSE_NAME course here because there shouldn't be any info_target_server_only around.
				Mapi_CreateCourse(1, SURF_NO_MAPAPI_COURSE_NAME, -1, SURF_NO_MAPAPI_COURSE_DESCRIPTOR, g_mapCfgMaxVelocity);
				break;
			}
			if (g_mappingApi.mapApiVersion != SURF_MAPAPI_VERSION)
			{
				Mapi_Error("FATAL. Mapping API version %i is invalid!", g_mappingApi.mapApiVersion);
				g_mappingApi.fatalFailure = true;
				return;
			}
			break;
		}
	}
	// Do a second pass for course descriptors.
	if (g_mappingApi.mapApiVersion != SURF_NO_MAPAPI_VERSION)
	{
		for (i32 i = 0; i < pKeyValues->Count(); i++)
		{
			auto ekv = (*pKeyValues)[i];

			if (!ekv)
			{
				continue;
			}
			const char *classname = ekv->GetString("classname");
			if (SURF_STREQI(classname, "info_target_server_only"))
			{
				Mapi_OnInfoTargetSpawn(ekv);
			}
		}
	}
}

void Surf::mapapi::OnSpawn(int count, const EntitySpawnInfo_t *info)
{
	if (!info || g_mappingApi.fatalFailure)
	{
		return;
	}

	for (i32 i = 0; i < count; i++)
	{
		auto ekv = info[i].m_pKeyValues;
#if 0
		// Debug print for all keyvalues
		FOR_EACH_ENTITYKEY(ekv, iter)
		{
			auto kv = ekv->GetKeyValue(iter);
			if (!kv)
			{
				continue;
			}
			CBufferStringGrowable<128> bufferStr;
			const char *key = ekv->GetEntityKeyId(iter).GetString();
			const char *value = kv->ToString(bufferStr);
			Msg("\t%s: %s\n", key, value);
		}
#endif

		if (!info[i].m_pEntity || !ekv || !info[i].m_pEntity->GetClassname())
		{
			continue;
		}
		const char *classname = info[i].m_pEntity->GetClassname();
		const char *targetname = ekv->GetString("targetname", "");
		if (SURF_STREQI(classname, "trigger_multiple"))
		{
			Mapi_OnTriggerMultipleSpawn(&info[i]);
		}
		else if (SURF_STREQI(classname, "info_teleport_destination"))
		{
			Mapi_OnInfoTeleportDestinationSpawn(&info[i]);
		}
		else if (SURF_STREQI(classname, "trigger_push"))
		{
			Mapi_OnTriggerPushSpawn(&info[i]);
		}
		else if (SURF_STREQI(classname, "trigger_teleport"))
		{
			Mapi_OnTriggerTeleportSpawn(&info[i]);
		}
		else if (SURF_STREQI(classname, "game_player_equip"))
		{
			// allow players to choose their equipment
			g_pSurfUtils->RemoveEntity(info[i].m_pEntity->m_pInstance);
		}
		else if (SURF_STREQI(classname, "logic_timer") && SURF_STREQI(targetname, "clash_ad_timer"))
		{
			// fuck gambling you slimy losers
			g_pSurfUtils->RemoveEntity(info[i].m_pEntity->m_pInstance);
		}
		else if (SURF_STREQI(classname, "func_brush") && SURF_STREQI(targetname, "clash_ad"))
		{
			g_pSurfUtils->RemoveEntity(info[i].m_pEntity->m_pInstance);
		}
	}

	if (g_mappingApi.fatalFailure)
	{
		g_mappingApi.triggers.RemoveAll();
		g_mappingApi.courseDescriptors.RemoveAll();
	}
}

void Surf::mapapi::OnRoundPreStart()
{
	g_mappingApi.triggers.RemoveAll();
	g_mappingApi.roundIsStarting = true;
}

void Surf::mapapi::OnRoundStart()
{
	Surf::course::SetupLocalCourses();

	g_mappingApi.roundIsStarting = false;
	FOR_EACH_VEC(g_mappingApi.courseDescriptors, courseInd)
	{
		//  Find the number of stage zones that a course has
		//  and make sure that they start from 1 and are consecutive by
		//  XORing the values with a consecutive 1...n sequence.
		//  https://florian.github.io/xor-trick/
		//  Handle checkpoints seperately to support route splits
		i32 stageXor = 0;
		i32 stageCount = 0;
		bool seenCp[SURF_MAX_CHECKPOINT_ZONES] = {};
		i32 cpCount = 0;
		bool invalid = false;

		SurfCourseDescriptor *courseDescriptor = &g_mappingApi.courseDescriptors[courseInd];
		FOR_EACH_VEC(g_mappingApi.triggers, i)
		{
			SurfTrigger *trigger = &g_mappingApi.triggers[i];
			if (!Surf::mapapi::IsTimerTrigger(trigger->type))
			{
				if (trigger->type == SURFTRIGGER_DESTINATION)
				{
					CBaseEntity *pEntity = reinterpret_cast<CBaseEntity *>(trigger->entity.Get());
					if (!pEntity)
					{
						continue;
					}

					Vector absOrigin = pEntity->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
					QAngle absRotation = pEntity->m_CBodyComponent()->m_pSceneNode()->m_angAbsRotation();
					trigger->origin = absOrigin;
					trigger->rotation = absRotation;

					continue;
				}
				continue;
			}

			if (!SURF_STREQ(trigger->zone.courseDescriptor, courseDescriptor->entityTargetname))
			{
				continue;
			}

			switch (trigger->type)
			{
				case SURFTRIGGER_ZONE_START:
				case SURFTRIGGER_ZONE_END:
				case SURFTRIGGER_ZONE_BONUS_START:
				case SURFTRIGGER_ZONE_BONUS_END:
				case SURFTRIGGER_ZONE_STAGE:
				{
					CBaseEntity *pEntity = reinterpret_cast<CBaseEntity *>(trigger->entity.Get());
					Vector absOrigin = pEntity->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
					QAngle absRotation = pEntity->m_CBodyComponent()->m_pSceneNode()->m_angRotation();
					Vector mins = pEntity->m_pCollision()->m_vecMins();
					Vector maxs = pEntity->m_pCollision()->m_vecMaxs();

					trigger->mins = mins;
					trigger->maxs = maxs;
					trigger->origin = absOrigin;
					trigger->rotation = absRotation;

					if (trigger->type == SURFTRIGGER_ZONE_STAGE)
					{
						stageXor ^= (++stageCount) ^ trigger->zone.number;
						break;
					}

					if (g_pSurfZoneBeamService)
					{
						g_pSurfZoneBeamService->AddZone(trigger);
					}
					break;
				}
				case SURFTRIGGER_ZONE_CHECKPOINT:
				{
					i32 num = trigger->zone.number;
					if (num < 1 || num > SURF_MAX_CHECKPOINT_ZONES)
					{
						Mapi_Error("Course \"%s\" Checkpoint zone has invalid number %d!", courseDescriptor->name, num);
						invalid = true;
						break;
					}
					seenCp[num] = true;
					if (num > cpCount)
					{
						cpCount = num;
					}
					break;
				}
			}
		}

		for (i32 n = 1; n <= cpCount; n++)
		{
			if (!seenCp[n])
			{
				Mapi_Error("Course \"%s\" Checkpoint zones aren't consecutive, missing checkpoint %d!", courseDescriptor->name, n);
				invalid = true;
				break;
			}
		}

		if (cpCount > SURF_MAX_CHECKPOINT_ZONES)
		{
			Mapi_Error("Course \"%s\" Too many checkpoint zones! Maximum is %i.", courseDescriptor->name, SURF_MAX_CHECKPOINT_ZONES);
			invalid = true;
		}

		if (stageCount > SURF_MAX_STAGE_ZONES)
		{
			Mapi_Error("Course \"%s\" Too many stage zones! Maximum is %i.", courseDescriptor->name, SURF_MAX_STAGE_ZONES);
			invalid = true;
		}

		if (invalid)
		{
			g_mappingApi.courseDescriptors.FastRemove(courseInd);
			courseInd--;
			break;
		}
		courseDescriptor->checkpointCount = cpCount;
		courseDescriptor->stageCount = stageCount;
	}
}

void Surf::mapapi::CheckEndTimerTrigger(CBaseTrigger *trigger)
{
	SurfTrigger *surfTrigger = Mapi_FindSurfTrigger(trigger);
	if (surfTrigger && (surfTrigger->type == SURFTRIGGER_ZONE_END || surfTrigger->type == SURFTRIGGER_ZONE_BONUS_END))
	{
		SurfCourseDescriptor *desc = Mapi_FindCourse(surfTrigger->zone.courseDescriptor);
		if (!desc)
		{
			return;
		}
		desc->hasEndPosition = utils::FindValidPositionForTrigger(trigger, desc->endPosition, desc->endAngles);
	}
}

const SurfTrigger *Surf::mapapi::IsPositionInOrAboveTimerZone(const Vector &position)
{
	FOR_EACH_VEC(g_mappingApi.triggers, i)
	{
		const SurfTrigger *trigger = &g_mappingApi.triggers[i];
		if (trigger->type != SURFTRIGGER_ZONE_START && trigger->type != SURFTRIGGER_ZONE_BONUS_START && trigger->type != SURFTRIGGER_ZONE_STAGE)
		{
			continue;
		}
		Vector mins = trigger->mins + trigger->origin;
		Vector maxs = trigger->maxs + trigger->origin;

		bool inHorizontalBounds = position.x >= mins.x && position.x <= maxs.x && position.y >= mins.y && position.y <= maxs.y;

		bool inVerticalBounds = position.z >= mins.z && position.z <= maxs.z;

		bool aboveStartZone = position.z > maxs.z && position.z <= maxs.z + 400.0f;

		if (inHorizontalBounds && (inVerticalBounds || aboveStartZone))
		{
			return trigger;
		}
	}
	return nullptr;
}

const SurfTrigger *Surf::mapapi::GetSurfTrigger(CBaseTrigger *trigger)
{
	return Mapi_FindSurfTrigger(trigger);
}

const SurfTrigger *Surf::mapapi::GetSurfDestination(CBaseEntity *entity)
{
	return Mapi_FindSurfDestination(entity);
}

const SurfCourseDescriptor *Surf::mapapi::GetCourseDescriptorFromTrigger(CBaseTrigger *trigger)
{
	SurfTrigger *surfTrigger = Mapi_FindSurfTrigger(trigger);
	if (!surfTrigger)
	{
		return nullptr;
	}
	return Surf::mapapi::GetCourseDescriptorFromTrigger(surfTrigger);
}

const SurfCourseDescriptor *Surf::mapapi::GetCourseDescriptorFromTrigger(const SurfTrigger *trigger)
{
	const SurfCourseDescriptor *course = nullptr;
	switch (trigger->type)
	{
		case SURFTRIGGER_ZONE_START:
		case SURFTRIGGER_ZONE_END:
		case SURFTRIGGER_ZONE_BONUS_START:
		case SURFTRIGGER_ZONE_BONUS_END:
		case SURFTRIGGER_ZONE_CHECKPOINT:
		case SURFTRIGGER_ZONE_STAGE:
		{
			course = Mapi_FindCourse(trigger->zone.courseDescriptor);
			if (!course)
			{
				Mapi_Error("%s: Couldn't find course descriptor from name \"%s\"! Trigger's Hammer Id: %i", g_errorPrefix,
						   trigger->zone.courseDescriptor, trigger->hammerId);
			}
		}
		break;
	}
	return course;
}

bool MappingInterface::IsTriggerATimerZone(CBaseTrigger *trigger)
{
	SurfTrigger *surfTrigger = Mapi_FindSurfTrigger(trigger);
	if (!surfTrigger)
	{
		return false;
	}
	return Surf::mapapi::IsTimerTrigger(surfTrigger->type);
}

bool MappingInterface::GetJumpstatArea(Vector &pos, QAngle &angles)
{
	if (g_mappingApi.hasJumpstatArea)
	{
		pos = g_mappingApi.jumpstatAreaPos;
		angles = g_mappingApi.jumpstatAreaAngles;
	}

	return g_mappingApi.hasJumpstatArea;
}

void Surf::course::ClearCourses()
{
	g_sortedCourses.RemoveAll();
	SurfTimerService::ClearRecordCache();
}

u32 Surf::course::GetCourseCount()
{
	return g_sortedCourses.Count();
}

const SurfCourseDescriptor *Surf::course::GetCourseByCourseID(i32 id)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->id == id)
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const SurfCourseDescriptor *Surf::course::GetCourseByLocalCourseID(u32 id)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->localDatabaseID == id)
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const SurfCourseDescriptor *Surf::course::GetCourseByGlobalCourseID(u32 id)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->globalDatabaseID == id)
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const SurfCourseDescriptor *Surf::course::GetCourse(const char *courseName, bool caseSensitive)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		const char *name = g_sortedCourses[i]->name;
		if (caseSensitive ? SURF_STREQ(name, courseName) : SURF_STREQI(name, courseName))
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const SurfCourseDescriptor *Surf::course::GetCourse(u32 guid)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->guid == guid)
		{
			return g_sortedCourses[i];
		}
	}
	return nullptr;
}

const SurfCourseDescriptor *Surf::course::GetFirstCourse()
{
	if (g_sortedCourses.Count() >= 1)
	{
		return g_sortedCourses[0];
	}
	return nullptr;
}

void Surf::course::SetupLocalCourses()
{
	if (SurfDatabaseService::IsMapSetUp())
	{
		SurfDatabaseService::SetupCourses(g_sortedCourses);
	}
}

bool Surf::course::UpdateCourseLocalID(const char *courseName, u32 databaseID)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->GetName() == courseName)
		{
			g_sortedCourses[i]->localDatabaseID = databaseID;
			return true;
		}
	}
	return false;
}

bool Surf::course::UpdateCourseGlobalID(const char *courseName, u32 globalID)
{
	FOR_EACH_VEC(g_sortedCourses, i)
	{
		if (g_sortedCourses[i]->GetName() == courseName)
		{
			g_sortedCourses[i]->globalDatabaseID = globalID;
			return true;
		}
	}
	return false;
}

static void ListCourses(SurfPlayer *player)
{
	if (player->timerService->GetCourse())
	{
		player->languageService->PrintChat(true, false, "Current Course", player->timerService->GetCourse()->name);
	}
	else
	{
		player->languageService->PrintChat(true, false, "No Current Course");
	}
	player->languageService->PrintConsole(false, false, "Course List Header");
	for (u32 i = 0; i < Surf::course::GetCourseCount(); i++)
	{
		player->PrintConsole(false, false, "%s", g_sortedCourses[i]->name);
	}
}

SCMD(surf_courses, SCFL_MAP)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	ListCourses(player);
	return true;
}

SCMD(surf_course, SCFL_MAP)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (args->ArgC() < 2)
	{
		ListCourses(player);
	}
	else
	{
		Surf::misc::HandleTeleportToCourse(player, args);
	}
	return true;
}
