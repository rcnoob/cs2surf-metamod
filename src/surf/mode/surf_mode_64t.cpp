#include "surf_mode_64t.h"

#define MODE_NAME_SHORT "64t"
#define MODE_NAME       "64tick"

void Surf64tModeService::Reset()
{
	this->hasValidDesiredViewAngle = {};
	this->lastValidDesiredViewAngle = vec3_angle;
	this->oldDuckPressed = {};
	this->forcedUnduck = {};
	this->postProcessMovementZSpeed = {};

	this->angleHistory.RemoveAll();
	this->leftPreRatio = {};
	this->rightPreRatio = {};
	this->bonusSpeed = {};
	this->maxPre = {};

	this->inTryPlayerMove = {};
	this->lastPlane = vec3_origin;
	this->stuckTraceCount = {};

	this->airMoving = {};
	this->tpmTriggerFixOrigins.RemoveAll();
}

void Surf64tModeService::Cleanup()
{
	auto pawn = this->player->GetPlayerPawn();
	if (pawn)
	{
		pawn->m_flVelocityModifier(1.0f);
	}
}

const char *Surf64tModeService::GetModeName()
{
	return MODE_NAME;
}

const char *Surf64tModeService::GetModeShortName()
{
	return MODE_NAME_SHORT;
}

const CVValue_t *Surf64tModeService::GetModeConVarValues()
{
	return modeCvarValues;
}

void Surf64tModeService::OnSetupMove(CUserCmd *pc)
{
	for (i32 j = 0; j < pc->mutable_base()->subtick_moves_size(); j++)
	{
		CSubtickMoveStep *subtickMove = pc->mutable_base()->mutable_subtick_moves(j);
		if (subtickMove->button() == IN_ATTACK || subtickMove->button() == IN_ATTACK2 || subtickMove->button() == IN_RELOAD)
		{
			continue;
		}
		float when = subtickMove->when();
		subtickMove->set_when(0);
	}
}
