#include "movement.h"

// used: sdk
#include <iostream>
#include <array>
// used: sdk entity
#include "../../sdk/entity.h"
// used: cusercmd
#include "../../sdk/datatypes/usercmd.h"

#include "../../sdk/interfaces/ccsgoinput.h"
#include "../../sdk/datatypes/qangle.h"
// used: convars
#include "../../core/convars.h"
#include "../../sdk/interfaces/ienginecvar.h"

// used: cheat variables
#include "../../core/variables.h"

// movement correction angles
static QAngle_t angCorrectionView = {};

void F::MISC::MOVEMENT::OnMove(CUserCmd* pCmd, CBaseUserCmdPB* pBaseCmd, CCSPlayerController* pLocalController, C_CSPlayerPawn* pLocalPawn)
{
	if (!pLocalController->IsPawnAlive())
		return;

	// check if player is in noclip or on ladder or in water
	if (const int32_t nMoveType = pLocalPawn->GetMoveType(); nMoveType == MOVETYPE_NOCLIP || nMoveType == MOVETYPE_LADDER || pLocalPawn->GetWaterLevel() >= WL_WAIST)
		return;

	BunnyHop(pCmd, pBaseCmd, pLocalPawn);
	AutoStrafe(pCmd, pBaseCmd, pLocalPawn, 0);

	// loop through all tick commands
	for (int nSubTick = 0; nSubTick < pCmd->csgoUserCmd.inputHistoryField.pRep->nAllocatedSize; nSubTick++)
	{
		CCSGOInputHistoryEntryPB* pInputEntry = pCmd->GetInputHistoryEntry(nSubTick);
		if (pInputEntry == nullptr)
			continue;

		// save view angles for movement correction
		angCorrectionView = pInputEntry->pViewAngles->angValue;

		// movement correction & anti-untrusted
		ValidateUserCommand(pCmd, pBaseCmd, pInputEntry);
	}
}

void F::MISC::MOVEMENT::BunnyHop(CUserCmd* pCmd, CBaseUserCmdPB* pUserCmd, C_CSPlayerPawn* pLocalPawn)
{
	if (!C_GET(bool, Vars.bAutoBHop) || CONVAR::sv_autobunnyhopping->value.i1)
		return;

	// update random seed
	//MATH::fnRandomSeed(pUserCmd->nRandomSeed);

	//// bypass of possible SMAC/VAC server anticheat detection
	//if (static bool bShouldFakeJump = false; bShouldFakeJump)
	//{
	//	pCmd->nButtons.nValue |= IN_JUMP;
	//	bShouldFakeJump = false;
	//}
	//// check is player want to jump
	//else if (pCmd->nButtons.nValue & IN_JUMP)
	//{
	//	// check is player on the ground
	//	if (pLocalPawn->GetFlags() & FL_ONGROUND)
	//		// note to fake jump at the next tick
	//		bShouldFakeJump = true;
	//	// check did random jump chance passed
	//	else if (MATH::fnRandomInt(0, 100) <= C_GET(int, Vars.nAutoBHopChance))
	//		pCmd->nButtons.nValue &= ~IN_JUMP;
	//}

	// im lazy so yea :D
	if (pLocalPawn->GetFlags() & FL_ONGROUND)
	{
		pUserCmd->pInButtonState->SetBits(EButtonStatePBBits::BUTTON_STATE_PB_BITS_BUTTONSTATE1);
		pUserCmd->pInButtonState->nValue &= ~IN_JUMP;
	}
}

void F::MISC::MOVEMENT::AutoStrafe(CUserCmd* pCmd, CBaseUserCmdPB* pUserCmd, C_CSPlayerPawn* pLocalPawn, int type)
{
	static uint64_t last_pressed = 0;
	static uint64_t last_buttons = 0;

	if (!C_GET(bool, Vars.bAutoStrafe))
		return;

	auto& cmd = I::Input->arrCommands[I::Input->nSequenceNumber % 150];
	bool strafe_assist = C_GET(bool, Vars.bAutoStrafeAssistance);
	const auto current_buttons = cmd.nButtons.nValue;
	auto yaw = MATH::normalize_yaw(pUserCmd->pViewAngles->angValue.y);

	const auto check_button = [&](const uint64_t button)
	{
		if (current_buttons & button && (!(last_buttons & button) || button & IN_MOVELEFT && !(last_pressed & IN_MOVERIGHT) || button & IN_MOVERIGHT && !(last_pressed & IN_MOVELEFT) || button & IN_FORWARD && !(last_pressed & IN_BACK) || button & IN_BACK && !(last_pressed & IN_FORWARD)))
		{
			if (strafe_assist)
			{
				if (button & IN_MOVELEFT)
					last_pressed &= ~IN_MOVERIGHT;
				else if (button & IN_MOVERIGHT)
					last_pressed &= ~IN_MOVELEFT;
				else if (button & IN_FORWARD)
					last_pressed &= ~IN_BACK;
				else if (button & IN_BACK)
					last_pressed &= ~IN_FORWARD;
			}

			last_pressed |= button;
		}
		else if (!(current_buttons & button))
			last_pressed &= ~button;
	};

	check_button(IN_MOVELEFT);
	check_button(IN_MOVERIGHT);
	check_button(IN_FORWARD);
	check_button(IN_BACK);

	last_buttons = current_buttons;

	const auto velocity = pLocalPawn->GetAbsVelocity();
	bool wasdstrafe = (C_GET(int, Vars.bAutoStrafeMode) == 0);
	bool viewanglestrafe = (C_GET(int, Vars.bAutoStrafeMode) == 1);
	float smoothing = (C_GET(float, Vars.flAutoStrafeSmooth));

	/*const auto weapon = pLocalPawn->get_weapon_services_ptr()->get_h_active_weapon().get();
	const auto js = weapon && (cfg.weapon_config.is_scout && cfg.weapon_config.cur.scout_jumpshot && pLocalPawn->get_vec_abs_velocity().length_2d() < 50.f);
	const auto throwing_nade = weapon && weapon->is_grenade() && ticks_to_time(local_player->get_tickbase()) >= weapon->get_throw_time() && weapon->get_throw_time() != 0.f;

	if (js)
		return;*/

	if (pLocalPawn->GetFlags() & FL_ONGROUND)
		return;

	auto rotate_movement = [](CUserCmd& cmd, float target_yaw)
	{
		auto pUserCmd = cmd.csgoUserCmd.pBaseCmd;

		const float rot = M_DEG2RAD(pUserCmd->pViewAngles->angValue.y - target_yaw);

		const float new_forward = std::cos(rot) * pUserCmd->flForwardMove - std::sin(rot) * pUserCmd->flSideMove;
		const float new_side = std::sin(rot) * pUserCmd->flForwardMove + std::cos(rot) * pUserCmd->flSideMove;

		cmd.nButtons.nValue &= ~(IN_BACK | IN_FORWARD | IN_MOVELEFT | IN_MOVERIGHT);
		pUserCmd->flForwardMove = MATH::clamp(new_forward, -1.f, 1.f);
		pUserCmd->flSideMove = MATH::clamp(new_side * -1.f, -1.f, 1.f);

		if (pUserCmd->flForwardMove > 0.f)
			cmd.nButtons.nValue |= IN_FORWARD;
		else if (pUserCmd->flForwardMove < 0.f)
			cmd.nButtons.nValue |= IN_BACK;

		if (pUserCmd->flSideMove > 0.f)
			cmd.nButtons.nValue |= IN_MOVELEFT;
		else if (pUserCmd->flSideMove < 0.f)
			cmd.nButtons.nValue |= IN_MOVERIGHT;
	};

	if (wasdstrafe)
	{
		auto offset = 0.f;
		if (last_pressed & IN_MOVELEFT)
			offset += 90.f;
		if (last_pressed & IN_MOVERIGHT)
			offset -= 90.f;
		if (last_pressed & IN_FORWARD)
			offset *= 0.5f;
		else if (last_pressed & IN_BACK)
			offset = -offset * 0.5f + 180.f;

		yaw += offset;

		pUserCmd->flForwardMove = 0.f;
		pUserCmd->flSideMove = 0.f;

		rotate_movement(cmd, MATH::normalize_yaw(yaw));

		if (!viewanglestrafe && offset == 0.f)
			return;
	}

	if (pUserCmd->flSideMove != 0.0f || pUserCmd->flForwardMove != 0.0f)
		return;

	auto velocity_angle = M_RAD2DEG(std::atan2f(velocity.y, velocity.x));
	if (velocity_angle < 0.0f)
		velocity_angle += 360.0f;

	if (velocity_angle < 0.0f)
		velocity_angle += 360.0f;

	velocity_angle -= floorf(velocity_angle / 360.0f + 0.5f) * 360.0f;

	const auto speed = velocity.Length2D();
	const auto ideal = MATH::clamp(M_RAD2DEG(std::atan2(15.f, speed)), 0.f, 45.f);

	const auto correct = (100.f - smoothing) * 0.02f * (ideal + ideal);

	pUserCmd->flForwardMove = 0.f;
	const auto velocity_delta = MATH::normalize_yaw(yaw - velocity_angle);

	/*if (throwing_nade && fabsf(velocity_delta) <=20.f)
	{
		auto &wish_angle = antiaim::wish_angles[globals::current_cmd->command_number % 150];
		wish_angle.y = math::normalize_yaw(yaw);
		globals::current_cmd->forwardmove = 450.f;

		antiaim::fix_movement(globals::current_cmd);
		return;
	}*/

	if (fabsf(velocity_delta) > 170.f && speed > 80.f || velocity_delta > correct && speed > 80.f)
	{
		yaw = correct + velocity_angle;
		pUserCmd->flSideMove = -1.f;
		rotate_movement(cmd, MATH::normalize_yaw(yaw));
		return;
	}
	const bool side_switch = I::Input->nSequenceNumber % 2 == 0;

	if (-correct <= velocity_delta || speed <= 80.f)
	{
		if (side_switch)
		{
			yaw = yaw - ideal;
			pUserCmd->flSideMove = -1.f;
		}
		else
		{
			yaw = ideal + yaw;
			pUserCmd->flSideMove = 1.f;
		}
		rotate_movement(cmd, MATH::normalize_yaw(yaw));
	}
	else
	{
		yaw = velocity_angle - correct;
		pUserCmd->flSideMove = 1.f;

		rotate_movement(cmd, MATH::normalize_yaw(yaw));
	}
}

void F::MISC::MOVEMENT::ValidateUserCommand(CUserCmd* pCmd, CBaseUserCmdPB* pUserCmd, CCSGOInputHistoryEntryPB* pInputEntry)
{
	if (pUserCmd == nullptr)
		return;

	// clamp angle to avoid untrusted angle
	if (C_GET(bool, Vars.bAntiUntrusted))
	{
		pInputEntry->SetBits(EInputHistoryBits::INPUT_HISTORY_BITS_VIEWANGLES);
		if (pInputEntry->pViewAngles->angValue.IsValid())
		{
			pInputEntry->pViewAngles->angValue.Clamp();
			pInputEntry->pViewAngles->angValue.z = 0.f;
		}
		else
		{
			pInputEntry->pViewAngles->angValue = {};
			L_PRINT(LOG_WARNING) << CS_XOR("view angles have a NaN component, the value is reset");
		}
	}

	MovementCorrection(pUserCmd, pInputEntry, angCorrectionView);

	// correct movement buttons while player move have different to buttons values
	// clear all of the move buttons states
	pUserCmd->pInButtonState->SetBits(EButtonStatePBBits::BUTTON_STATE_PB_BITS_BUTTONSTATE1);
	pUserCmd->pInButtonState->nValue &= (~IN_FORWARD | ~IN_BACK | ~IN_LEFT | ~IN_RIGHT);

	// re-store buttons by active forward/side moves
	if (pUserCmd->flForwardMove > 0.0f)
		pUserCmd->pInButtonState->nValue |= IN_FORWARD;
	else if (pUserCmd->flForwardMove < 0.0f)
		pUserCmd->pInButtonState->nValue |= IN_BACK;

	if (pUserCmd->flSideMove > 0.0f)
		pUserCmd->pInButtonState->nValue |= IN_RIGHT;
	else if (pUserCmd->flSideMove < 0.0f)
		pUserCmd->pInButtonState->nValue |= IN_LEFT;

	if (!pInputEntry->pViewAngles->angValue.IsZero())
	{
		const float flDeltaX = std::remainderf(pInputEntry->pViewAngles->angValue.x - angCorrectionView.x, 360.f);
		const float flDeltaY = std::remainderf(pInputEntry->pViewAngles->angValue.y - angCorrectionView.y, 360.f);

		float flPitch = CONVAR::m_pitch->value.fl;
		float flYaw = CONVAR::m_yaw->value.fl;

		float flSensitivity = CONVAR::sensitivity->value.fl;
		if (flSensitivity == 0.0f)
			flSensitivity = 1.0f;

		pUserCmd->SetBits(EBaseCmdBits::BASE_BITS_MOUSEDX);
		pUserCmd->nMousedX = static_cast<short>(flDeltaX / (flSensitivity * flPitch));

		pUserCmd->SetBits(EBaseCmdBits::BASE_BITS_MOUSEDY);
		pUserCmd->nMousedY = static_cast<short>(-flDeltaY / (flSensitivity * flYaw));
	}
}

void F::MISC::MOVEMENT::MovementCorrection(CBaseUserCmdPB* pUserCmd, CCSGOInputHistoryEntryPB* pInputEntry, const QAngle_t& angDesiredViewPoint)
{
	if (pUserCmd == nullptr)
		return;

	Vector_t vecForward = {}, vecRight = {}, vecUp = {};
	angDesiredViewPoint.ToDirections(&vecForward, &vecRight, &vecUp);

	// we don't attempt on forward/right roll, and on up pitch/yaw
	vecForward.z = vecRight.z = vecUp.x = vecUp.y = 0.0f;

	vecForward.NormalizeInPlace();
	vecRight.NormalizeInPlace();
	vecUp.NormalizeInPlace();

	Vector_t vecOldForward = {}, vecOldRight = {}, vecOldUp = {};
	pInputEntry->pViewAngles->angValue.ToDirections(&vecOldForward, &vecOldRight, &vecOldUp);

	// we don't attempt on forward/right roll, and on up pitch/yaw
	vecOldForward.z = vecOldRight.z = vecOldUp.x = vecOldUp.y = 0.0f;

	vecOldForward.NormalizeInPlace();
	vecOldRight.NormalizeInPlace();
	vecOldUp.NormalizeInPlace();

	const float flPitchForward = vecForward.x * pUserCmd->flForwardMove;
	const float flYawForward = vecForward.y * pUserCmd->flForwardMove;
	const float flPitchSide = vecRight.x * pUserCmd->flSideMove;
	const float flYawSide = vecRight.y * pUserCmd->flSideMove;
	const float flRollUp = vecUp.z * pUserCmd->flUpMove;

	// solve corrected movement speed
	pUserCmd->SetBits(EBaseCmdBits::BASE_BITS_FORWARDMOVE);
	pUserCmd->flForwardMove = vecOldForward.x * flPitchSide + vecOldForward.y * flYawSide + vecOldForward.x * flPitchForward + vecOldForward.y * flYawForward + vecOldForward.z * flRollUp;

	pUserCmd->SetBits(EBaseCmdBits::BASE_BITS_LEFTMOVE);
	pUserCmd->flSideMove = vecOldRight.x * flPitchSide + vecOldRight.y * flYawSide + vecOldRight.x * flPitchForward + vecOldRight.y * flYawForward + vecOldRight.z * flRollUp;

	pUserCmd->SetBits(EBaseCmdBits::BASE_BITS_UPMOVE);
	pUserCmd->flUpMove = vecOldUp.x * flYawSide + vecOldUp.y * flPitchSide + vecOldUp.x * flYawForward + vecOldUp.y * flPitchForward + vecOldUp.z * flRollUp;
}
