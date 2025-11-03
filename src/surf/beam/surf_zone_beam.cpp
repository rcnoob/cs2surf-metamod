#include "surf_zone_beam.h"
#include "surf/surf.h"
#include "surf/trigger/surf_trigger.h"
#include "sdk/entity/cbasemodelentity.h"

SurfZoneBeamService *g_pSurfZoneBeamService = nullptr;

void SurfZoneBeamService::Init()
{
	if (!g_pSurfZoneBeamService)
	{
		g_pSurfZoneBeamService = new SurfZoneBeamService(nullptr);
	}
}

void SurfZoneBeamService::AddZone(SurfTrigger *trigger)
{
	if (trigger->type == SURFTRIGGER_ZONE_START)
	{
		SurfZoneBeamSet beamSet;
		CreateZoneOutlineBeams(trigger, beamSet, Color(0, 255, 0, 255));
		startZoneBeams.AddToHead(beamSet);
	}
	else if (trigger->type == SURFTRIGGER_ZONE_END)
	{
		SurfZoneBeamSet beamSet;
		CreateZoneOutlineBeams(trigger, beamSet, Color(255, 0, 0, 255));
		endZoneBeams.AddToHead(beamSet);
	}
}

void SurfZoneBeamService::CreateZoneOutlineBeams(SurfTrigger *trigger, SurfZoneBeamSet &beamSet, Color color)
{
	Vector mins = trigger->mins;
	Vector maxs = trigger->maxs;

	Vector corners[8] = {Vector(mins.x, mins.y, mins.z), Vector(maxs.x, mins.y, mins.z), Vector(maxs.x, maxs.y, mins.z),
						 Vector(mins.x, maxs.y, mins.z), Vector(mins.x, mins.y, maxs.z), Vector(maxs.x, mins.y, maxs.z),
						 Vector(maxs.x, maxs.y, maxs.z), Vector(mins.x, maxs.y, maxs.z)};

	int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

	for (int i = 0; i < 12; i++)
	{
		Vector start = corners[edges[i][0]];
		Vector end = corners[edges[i][1]];
		beamSet.beams[i] = CreatePersistentBeam(start, end, color);
	}
}

CHandle<CBeam> SurfZoneBeamService::CreatePersistentBeam(const Vector &start, const Vector &end, Color color)
{
	CBeam *beam = utils::CreateEntityByName<CBeam>("beam");
	if (!beam)
	{
		return CHandle<CBeam>();
	}

	beam->Teleport(&start, nullptr, nullptr);

	beam->m_clrRender(color);
	beam->m_fWidth(1.0f);
	beam->m_vecEndPos(end);
	beam->m_fadeMinDist(-1.0f);

	beam->DispatchSpawn();

	return beam->m_pEntity->m_EHandle;
}
