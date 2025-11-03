#include "surf/surf.h"
#include "surf/trigger/surf_trigger.h"
#include "sdk/entity/cbasemodelentity.h"

extern SurfZoneBeamService *g_pSurfZoneBeamService;

struct SurfZoneBeamSet
{
	CHandle<CBeam> beams[12];
};

class SurfZoneBeamService : public SurfBaseService
{
public:
	using SurfBaseService::SurfBaseService;

	CUtlVector<SurfZoneBeamSet> startZoneBeams;
	CUtlVector<SurfZoneBeamSet> endZoneBeams;

	static void Init();

	void AddZone(SurfTrigger *trigger);
	void CreateZoneOutlineBeams(SurfTrigger *trigger, SurfZoneBeamSet &beamSet, Color color);
	CHandle<CBeam> CreatePersistentBeam(const Vector &start, const Vector &end, Color color);
};
