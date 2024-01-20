﻿//
// Copyright (c) 2003-2009, by Yet Another POD-Bot Development Team.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// $Id:$
//
// Based on SyPB
//

#include <core.h>

// console variables
ConVar ebot_debug("ebot_debug", "0");
ConVar ebot_debuggoal("ebot_debug_goal", "-1");
ConVar ebot_gamemod("ebot_gamemode", "0");

ConVar ebot_followuser("ebot_follow_user_max", "2");
ConVar ebot_knifemode("ebot_knife_mode", "0");
ConVar ebot_walkallow("ebot_walk_allow", "1");
ConVar ebot_stopbots("ebot_stop_bots", "0");
ConVar ebot_restrictweapons("ebot_restrict_weapons", "");
ConVar ebot_camp_min("ebot_camp_time_min", "16");
ConVar ebot_camp_max("ebot_camp_time_max", "48");
ConVar ebot_use_radio("ebot_use_radio", "1");
ConVar ebot_force_flashlight("ebot_force_flashlight", "0");
ConVar ebot_use_flare("ebot_zm_use_flares", "1");
ConVar ebot_eco_rounds("ebot_eco_rounds", "1");

ConVar ebot_chatter_path("ebot_chatter_path", "radio/bot");
ConVar ebot_buy_weapons("ebot_buy_weapons", "1");
ConVar ebot_prefer_better_pickup("ebot_prefer_better_pickup", "1");
ConVar ebot_kill_breakables("ebot_kill_breakables", "0");

// this function get the current message from the bots message queue
int Bot::GetMessageQueue(void)
{
	const int message = m_messageQueue[m_actMessageIndex++];
	m_actMessageIndex &= 0x1f; // wraparound
	return message;
}

// this function put a message into the bot message queue
void Bot::PushMessageQueue(const int message)
{
	if (message == CMENU_SAY)
	{
		// notify other bots of the spoken text otherwise, bots won't respond to other bots (network messages aren't sent from bots)
		for (const auto& bot : g_botManager->m_bots)
		{
			if (bot != nullptr && bot != this)
			{
				if (m_isAlive == bot->m_isAlive)
				{
					bot->m_sayTextBuffer.entityIndex = m_index;
					cstrcpy(bot->m_sayTextBuffer.sayText, m_tempStrings);
				}
			}
		}
	}
	else if (message == CMENU_BUY && g_gameVersion == HALFLIFE)
	{
		m_buyingFinished = true;
		m_isVIP = false;
		m_buyState = 7;
	}

	m_messageQueue[m_pushMessageIndex++] = message;
	m_pushMessageIndex &= 0x1f; // wraparound
}

float Bot::InFieldOfView(const Vector destination)
{
	const float absoluteAngle = cabsf(AngleMod(pev->v_angle.y) - AngleMod(destination.ToYaw()));
	if (absoluteAngle > 180.0f)
		return 360.0f - absoluteAngle;
	return absoluteAngle;
}

bool Bot::IsInViewCone(const Vector origin)
{
	return ::IsInViewCone(origin, GetEntity());
}

bool Bot::CheckVisibility(edict_t* targetEntity)
{
	m_visibility = VISIBILITY_NONE;
	if (FNullEnt(targetEntity))
		return false;

	TraceResult tr{};
	const Vector eyes = EyePosition();
	Vector spot = targetEntity->v.origin;

	const float vis = 0.97f;
	bool ignoreGlass = true;

	// zombies can't hit from the glass...
	if (m_isZombieBot)
		ignoreGlass = false;

	TraceLine(eyes, spot, true, ignoreGlass, pev->pContainingEntity, &tr);

	if (tr.flFraction > vis || tr.pHit == targetEntity)
	{
		m_visibility |= VISIBILITY_BODY;
		m_enemyOrigin = spot;
	}

	// check top of head
	spot.z += 25.0f;
	TraceLine(eyes, spot, true, ignoreGlass, pev->pContainingEntity, &tr);

	if (tr.flFraction > vis || tr.pHit == targetEntity)
	{
		m_visibility |= VISIBILITY_HEAD;
		m_enemyOrigin = spot;
	}

	if (m_visibility != VISIBILITY_NONE)
		return true;

	if (targetEntity->v.flags & FL_DUCKING)
		spot.z = targetEntity->v.origin.z - 14.0f;
	else
		spot.z = targetEntity->v.origin.z - 34.0f;

	TraceLine(eyes, spot, true, ignoreGlass, pev->pContainingEntity, &tr);

	if (tr.flFraction > vis || tr.pHit == targetEntity)
	{
		m_visibility |= VISIBILITY_OTHER;
		m_enemyOrigin = spot;
		return true;
	}

	const Vector dir = (targetEntity->v.origin - pev->origin).Normalize2D();
	Vector perp(-dir.y, dir.x, 0.0f);
	spot = targetEntity->v.origin + Vector(perp.x * 13.0f, perp.y * 13.0f, 0);

	TraceLine(eyes, spot, true, ignoreGlass, pev->pContainingEntity, &tr);

	if (tr.flFraction > vis || tr.pHit == targetEntity)
	{
		m_visibility |= VISIBILITY_OTHER;
		m_enemyOrigin = spot;
		return true;
	}

	spot = targetEntity->v.origin - Vector(perp.x * 13.0f, perp.y * 13.0f, 0);
	TraceLine(eyes, spot, true, ignoreGlass, pev->pContainingEntity, &tr);

	if (tr.flFraction > vis || tr.pHit == targetEntity)
	{
		m_visibility |= VISIBILITY_OTHER;
		m_enemyOrigin = spot;
		return true;
	}

	return false;
}

bool Bot::IsEnemyViewable(edict_t* entity, const bool setEnemy, const bool checkOnly)
{
	if (FNullEnt(entity))
		return false;

	if (IsNotAttackLab(entity))
		return false;

	if (checkOnly)
		return CheckVisibility(entity);

	if (CheckVisibility(entity))
	{
		m_seeEnemyTime = engine->GetTime();
		SetLastEnemy(entity);
		return true;
	}

	return false;
}

bool Bot::ItemIsVisible(const Vector destination, char* itemName)//, bool bomb)
{
	TraceResult tr{};

	// trace a line from bot's eyes to destination..
	TraceLine(EyePosition(), destination, true, false, GetEntity(), &tr);

	// check if line of sight to object is not blocked (i.e. visible)
	if (tr.flFraction < 1.0f)
	{
		// check for standard items
		if (g_gameVersion == HALFLIFE)
		{
			if (tr.flFraction > 0.95f && cstrcmp(STRING(tr.pHit->v.classname), itemName) == 0)
				return true;
		}
		else
		{
			if (tr.flFraction > 0.97f && cstrcmp(STRING(tr.pHit->v.classname), itemName) == 0)
				return true;

			if (tr.flFraction > 0.95f && cstrncmp(STRING(tr.pHit->v.classname), "csdmw_", 6) == 0)
				return true;
		}

		return false;
	}

	return true;
}

bool Bot::EntityIsVisible(const Vector dest, const bool fromBody)
{
	TraceResult tr{};

	// trace a line from bot's eyes to destination...
	TraceLine(fromBody ? pev->origin - Vector(0.0f, 0.0f, 1.0f) : EyePosition(), dest, true, true, GetEntity(), &tr);

	// check if line of sight to object is not blocked (i.e. visible)
	return tr.flFraction >= 1.0f;
}

// this function controls zombie ai
void Bot::ZombieModeAi(void)
{
	// zombie bot slowdown fix
	if (pev->flags & FL_DUCKING)
	{
		pev->speed = pev->maxspeed;
		m_moveSpeed = pev->maxspeed;
	}

	if (!m_isSlowThink)
		return;

	extern ConVar ebot_random_join_quit;
	if (ebot_random_join_quit.GetBool() && m_stayTime > 0.0f && m_stayTime < engine->GetTime() && chanceof(25))
	{
		Kick();
		return;
	}

	edict_t* entity = nullptr;
	if (FNullEnt(m_enemy) && FNullEnt(m_moveTargetEntity))
	{
		extern ConVar ebot_escape;
		Bot* bot;
		edict_t* targetEnt = nullptr;
		float targetDistance = FLT_MAX;
		float distance;

		// zombie improve
		for (const auto& client : g_clients)
		{
			if (FNullEnt(client.ent))
				continue;

			if (!(client.flags & CFLAG_USED))
				continue;

			if (!(client.flags & CFLAG_ALIVE))
				continue;

			if (GetGameMode() == MODE_ZH || ebot_escape.GetBool())
				entity = client.ent;
			else
			{
				bot = g_botManager->GetBot(client.index);
				if (bot == nullptr || bot == this || !bot->m_isAlive)
					continue;

				if (bot->m_enemy == nullptr && bot->m_moveTargetEntity == nullptr)
					continue;

				entity = (bot->m_enemy == nullptr) ? bot->m_moveTargetEntity : bot->m_enemy;
				if (m_team == bot->m_team)
				{
					if (entity == targetEnt || m_team == GetTeam(entity))
						continue;
				}
				else
				{
					if (bot->GetEntity() == targetEnt || m_team != GetTeam(entity))
						continue;

					entity = bot->GetEntity();
				}
			}

			if (m_team == GetTeam(entity))
				continue;

			distance = (pev->origin - GetEntityOrigin(entity)).GetLengthSquared2D();
			if (distance < targetDistance)
			{
				targetDistance = distance;
				targetEnt = entity;
			}
		}

		if (!FNullEnt(targetEnt))
			SetMoveTarget(targetEnt);
	}
}

void Bot::ZmCampPointAction(const int mode)
{
	if (!IsZombieMode())
		return;

	if (m_isZombieBot)
		return;

	if (g_waypoint->m_zmHmPoints.IsEmpty())
		return;

	float campAction = 0.0f;
	int campPointWaypointIndex = -1;
	
	if (IsValidWaypoint(m_myMeshWaypoint) && g_waypoint->IsZBCampPoint(m_myMeshWaypoint))
	{
		if (IsValidWaypoint(m_currentWaypointIndex) && m_currentWaypointIndex != m_myMeshWaypoint)
			return;

		if (mode == 1)
		{
			campAction = crandomfloat(0.75f, 1.5f);
			campPointWaypointIndex = m_myMeshWaypoint;
		}
		else
		{
			campAction = 1.6f;
			campPointWaypointIndex = m_chosenGoalIndex;
		}
	}
	else if (IsValidWaypoint(m_currentWaypointIndex) && g_waypoint->IsZBCampPoint(m_currentWaypointIndex, false))
	{
		if (mode == 1)
		{
			campAction = 1.0f;
			campPointWaypointIndex = m_currentWaypointIndex;
		}
		else
		{
			campAction = 1.6f;
			campPointWaypointIndex = m_chosenGoalIndex;
		}
	}

	// wut?
	if (IsOnLadder() || (!IsOnFloor() && !IsInWater()))
		m_checkCampPointTime = engine->GetTime() + 0.5f;
	else if (campAction == 0.0f || !IsValidWaypoint(m_currentWaypointIndex))
		m_checkCampPointTime = 0.0f;
	else if (m_checkCampPointTime == 0.0f && campAction != 1.0f)
		m_checkCampPointTime = engine->GetTime() + campAction;
	else if (m_checkCampPointTime < engine->GetTime() || campAction == 1.0f || (IsValidWaypoint(m_myMeshWaypoint) && (g_waypoint->GetPath(m_myMeshWaypoint)->origin - pev->origin).GetLengthSquared() <= squaredf(24.0f)))
	{
		m_zhCampPointIndex = campPointWaypointIndex;

		SelectBestWeapon();
		MakeVectors(pev->v_angle);

		m_timeCamping = engine->GetTime() + 9999.0f;
		PushTask(TASK_CAMP, TASKPRI_CAMP, -1, m_timeCamping, true);

		m_aimFlags |= AIM_CAMP;

		m_moveToGoal = false;
		m_checkTerrain = false;

		m_moveSpeed = 0.0f;
		m_strafeSpeed = 0.0f;

		m_checkCampPointTime = 0.0f;
	}
}

bool Bot::IsBehindSmokeClouds(edict_t* ent)
{
	// in zombie mode, flares are counted as smoke and breaks the bot's vision
	if (IsZombieMode())
		return false;

	if (FNullEnt(ent))
		return false;

	edict_t* pentGrenade = nullptr;
	const Vector entOrigin = GetEntityOrigin(ent);
	const Vector betweenUs = (entOrigin - pev->origin).Normalize();
	Vector pentOrigin, betweenNade;

	while (!FNullEnt(pentGrenade = FIND_ENTITY_BY_CLASSNAME(pentGrenade, "grenade")))
	{
		// if grenade is invisible don't care for it
		if ((pentGrenade->v.effects & EF_NODRAW) || !(pentGrenade->v.flags & (FL_ONGROUND | FL_PARTIALGROUND)) || cstrcmp(STRING(pentGrenade->v.model) + 9, "smokegrenade.mdl"))
			continue;

		// check if visible to the bot
		if (InFieldOfView(entOrigin - EyePosition()) > pev->fov * 0.33333333333f && !EntityIsVisible(entOrigin))
			continue;

		pentOrigin = GetEntityOrigin(pentGrenade);
		betweenNade = (pentOrigin - pev->origin).Normalize();
		if ((betweenNade | betweenUs) > (betweenNade | ((Vector(betweenNade.y, betweenNade.x, 0.0f) * 150.0f + pentOrigin) - pev->origin).Normalize()))
			return true;
	}

	return false;
}

edict_t* Bot::FindSmokeGrenadeThere(const Vector origin, const float maxDistance)
{
	if (!IsZombieMode())
		return nullptr;

	edict_t* pentGrenade = nullptr;
	while (!FNullEnt(pentGrenade = FIND_ENTITY_BY_CLASSNAME(pentGrenade, "grenade")))
	{
		// if grenade is invisible don't care for it
		if (pentGrenade->v.effects & EF_NODRAW || !(pentGrenade->v.flags & (FL_ONGROUND | FL_PARTIALGROUND)) || cstrcmp(STRING(pentGrenade->v.model) + 9, "smokegrenade.mdl"))
			continue;

		if ((origin - pentGrenade->v.origin).GetLengthSquared() > maxDistance)
			continue;

		return pentGrenade;
	}

	return nullptr;
}

// this function returns the best weapon of this bot (based on personality prefs)
int Bot::GetBestWeaponCarried(void)
{
	int* ptr = g_weaponPrefs[m_personality];
	int weaponIndex = 0;
	int weapons = pev->weapons;

	WeaponSelect* weaponTab = &g_weaponSelect[0];

	// take the shield in account
	if (HasShield())
		weapons |= (1 << WEAPON_SHIELDGUN);

	int i;
	for (i = 0; i < Const_NumWeapons; i++)
	{
		if (weapons & (1 << weaponTab[*ptr].id))
			weaponIndex = i;

		ptr++;
	}

	return weaponIndex;
}

// this function returns the best secondary weapon of this bot (based on personality prefs)
int Bot::GetBestSecondaryWeaponCarried(void)
{
	int* ptr = g_weaponPrefs[m_personality];
	int weaponIndex = 0;
	int weapons = pev->weapons;

	// take the shield in account
	if (HasShield())
		weapons |= (1 << WEAPON_SHIELDGUN);

	WeaponSelect* weaponTab = &g_weaponSelect[0];

	int i, id;
	for (i = 0; i < Const_NumWeapons; i++)
	{
		id = weaponTab[*ptr].id;
		if ((weapons & (1 << weaponTab[*ptr].id)) && (id == WEAPON_USP || id == WEAPON_GLOCK18 || id == WEAPON_DEAGLE || id == WEAPON_P228 || id == WEAPON_ELITE || id == WEAPON_FN57))
		{
			weaponIndex = i;
			break;
		}

		ptr++;
	}

	return weaponIndex;
}

// this function compares weapons on the ground to the one the bot is using
bool Bot::RateGroundWeapon(edict_t* ent)
{
	if (FNullEnt(ent))
		return false;

	int hasWeapon = 0;
	int groundIndex = 0;
	int* ptr = g_weaponPrefs[m_personality];

	WeaponSelect* weaponTab = &g_weaponSelect[0];

	int i;
	for (i = 0; i < Const_NumWeapons; i++)
	{
		if (cstrcmp(weaponTab[*ptr].modelName, STRING(ent->v.model) + 9) == 0)
		{
			groundIndex = i;
			break;
		}

		ptr++;
	}

	if (IsRestricted(weaponTab[groundIndex].id) && HasPrimaryWeapon())
		return false;

	if (groundIndex < 7)
		hasWeapon = GetBestSecondaryWeaponCarried();
	else
		hasWeapon = GetBestWeaponCarried();

	return groundIndex > hasWeapon;
}

// this function checks buttons for use button waypoint
edict_t* Bot::FindButton(void)
{
	float distance;
	float nearestDistance = FLT_MAX;
	edict_t* searchEntity = nullptr, * foundEntity = nullptr;

	// find the nearest button which can open our target
	while (!FNullEnt(searchEntity = FIND_ENTITY_IN_SPHERE(searchEntity, pev->origin, 512.0f)))
	{
		if (cstrncmp("func_button", STRING(searchEntity->v.classname), 11) == 0 || cstrncmp("func_rot_button", STRING(searchEntity->v.classname), 15) == 0)
		{
			distance = (pev->origin - GetEntityOrigin(searchEntity)).GetLengthSquared();
			if (distance < nearestDistance)
			{
				nearestDistance = distance;
				foundEntity = searchEntity;
			}
		}
	}

	return foundEntity;
}

void Bot::FindItem(void)
{
	if (m_isZombieBot)
		return;

	if (IsZombieMode() && m_currentWeapon != WEAPON_KNIFE) // if we're holding knife, mostly our guns dont have a ammo
		return;

	if (GetCurrentTaskID() == TASK_ESCAPEFROMBOMB)
		return;

	if (GetCurrentTaskID() == TASK_PLANTBOMB)
		return;

	if (GetCurrentTaskID() == TASK_THROWHEGRENADE)
		return;

	if (GetCurrentTaskID() == TASK_THROWFBGRENADE)
		return;

	if (GetCurrentTaskID() == TASK_THROWSMGRENADE)
		return;

	if (GetCurrentTaskID() == TASK_DESTROYBREAKABLE)
		return;

	if (!FNullEnt(m_enemy))
		return;

	if (IsOnLadder())
	{
		m_pickupItem = nullptr;
		m_pickupType = PICKTYPE_NONE;
		return;
	}

	edict_t* ent = nullptr;

	if (!FNullEnt(m_pickupItem))
	{
		while (!FNullEnt(ent = FIND_ENTITY_IN_SPHERE(ent, pev->origin, 512.0f)))
		{
			if (ent != m_pickupItem || (ent->v.effects & EF_NODRAW) || IsValidPlayer(ent->v.owner))
				continue; // someone owns this weapon or it hasn't re spawned yet

			if (ItemIsVisible(GetEntityOrigin(ent), const_cast <char*> (STRING(ent->v.classname))))
				return;

			break;
		}
	}

	edict_t* pickupItem = nullptr;
	m_pickupItem = nullptr;
	m_pickupType = PICKTYPE_NONE;
	ent = nullptr;
	pickupItem = nullptr;

	PickupType pickupType = PICKTYPE_NONE;

	float minDistance = squaredf(512.0f);
	while (!FNullEnt(ent = FIND_ENTITY_IN_SPHERE(ent, pev->origin, 512.0f)))
	{
		pickupType = PICKTYPE_NONE;
		if ((ent->v.effects & EF_NODRAW) || ent == m_itemIgnore)
			continue;

		if (pev->health < pev->max_health && cstrncmp("item_healthkit", STRING(ent->v.classname), 14) == 0)
			pickupType = PICKTYPE_GETENTITY;
		else if (pev->health < pev->max_health && cstrncmp("func_healthcharger", STRING(ent->v.classname), 18) == 0 && ent->v.frame == 0)
		{
			const Vector origin = GetEntityOrigin(ent);
			if ((pev->origin - origin).GetLengthSquared() < squaredf(100.0f))
			{
				if (g_isXash)
					pev->button |= IN_USE;
				else
					MDLL_Use(ent, GetEntity());
			}

			m_lookAt = origin;
			pickupType = PICKTYPE_GETENTITY;
			m_canChooseAimDirection = false;
			m_moveToGoal = false;
			m_checkTerrain = false;
			m_moveSpeed = pev->maxspeed;
			m_strafeSpeed = 0.0f;
			m_aimStopTime = 0.0f;
		}
		else if (pev->armorvalue < 100 && cstrncmp("item_battery", STRING(ent->v.classname), 12) == 0)
			pickupType = PICKTYPE_GETENTITY;
		else if (pev->armorvalue < 100 && cstrncmp("func_recharge", STRING(ent->v.classname), 13) == 0 && ent->v.frame == 0)
		{
			const Vector origin = GetEntityOrigin(ent);
			if (!g_isXash && (pev->origin - origin).GetLengthSquared() < squaredf(100.0f))
			{
				if (g_isXash)
					pev->button |= IN_USE;
				else
					MDLL_Use(ent, GetEntity());
			}

			m_lookAt = origin;
			pickupType = PICKTYPE_GETENTITY;
			m_canChooseAimDirection = false;
			m_moveToGoal = false;
			m_checkTerrain = false;
			m_moveSpeed = pev->maxspeed;
			m_strafeSpeed = 0.0f;
			m_aimStopTime = 0.0f;
		}
		else if (g_gameVersion == HALFLIFE)
		{
			if (m_currentWeapon != WEAPON_SNARK && cstrncmp("monster_snark", STRING(ent->v.classname), 13) == 0)
			{
				SetEnemy(ent);
				SetLastEnemy(ent);
				m_canChooseAimDirection = false;
				m_moveToGoal = false;
				m_checkTerrain = false;
				m_lookAt = GetEntityOrigin(ent);
				m_moveSpeed = -pev->maxspeed;
				m_strafeSpeed = 0.0f;
				m_aimStopTime = 0.0f;

				if (!(pev->oldbuttons & IN_ATTACK))
					pev->button |= IN_ATTACK;
			}
			else if (cstrncmp("weapon_", STRING(ent->v.classname), 7) == 0)
				pickupType = PICKTYPE_GETENTITY;
			else if (cstrncmp("ammo_", STRING(ent->v.classname), 5) == 0)
				pickupType = PICKTYPE_GETENTITY;
			else if (cstrncmp("weaponbox", STRING(ent->v.classname), 9) == 0)
				pickupType = PICKTYPE_GETENTITY;
		}
		else if (cstrncmp("hostage_entity", STRING(ent->v.classname), 14) == 0)
			pickupType = PICKTYPE_HOSTAGE;
		else if (cstrncmp("weaponbox", STRING(ent->v.classname), 9) == 0 && cstrcmp(STRING(ent->v.model) + 9, "backpack.mdl") == 0)
			pickupType = PICKTYPE_DROPPEDC4;
		else if (!m_isUsingGrenade && cstrncmp("weaponbox", STRING(ent->v.classname), 9) == 0 && cstrcmp(STRING(ent->v.model) + 9, "backpack.mdl") == 0)
			pickupType = PICKTYPE_DROPPEDC4;
		else if (!m_isUsingGrenade && (cstrncmp("weaponbox", STRING(ent->v.classname), 9) == 0 || cstrncmp("armoury_entity", STRING(ent->v.classname), 14) == 0 || cstrncmp("csdm", STRING(ent->v.classname), 4) == 0))
			pickupType = PICKTYPE_WEAPON;
		else if (!m_isUsingGrenade && cstrncmp("weapon_shield", STRING(ent->v.classname), 13) == 0)
			pickupType = PICKTYPE_SHIELDGUN;
		else if (!m_hasDefuser && cstrncmp("item_thighpack", STRING(ent->v.classname), 14) == 0 && m_team == TEAM_COUNTER)
			pickupType = PICKTYPE_DEFUSEKIT;
		else if (cstrncmp("grenade", STRING(ent->v.classname), 7) == 0 && cstrcmp(STRING(ent->v.model) + 9, "c4.mdl") == 0)
			pickupType = PICKTYPE_PLANTEDC4;
		else
		{
			int i;
			for (i = 0; (i < entityNum && pickupType == PICKTYPE_NONE); i++)
			{
				if (g_entityId[i] == -1 || g_entityAction[i] != 3)
					continue;

				if (m_team != g_entityTeam[i] || (g_entityTeam[i] != TEAM_COUNTER && g_entityTeam[i] != TEAM_TERRORIST))
					continue;

				if (ent != INDEXENT(g_entityId[i]))
					continue;

				pickupType = PICKTYPE_GETENTITY;
			}
		}

		if (m_isZombieBot && pickupType != PICKTYPE_GETENTITY)
			continue;

		if (pickupType == PICKTYPE_NONE)
			continue;

		const Vector entityOrigin = GetEntityOrigin(ent);
		const float distance = (pev->origin - entityOrigin).GetLengthSquared();
		if (distance > minDistance)
			continue;

		if (!ItemIsVisible(entityOrigin, const_cast <char*> (STRING(ent->v.classname))))
		{
			if (cstrncmp("grenade", STRING(ent->v.classname), 7) != 0 || cstrcmp(STRING(ent->v.model) + 9, "c4.mdl") != 0)
				continue;

			if (distance > squaredf(80.0f))
				continue;
		}

		bool allowPickup = true;
		if (pickupType == PICKTYPE_GETENTITY)
			allowPickup = true;
		else if (pickupType == PICKTYPE_WEAPON)
		{
			const int weaponCarried = GetBestWeaponCarried();
			const int secondaryWeaponCarried = GetBestSecondaryWeaponCarried();
			const int secondaryWeaponAmmoMax = g_weaponDefs[g_weaponSelect[secondaryWeaponCarried].id].ammo1Max;
			const int weaponAmmoMax = g_weaponDefs[g_weaponSelect[weaponCarried].id].ammo1Max;

			if (secondaryWeaponCarried < 7 && (m_ammo[g_weaponSelect[secondaryWeaponCarried].id] > 0.3 * secondaryWeaponAmmoMax) && cstrcmp(STRING(ent->v.model) + 9, "w_357ammobox.mdl") == 0)
				allowPickup = false;
			else if (!m_isVIP && weaponCarried >= 7 && (m_ammo[g_weaponSelect[weaponCarried].id] > 0.3 * weaponAmmoMax) && cstrncmp(STRING(ent->v.model) + 9, "w_", 2) == 0)
			{
				if (cstrcmp(STRING(ent->v.model) + 9, "w_9mmarclip.mdl") == 0 && !(weaponCarried == WEAPON_FAMAS || weaponCarried == WEAPON_AK47 || weaponCarried == WEAPON_M4A1 || weaponCarried == WEAPON_GALIL || weaponCarried == WEAPON_AUG || weaponCarried == WEAPON_SG552))
					allowPickup = false;
				else if (cstrcmp(STRING(ent->v.model) + 9, "w_shotbox.mdl") == 0 && !(weaponCarried == WEAPON_M3 || weaponCarried == WEAPON_XM1014))
					allowPickup = false;
				else if (cstrcmp(STRING(ent->v.model) + 9, "w_9mmclip.mdl") == 0 && !(weaponCarried == WEAPON_MP5 || weaponCarried == WEAPON_TMP || weaponCarried == WEAPON_P90 || weaponCarried == WEAPON_MAC10 || weaponCarried == WEAPON_UMP45))
					allowPickup = false;
				else if (cstrcmp(STRING(ent->v.model) + 9, "w_crossbow_clip.mdl") == 0 && !(weaponCarried == WEAPON_AWP || weaponCarried == WEAPON_G3SG1 || weaponCarried == WEAPON_SCOUT || weaponCarried == WEAPON_SG550))
					allowPickup = false;
				else if (cstrcmp(STRING(ent->v.model) + 9, "w_chainammo.mdl") == 0 && weaponCarried == WEAPON_M249)
					allowPickup = false;
			}
			else if (pev->health >= 100 && cstrcmp(STRING(ent->v.model) + 9, "medkit.mdl") == 0)
				allowPickup = false;
			else if (pev->armorvalue >= 100 && (cstrcmp(STRING(ent->v.model) + 9, "kevlar.mdl") == 0 || cstrcmp(STRING(ent->v.model) + 9, "battery.mdl") == 0)) // armor vest
				allowPickup = false;
			else if (pev->weapons & (1 << WEAPON_FBGRENADE) && cstrcmp(STRING(ent->v.model) + 9, "flashbang.mdl") == 0) // concussion grenade
				allowPickup = false;
			else if (pev->weapons & (1 << WEAPON_HEGRENADE) && cstrcmp(STRING(ent->v.model) + 9, "hegrenade.mdl") == 0) // explosive grenade
				allowPickup = false;
			else if (pev->weapons & (1 << WEAPON_SMGRENADE) && cstrcmp(STRING(ent->v.model) + 9, "smokegrenade.mdl") == 0) // smoke grenade
				allowPickup = false;
			else if (m_isVIP || !ebot_prefer_better_pickup.GetBool() || !RateGroundWeapon(ent))
				allowPickup = false;
		}
		else if (pickupType == PICKTYPE_SHIELDGUN)
		{
			if ((pev->weapons & (1 << WEAPON_ELITE)) || HasShield() || m_isVIP || (HasPrimaryWeapon() && !RateGroundWeapon(ent)))
				allowPickup = false;
		}
		else if (m_team != TEAM_TERRORIST && m_team != TEAM_COUNTER)
			allowPickup = false;
		else if (m_team == TEAM_TERRORIST)
		{
			if (pickupType == PICKTYPE_DROPPEDC4)
			{
				allowPickup = true;
				m_destOrigin = entityOrigin;
			}
			else if (pickupType == PICKTYPE_HOSTAGE)
			{
				m_itemIgnore = ent;
				allowPickup = false;

				if (m_skill > 80 && chanceof(50) && GetCurrentTaskID() != TASK_GOINGFORCAMP && GetCurrentTaskID() != TASK_CAMP)
				{
					const int index = FindDefendWaypoint(entityOrigin);
					if (IsValidWaypoint(index))
					{
						m_campposition = g_waypoint->GetPath(index)->origin;
						PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, ebot_camp_max.GetFloat(), true);
						return;
					}
				}
			}
			else if (pickupType == PICKTYPE_PLANTEDC4)
			{
				allowPickup = false;

				if (!m_defendedBomb)
				{
					const int index = FindDefendWaypoint(entityOrigin);
					if (IsValidWaypoint(index))
					{
						const float timeMidBlowup = g_timeBombPlanted + ((engine->GetC4TimerTime() * 0.5f) + engine->GetC4TimerTime() * 0.25f) - g_waypoint->GetTravelTime(m_moveSpeed, pev->origin, g_waypoint->GetPath(index)->origin);
						if (timeMidBlowup > engine->GetTime())
						{
							m_defendedBomb = true;
							RemoveCertainTask(TASK_MOVETOPOSITION);
							RemoveCertainTask(TASK_GOINGFORCAMP);
							RemoveCertainTask(TASK_CAMP);
							m_campposition = g_waypoint->GetPath(index)->origin;
							PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, engine->GetTime() + ebot_camp_max.GetFloat(), true);
						}
						else
						{
							PushTask(TASK_ESCAPEFROMBOMB, TASKPRI_ESCAPEFROMBOMB, -1, 1.0f, true, true);
							RadioMessage(Radio_ShesGonnaBlow);
						}
					}
				}
			}
		}
		else if (m_team == TEAM_COUNTER)
		{
			if (pickupType == PICKTYPE_HOSTAGE)
			{
				if (FNullEnt(ent) || ent->v.health <= 0 || ent->v.speed > 1.0f)
					allowPickup = false; // never pickup dead/moving hostages
				else
				{
					for (const auto& bot : g_botManager->m_bots)
					{
						if (bot != nullptr && bot->m_isAlive)
						{
							for (const auto& hostage : bot->m_hostages)
							{
								if (hostage == ent)
								{
									allowPickup = false;
									break;
								}
							}
						}
					}
				}
			}
			else if (pickupType == PICKTYPE_DROPPEDC4)
			{
				m_itemIgnore = ent;
				allowPickup = false;

				if (m_skill > 80 && crandomint(0, 100) < 90)
				{
					const int index = FindDefendWaypoint(entityOrigin);
					if (IsValidWaypoint(index))
					{
						m_campposition = g_waypoint->GetPath(index)->origin;
						PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, engine->GetTime() + ebot_camp_max.GetFloat(), false);
						return;
					}
				}
			}
			else if (pickupType == PICKTYPE_PLANTEDC4)
			{
				if (m_states & (STATE_SEEINGENEMY) || OutOfBombTimer())
				{
					allowPickup = false;
					return;
				}

				allowPickup = !IsBombDefusing(g_waypoint->GetBombPosition());
				if (!allowPickup)
				{
					const int index = FindDefendWaypoint(entityOrigin);
					if (IsValidWaypoint(index))
					{
						const float timeBlowup = g_timeBombPlanted + engine->GetC4TimerTime() - g_waypoint->GetTravelTime(pev->maxspeed, pev->origin, g_waypoint->GetPath(index)->origin);
						if (timeBlowup > engine->GetTime())
						{
							RemoveCertainTask(TASK_MOVETOPOSITION); // remove any move tasks
							RemoveCertainTask(TASK_GOINGFORCAMP);
							RemoveCertainTask(TASK_CAMP);
							m_campposition = g_waypoint->GetPath(index)->origin;
							PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, engine->GetTime() + timeBlowup, true);
						}
						else
						{
							PushTask(TASK_ESCAPEFROMBOMB, TASKPRI_ESCAPEFROMBOMB, -1, 1.0f, true, true);
							RadioMessage(Radio_ShesGonnaBlow);
						}
					}
				}
			}
		}

		if (allowPickup)
		{
			minDistance = distance;
			pickupItem = ent;
			m_pickupType = pickupType;
		}
	}

	if (!FNullEnt(pickupItem))
	{
		if (!IsDeathmatchMode())
		{
			for (const auto& bot : g_botManager->m_bots)
			{
				if (bot != nullptr && bot != this && bot->m_isAlive && bot->m_pickupItem == pickupItem && bot->m_team == m_team)
				{
					m_pickupItem = nullptr;
					m_pickupType = PICKTYPE_NONE;
					return;
				}
			}
		}

		const Vector pickupOrigin = GetEntityOrigin(pickupItem);
		if (pickupOrigin.z > EyePosition().z + 12.0f || IsDeadlyDrop(pickupOrigin))
		{
			m_pickupItem = nullptr;
			m_pickupType = PICKTYPE_NONE;

			return;
		}

		m_pickupItem = pickupItem;
	}
}

// this function depending on show boolen, shows/remove chatter, icon, on the head of bot
void Bot::SwitchChatterIcon(const bool show)
{
	if (g_gameVersion == HALFLIFE)
		return;

	for (const auto& client : g_clients)
	{
		if (client.team != m_team || !IsValidPlayer(client.ent) || IsValidBot(client.ent))
			continue;

		MESSAGE_BEGIN(MSG_ONE, g_netMsg->GetId(NETMSG_BOTVOICE), nullptr, client.ent); // begin message
		WRITE_BYTE(show); // switch on/off
		WRITE_BYTE(m_index);
		MESSAGE_END();
	}
}

// this function inserts the radio message into the message queue
void Bot::RadioMessage(const int message)
{
	if (g_gameVersion == HALFLIFE)
		return;

	if (ebot_use_radio.GetInt() < 1)
		return;

	if (m_radiotimer > engine->GetTime())
		return;

	if (m_numFriendsLeft < 1)
		return;

	if (m_numEnemiesLeft == 0)
	{
		PlayChatterMessage(ChatterMessage::Happy);
		return;
	}

	if (GetGameMode() == MODE_DM)
		return;

	// poor bots spamming this :(
	if (IsZombieMode() && message == Radio_NeedBackup && !g_waypoint->m_zmHmPoints.IsEmpty())
		return;

	m_radioSelect = message;
	if (ebot_use_radio.GetInt() > 1)
	{
		PlayChatterMessage(GetEqualChatter(message));
		return;
	}

	PushMessageQueue(CMENU_RADIO);
	m_radiotimer = engine->GetTime() + crandomfloat(m_numFriendsLeft * 0.5f, m_numFriendsLeft);
}

// this function inserts the voice message into the message queue (mostly same as above)
//std::unordered_map<char*, float> durationMap;
void Bot::PlayChatterMessage(const ChatterMessage message)
{
	if (ebot_use_radio.GetInt() < 2)
		return;

	if (g_audioTime > engine->GetTime())
		return;

	if (m_numFriendsLeft == 0)
		return;

	if (m_radiotimer > engine->GetTime())
		return;

	if (m_lastChatterMessage == message)
		return;

	char* voice = GetVoice(message);
	if (voice == nullptr)
		return;

	//float dur;
	//const auto it = durationMap.find(voice);
	//if (it != durationMap.end())
	//	dur = it->second;
	//else
	//{
		// storing FormatBuffer returns in a char* resulting weird paths for some reason...
		File fp(FormatBuffer("%s/sound/%s/%s.wav", GetModName(), ebot_chatter_path.GetString(), voice), "rb");
		if (!fp.IsValid())
			return;

		struct WavHeader
		{
			char chunk_id[4];
			int chunk_size;
			char format[4];
			char subchunk1_id[4];
			int subchunk1_size;
			short int audio_format;
			short int num_channels;
			int sample_rate;
			int byte_rate;
			short int block_align;
			short int bits_per_sample;
			char subchunk2_id[4];
			int subchunk2_size;
		};

		WavHeader header;
		fp.Read(&header, sizeof(header));

		const float dur = ((static_cast<float>(header.subchunk2_size) / (static_cast<float>(header.num_channels) * (static_cast<float>(header.bits_per_sample) * 0.125f))) / static_cast<float>(header.sample_rate)) + 0.75f;
		if (dur < 0.2f || dur > 12.0f)
			return;

		//durationMap[voice] = dur;
	//}

	m_chatterTimer = engine->GetTime() + dur;
	SwitchChatterIcon(true);

	for (const auto& client : g_clients)
	{
		if (client.team != m_team || IsValidBot(client.ent) || FNullEnt(client.ent))
			continue;

		MESSAGE_BEGIN(MSG_ONE, g_netMsg->GetId(NETMSG_SENDAUDIO), nullptr, client.ent); // begin message
		WRITE_BYTE(m_index);
		WRITE_STRING(FormatBuffer("%s/%s.wav", ebot_chatter_path.GetString(), voice));
		WRITE_SHORT(m_voicePitch);
		MESSAGE_END();
	}

	m_lastChatterMessage = message;
	m_radiotimer = engine->GetTime() + crandomfloat(m_numFriendsLeft * 0.5f, m_numFriendsLeft);
	PushMessageQueue(CMENU_RADIO);
	g_audioTime = m_chatterTimer;
}

// this function checks and executes pending messages
void Bot::CheckMessageQueue(void)
{
	if (m_actMessageIndex == m_pushMessageIndex)
		return;

	// get message from stack
	const auto state = GetMessageQueue();

	// nothing to do?
	if (state == CMENU_IDLE)
		return;

	if (state == CMENU_TEAMSAY)
	{
		if (GetGameMode() == MODE_DM || g_gameVersion == HALFLIFE)
			return;
	}

	if (state == CMENU_RADIO)
	{
		if (GetGameMode() == MODE_DM || g_gameVersion == HALFLIFE)
			return;

		if (g_audioTime + 1.0f > engine->GetTime())
			return;

		if (m_chatterTimer + 1.0f > engine->GetTime())
			return;
	}

	switch (state)
	{
	case CMENU_BUY: // general buy message
	{
		// if fun-mode no need to buy
		if (!ebot_buy_weapons.GetBool() || (ebot_knifemode.GetBool() && (ebot_eco_rounds.GetInt() != 1 || HasPrimaryWeapon())))
		{
			m_buyState = 6;
			if (chanceof(m_skill))
				SelectKnife();
		}

		// buy weapon
		if (m_nextBuyTime > engine->GetTime())
		{
			// keep sending message
			PushMessageQueue(CMENU_BUY);
			return;
		}

		if (!m_inBuyZone)
		{
			m_buyingFinished = true;
			break;
		}

		m_nextBuyTime = engine->GetTime() + crandomfloat(0.6f, 1.2f);

		// if freezetime is very low do not delay the buy process
		if (CVAR_GET_FLOAT("mp_freezetime") < 2.0f)
			m_nextBuyTime = engine->GetTime() + crandomfloat(0.25f, 0.5f);

		// prevent vip from buying
		if (g_mapType & MAP_AS)
		{
			if (*(INFOKEY_VALUE(GET_INFOKEYBUFFER(GetEntity()), "model")) == 'v')
			{
				m_isVIP = true;
				m_buyState = 7;
			}
			else
				m_isVIP = false;
		}
		else
			m_isVIP = false;

		// prevent terrorists from buying on es maps
		if ((g_mapType & MAP_ES) && m_team == TEAM_TERRORIST)
			m_buyState = 76;

		// prevent teams from buying on fun maps
		if (g_mapType & (MAP_KA | MAP_FY | MAP_AWP))
		{
			m_buyState = 7;

			if (g_mapType & MAP_KA)
				ebot_knifemode.SetInt(1);
		}

		if (m_buyState > 6)
		{
			m_buyingFinished = true;
			if (chanceof(m_skill))
				SelectKnife();
			return;
		}

		PushMessageQueue(CMENU_IDLE);
		PerformWeaponPurchase();
		break;
	}
	case CMENU_RADIO:
	{
		if (!m_isAlive)
			break;

		// if last bot radio command (global) happened just a 3 seconds ago, delay response
		if ((m_team == 0 || m_team == 1) && (g_lastRadioTime[m_team] + 3.0f < engine->GetTime()))
		{
			// if same message like previous just do a yes/no
			if (m_radioSelect != Radio_Affirmative && m_radioSelect != Radio_Negative)
			{
				if (m_radioSelect == g_lastRadio[m_team] && g_lastRadioTime[m_team] + 1.5 > engine->GetTime())
					m_radioSelect = -1;
				else
				{
					if (m_radioSelect != Radio_ReportingIn)
						g_lastRadio[m_team] = m_radioSelect;
					else
						g_lastRadio[m_team] = -1;

					for (const auto& bot : g_botManager->m_bots)
					{
						if (bot != nullptr && bot != this && bot->m_team == m_team)
						{
							bot->m_radioOrder = m_radioSelect;
							bot->m_radioEntity = GetEntity();
						}
					}
				}
			}

			if (m_radioSelect != -1)
			{
				if (m_radioSelect != Radio_ReportingIn)
				{
					if (m_radioSelect < Radio_GoGoGo)
						FakeClientCommand(GetEntity(), "radio1");
					else if (m_radioSelect < Radio_Affirmative)
					{
						m_radioSelect -= Radio_GoGoGo - 1;
						FakeClientCommand(GetEntity(), "radio2");
					}
					else
					{
						m_radioSelect -= Radio_Affirmative - 1;
						FakeClientCommand(GetEntity(), "radio3");
					}

					// select correct menu item for this radio message
					FakeClientCommand(GetEntity(), "menuselect %d", m_radioSelect);
				}
			}

			g_lastRadioTime[m_team] = engine->GetTime(); // store last radio usage
		}
		else
			PushMessageQueue(CMENU_RADIO);

		break;
	}
	case CMENU_SAY:
	{
		ChatSay(false, m_tempStrings);
		break;
	}
	case CMENU_TEAMSAY:
	{
		ChatSay(true, m_tempStrings);
		break;
	}
	}
}

// this function checks for weapon restrictions
bool Bot::IsRestricted(const int weaponIndex)
{
	if (m_buyingFinished)
		return false;

	if (IsNullString(ebot_restrictweapons.GetString()))
		return false;

	uint16_t i;
	Array <String> bannedWeapons = String(ebot_restrictweapons.GetString()).Split(';');
	for (i = 0; bannedWeapons.GetElementNumber(); i++)
	{
		// check is this weapon is banned
		if (cstrncmp(bannedWeapons[i], STRING(GetWeaponReturn(true, nullptr, weaponIndex)), bannedWeapons[i].GetLength()) == 0)
			return true;
	}

	return false;
}

// this function determines currently owned primary weapon, and checks if bot has
// enough money to buy more powerful weapon
bool Bot::IsMorePowerfulWeaponCanBeBought(void)
{
	// if bot is not rich enough or non-standard weapon mode enabled return false
	if (g_weaponSelect[25].teamStandard != 1 || m_moneyAmount < 4000)
		return false;

	// also check if bot has really bad weapon, maybe it's time to change it
	if (UsesBadPrimary())
		return true;

	if (!IsNullString(ebot_restrictweapons.GetString()))
	{
		uint16_t i;
		Array <String> bannedWeapons = String(ebot_restrictweapons.GetString()).Split(';');
		for (i = 0; i < bannedWeapons.GetElementNumber(); i++)
		{
			if (m_currentWeapon == GetWeaponReturn(false, bannedWeapons[i]))
				return true;
		}
	}

	if (m_currentWeapon == WEAPON_SCOUT && m_moneyAmount > 5000)
		return true;
	else if (m_currentWeapon == WEAPON_MP5 && m_moneyAmount > 6000)
		return true;
	else if (m_currentWeapon == WEAPON_M3 || (m_currentWeapon == WEAPON_XM1014 && m_moneyAmount > 4000))
		return true;

	return false;
}

void Bot::PerformWeaponPurchase(void)
{
	m_nextBuyTime = engine->GetTime();
	WeaponSelect* selectedWeapon = nullptr;
	int* ptr = g_weaponPrefs[m_personality] + Const_NumWeapons;

	switch (m_buyState)
	{
	case 0:
	{
		if ((!HasShield() && !HasPrimaryWeapon()) && (g_botManager->EconomicsValid(m_team) || IsMorePowerfulWeaponCanBeBought()))
		{
			int gunMoney = 0, playerMoney = m_moneyAmount;
			int likeGunId[2] = { 0, 0 };
			int loadTime = 0;

			do
			{
				ptr--;
				gunMoney = 0;
				selectedWeapon = &g_weaponSelect[*ptr];
				loadTime++;

				if (selectedWeapon->buyGroup == 1)
					continue;

				if ((g_mapType & MAP_AS) && selectedWeapon->teamAS != 2 && selectedWeapon->teamAS != m_team)
					continue;

				if (selectedWeapon->teamStandard != 2 && selectedWeapon->teamStandard != m_team)
					continue;

				if (IsRestricted(selectedWeapon->id))
					continue;

				gunMoney = selectedWeapon->price;
				if (playerMoney <= gunMoney)
					continue;

				const int gunMode = BuyWeaponMode(selectedWeapon->id);
				if (playerMoney < gunMoney + (gunMode * 125))
					continue;

				if (likeGunId[0] == 0)
					likeGunId[0] = selectedWeapon->id;
				else
				{
					if (gunMode <= BuyWeaponMode(likeGunId[0]))
					{
						if ((BuyWeaponMode(likeGunId[1]) > BuyWeaponMode(likeGunId[0])) ||
							(BuyWeaponMode(likeGunId[1]) == BuyWeaponMode(likeGunId[0]) && (crandomint(1, 2) == 2)))
							likeGunId[1] = likeGunId[0];

						likeGunId[0] = selectedWeapon->id;
					}
					else
					{
						if (likeGunId[1] != 0)
						{
							if (gunMode <= BuyWeaponMode(likeGunId[1]))
								likeGunId[1] = selectedWeapon->id;
						}
						else
							likeGunId[1] = selectedWeapon->id;
					}
				}
			} while (loadTime < Const_NumWeapons);

			if (likeGunId[0] != 0)
			{
				WeaponSelect* buyWeapon = &g_weaponSelect[0];
				int weaponId = likeGunId[0];
				if (likeGunId[1] != 0)
					weaponId = likeGunId[(crandomint(1, 7) > 3) ? 0 : 1];

				int i;
				for (i = 0; i < Const_NumWeapons; i++)
				{
					if (buyWeapon[i].id == weaponId)
					{
						FakeClientCommand(GetEntity(), "buy;menuselect %d", buyWeapon[i].buyGroup);

						if (m_team == TEAM_TERRORIST)
							FakeClientCommand(GetEntity(), "menuselect %d", buyWeapon[i].newBuySelectT);
						else
							FakeClientCommand(GetEntity(), "menuselect %d", buyWeapon[i].newBuySelectCT);
					}
				}
			}
		}
		else if (HasPrimaryWeapon() && !HasShield())
			m_reloadState = RSTATE_PRIMARY;

		break;
	}
	case 1:
	{
		if (pev->armorvalue < crandomint(50, 80) && (g_botManager->EconomicsValid(m_team) && HasPrimaryWeapon()))
		{
			if (m_moneyAmount > 1500 && !IsRestricted(WEAPON_KEVHELM))
				FakeClientCommand(GetEntity(), "buyequip;menuselect 2");
			else
				FakeClientCommand(GetEntity(), "buyequip;menuselect 1");
		}
		break;
	}
	case 2:
	{
		if ((HasPrimaryWeapon() && m_moneyAmount > crandomint(6000, 9000)))
		{
			int likeGunId = 0;
			int loadTime = 0;

			do
			{
				ptr--;
				selectedWeapon = &g_weaponSelect[*ptr];
				loadTime++;

				if (selectedWeapon->buyGroup != 1)
					continue;

				if ((g_mapType & MAP_AS) && selectedWeapon->teamAS != 2 && selectedWeapon->teamAS != m_team)
					continue;

				if (selectedWeapon->teamStandard != 2 && selectedWeapon->teamStandard != m_team)
					continue;

				if (IsRestricted(selectedWeapon->id))
					continue;

				if (m_moneyAmount <= (selectedWeapon->price + 120))
					continue;

				const int gunMode = BuyWeaponMode(selectedWeapon->id);
				if (likeGunId == 0)
				{
					if ((pev->weapons & ((1 << WEAPON_USP) | (1 << WEAPON_GLOCK18))))
					{
						if (gunMode < 2)
							likeGunId = selectedWeapon->id;
					}
					else
						likeGunId = selectedWeapon->id;
				}
				else
				{
					if (gunMode < BuyWeaponMode(likeGunId))
						likeGunId = selectedWeapon->id;
				}
			} while (loadTime < Const_NumWeapons);

			if (likeGunId != 0)
			{
				WeaponSelect* buyWeapon = &g_weaponSelect[0];
				const int weaponId = likeGunId;

				int i;
				for (i = 0; i < Const_NumWeapons; i++)
				{
					if (buyWeapon[i].id == weaponId)
					{
						FakeClientCommand(GetEntity(), "buy;menuselect %d", buyWeapon[i].buyGroup);

						if (m_team == TEAM_TERRORIST)
							FakeClientCommand(GetEntity(), "menuselect %d", buyWeapon[i].newBuySelectT);
						else
							FakeClientCommand(GetEntity(), "menuselect %d", buyWeapon[i].newBuySelectCT);
					}
				}
			}
		}
		break;
	}
	case 3:
	{
		if (!HasPrimaryWeapon() && !chanceof(m_skill) && !IsRestricted(WEAPON_SHIELDGUN))
		{
			FakeClientCommand(GetEntity(), "buyequip");
			FakeClientCommand(GetEntity(), "menuselect 8");
		}

		if (crandomint(1, 2) == 1)
		{
			FakeClientCommand(GetEntity(), "buy;menuselect 1");

			if (crandomint(1, 2) == 1)
				FakeClientCommand(GetEntity(), "menuselect 4");
			else
				FakeClientCommand(GetEntity(), "menuselect 5");
		}

		break;
	}
	case 4:
	{
		if (chanceof(m_skill) && !IsRestricted(WEAPON_HEGRENADE))
		{
			FakeClientCommand(GetEntity(), "buyequip");
			FakeClientCommand(GetEntity(), "menuselect 4");
		}

		if (chanceof(m_skill) && g_botManager->EconomicsValid(m_team) && !IsRestricted(WEAPON_FBGRENADE))
		{
			FakeClientCommand(GetEntity(), "buyequip");
			FakeClientCommand(GetEntity(), "menuselect 3");
		}

		if (chanceof(m_skill) && g_botManager->EconomicsValid(m_team) && !IsRestricted(WEAPON_FBGRENADE))
		{
			FakeClientCommand(GetEntity(), "buyequip");
			FakeClientCommand(GetEntity(), "menuselect 3");
		}

		if (chanceof(m_skill) && g_botManager->EconomicsValid(m_team) && !IsRestricted(WEAPON_SMGRENADE))
		{
			FakeClientCommand(GetEntity(), "buyequip");
			FakeClientCommand(GetEntity(), "menuselect 5");
		}

		break;
	}
	case 5:
	{
		if ((g_mapType & MAP_DE) && m_team == TEAM_COUNTER && chanceof(m_skill) && m_moneyAmount > 200 && !IsRestricted(WEAPON_DEFUSER))
			FakeClientCommand(GetEntity(), "defuser"); // use alias in SteamCS
		break;
	}
	case 6:
	{
		int i;
		for (i = 0; i <= 5; i++)
			FakeClientCommand(GetEntity(), "buyammo%d", crandomint(1, 2)); // simulate human

		if (chanceof(m_skill))
			FakeClientCommand(GetEntity(), "buy;menuselect 7");
		else
			FakeClientCommand(GetEntity(), "buy;menuselect 6");

		if (m_reloadState != RSTATE_PRIMARY)
			m_reloadState = RSTATE_SECONDARY;

		break;
	}
	}

	m_buyState++;
	PushMessageQueue(CMENU_BUY);
}

int Bot::BuyWeaponMode(const int weaponId)
{
	int gunMode = 10;
	switch (weaponId)
	{
	case WEAPON_SHIELDGUN:
	{
		gunMode = 8;
		break;
	}
	case WEAPON_TMP:
	case WEAPON_UMP45:
	case WEAPON_P90:
	case WEAPON_MAC10:
	case WEAPON_SCOUT:
	case WEAPON_M3:
	case WEAPON_M249:
	case WEAPON_FN57:
	case WEAPON_P228:
	{
		gunMode = 5;
		break;
	}
	case WEAPON_XM1014:
	case WEAPON_G3SG1:
	case WEAPON_SG550:
	case WEAPON_GALIL:
	case WEAPON_ELITE:
	case WEAPON_SG552:
	case WEAPON_AUG:
	{
		gunMode = 4;
		break;
	}
	case WEAPON_MP5:
	case WEAPON_FAMAS:
	case WEAPON_USP:
	case WEAPON_GLOCK18:
	{
		gunMode = 3;
		break;
	}
	case WEAPON_AWP:
	case WEAPON_DEAGLE:
	{
		gunMode = 2;
		break;
	}
	case WEAPON_AK47:
	case WEAPON_M4A1:
	{
		gunMode = 1;
		break;
	}
	}

	return gunMode;
}

// this function returns the behavior having the higher activation level
TaskItem* MaxDesire(TaskItem* first, TaskItem* second)
{
	if (first->desire > second->desire)
		return first;

	return second;
}

// this function returns the first behavior if its activation level is anything higher than zero
TaskItem* SubsumeDesire(TaskItem* first, TaskItem* second)
{
	if (first->desire > 0.0f)
		return first;

	return second;
}

// this function clamp the inputs to be the last known value outside the [min, max] range
float HysteresisDesire(const float cur, const float min, const float max, float old)
{
	if (cur <= min || cur >= max)
		old = cur;

	return old;
}

// this function carried out each frame. does all of the sensing, calculates emotions and finally sets the desired action after applying all of the Filters
void Bot::SetConditions(void)
{
	m_aimFlags = 0;

	// slowly increase/decrease dynamic emotions back to their base level
	if (m_nextEmotionUpdate < engine->GetTime())
	{
		if (m_skill >= 80)
		{
			m_agressionLevel *= 2;
			m_fearLevel *= 0.5f;
		}

		if (m_agressionLevel > m_baseAgressionLevel)
			m_agressionLevel -= 0.05f;
		else
			m_agressionLevel += 0.05f;

		if (m_fearLevel > m_baseFearLevel)
			m_fearLevel -= 0.05f;
		else
			m_fearLevel += 0.05f;

		if (m_agressionLevel < 0.0f)
			m_agressionLevel = 0.0f;

		if (m_fearLevel < 0.0f)
			m_fearLevel = 0.0f;

		m_nextEmotionUpdate = engine->GetTime() + 0.5f;
	}

	// does bot see an enemy?
	if (LookupEnemy())
	{
		m_states |= STATE_SEEINGENEMY;
		SetMoveTarget(nullptr);
	}
	else
	{
		m_states &= ~STATE_SEEINGENEMY;
		SetEnemy(nullptr);
	}

	if (m_lastVictim != nullptr && (!IsAlive(m_lastVictim) || !IsValidPlayer(m_lastVictim)))
		m_lastVictim = nullptr;

	// did bot just kill an enemy?
	if (!FNullEnt(m_lastVictim))
	{
		if (GetTeam(m_lastVictim) != m_team)
		{
			// add some aggression because we just killed somebody
			m_agressionLevel += 0.1f;

			if (m_agressionLevel > 1.0f)
				m_agressionLevel = 1.0f;

			if (chanceof(50))
				ChatMessage(CHAT_KILL);
			else
				RadioMessage(Radio_EnemyDown);

			// if no more enemies found AND bomb planted, switch to knife to get to bombplace faster
			if (m_team == TEAM_COUNTER && m_currentWeapon != WEAPON_KNIFE && m_numEnemiesLeft == 0 && g_bombPlanted)
			{
				SelectKnife();

				// order team to regroup
				RadioMessage(Radio_RegroupTeam);
			}
		}
		else
			ChatMessage(CHAT_TEAMKILL, true);

		m_lastVictim = nullptr;
	}

	// check if our current enemy is still valid
	if (!FNullEnt(m_lastEnemy))
	{
		if (IsNotAttackLab(m_lastEnemy) || !IsAlive(m_lastEnemy))
			SetLastEnemy(nullptr);
	}
	else
		SetLastEnemy(nullptr);

	CheckGrenadeThrow();

	// check if there are items needing to be used/collected
	if (m_itemCheckTime < engine->GetTime() || !FNullEnt(m_pickupItem))
	{
		FindItem();
		m_itemCheckTime = engine->GetTime() + g_gameVersion == HALFLIFE ? 1.0f : crandomint(2.0f, 4.0f);
	}

	if (m_isSlowThink)
		ApplyTaskFilters();
}

static TaskItem* taskFinal;

// initialize & calculate the desire for all actions based on distances, emotions and other stuff
void Bot::ApplyTaskFilters(void)
{
	GetCurrentTask();

	// bot found some item to use?
	if (!FNullEnt(m_pickupItem) && GetCurrentTaskID() != TASK_ESCAPEFROMBOMB)
	{
		m_states |= STATE_PICKUPITEM;
		if (m_pickupType == PICKTYPE_BUTTON)
			g_taskFilters[TASK_PICKUPITEM].desire = 50.0f; // always pickup button
		else
			g_taskFilters[TASK_PICKUPITEM].desire = cmax((512.0f - (GetEntityOrigin(m_pickupItem) - pev->origin).GetLength()) * 0.2f, 50.0f);
	}
	else
	{
		m_states &= ~STATE_PICKUPITEM;
		g_taskFilters[TASK_PICKUPITEM].desire = 0.0f;
	}

	// calculate desire to attack
	if ((m_states & STATE_SEEINGENEMY) && ReactOnEnemy())
		g_taskFilters[TASK_FIGHTENEMY].desire = TASKPRI_FIGHTENEMY;
	else
		g_taskFilters[TASK_FIGHTENEMY].desire = 0.0f;

	// blinded behavior
	if (m_blindTime > engine->GetTime())
		g_taskFilters[TASK_BLINDED].desire = TASKPRI_BLINDED;
	else
		g_taskFilters[TASK_BLINDED].desire = 0.0f;

	m_oldCombatDesire = HysteresisDesire(g_taskFilters[TASK_FIGHTENEMY].desire, 40.0f, 90.0f, m_oldCombatDesire);
	g_taskFilters[TASK_FIGHTENEMY].desire = m_oldCombatDesire;
	taskFinal = SubsumeDesire(&g_taskFilters[TASK_BLINDED], SubsumeDesire(&g_taskFilters[TASK_FIGHTENEMY], &g_taskFilters[TASK_PICKUPITEM]));
	if (!m_tasks.IsEmpty())
	{
		taskFinal = MaxDesire(taskFinal, GetCurrentTask());
		PushTask(taskFinal->id, taskFinal->desire, taskFinal->data, taskFinal->time, taskFinal->resume); // push the final behavior in our task stack to carry out
	}
}

// this function resets bot tasks stack, by removing all entries from the stack
void Bot::ResetTasks(void)
{
	m_tasks.Destroy();
}

void Bot::PushTask(const BotTask id, const float desire, const int data, const float time, const bool resume, const bool force)
{
	if (!force)
	{
		if (id == TASK_GOINGFORCAMP && !CampingAllowed())
			return;

		if (id == TASK_ESCAPEFROMBOMB && !g_bombPlanted)
			return;
	}

	if (!m_tasks.IsEmpty())
	{
		TaskItem& item = m_tasks.Last();
		if (item.id == id)
		{
			item.desire = desire;
			return;
		}
	}

	TaskItem item;
	item.id = id;
	item.desire = desire;
	item.data = data;
	item.time = time;
	item.resume = resume;

	m_tasks.Push(item);
	IgnoreCollisionShortly();

	if (GetCurrentTaskID() == TASK_CAMP)
		SelectBestWeapon();

	if (ebot_debuggoal.GetInt() != -1)
		m_chosenGoalIndex = ebot_debuggoal.GetInt();
	else
		m_chosenGoalIndex = GetCurrentTask()->data;
}

inline TaskItem* Bot::GetCurrentTask(void)
{
	if (m_tasks.IsEmpty())
	{
		m_tasks.Destroy();

		TaskItem task;
		task.id = TASK_NORMAL;
		task.desire = TASKPRI_NORMAL;
		task.data = -1;
		task.time = 0.0f;
		task.resume = true;

		m_tasks.Push(task);
	}

	return &m_tasks.Last();
}

// this function removes one task from the bot task stack
void Bot::RemoveCertainTask(const BotTask id)
{
	if (m_tasks.IsEmpty() || GetCurrentTaskID() == TASK_NORMAL)
		return; // since normal task can be only once on the stack, don't remove it...

	if (GetCurrentTaskID() == id)
	{
		m_tasks.Pop();
		return;
	}

	int16_t i;
	for (i = 0; i < m_tasks.Size(); i++)
	{
		if (m_tasks[i].id == id)
			m_tasks.RemoveAt(i);
	}
}

// this function called whenever a task is completed
void Bot::TaskComplete(void)
{
	if (m_tasks.IsEmpty())
		return;

	do
	{
		m_tasks.Pop();
	} while (!m_tasks.IsEmpty() && !m_tasks.Last().resume);
}

BotTask Bot::GetCurrentTaskID(void)
{
	taskFinal = GetCurrentTask();
	if (taskFinal == nullptr)
		return BotTask(-1);

	return taskFinal->id;
}

int Bot::GetCurrentGoalID(void)
{
	taskFinal = GetCurrentTask();
	if (taskFinal == nullptr)
		return -1;

	return taskFinal->data;
}

float Bot::GetCurrentTaskTime(void)
{
	taskFinal = GetCurrentTask();
	if (taskFinal == nullptr)
		return -1.0f;

	return taskFinal->time;
}

void Bot::CheckGrenadeThrow(void)
{
	if (IsZombieMode())
	{
		if (!m_isZombieBot)
			return;

		if (m_isSlowThink)
			return;
	}

	if (m_seeEnemyTime + 5.0f < engine->GetTime())
		return;

	if (ebot_knifemode.GetBool() || m_grenadeCheckTime > engine->GetTime() || m_isUsingGrenade || GetCurrentTaskID() == TASK_PLANTBOMB || GetCurrentTaskID() == TASK_DEFUSEBOMB || m_isReloading)
	{
		m_states &= ~(STATE_THROWEXPLODE | STATE_THROWFLASH | STATE_THROWSMOKE);
		return;
	}

	const int grenadeToThrow = CheckGrenades();
	if (grenadeToThrow == -1)
	{
		m_states &= ~(STATE_THROWEXPLODE | STATE_THROWFLASH | STATE_THROWSMOKE);
		m_grenadeCheckTime = engine->GetTime() + 5.0f;
		return;
	}

	edict_t* targetEntity = nullptr;
	if (GetGameMode() == MODE_BASE || IsDeathmatchMode())
		targetEntity = m_lastEnemy;
	else if (IsZombieMode())
	{
		if (m_isZombieBot && !FNullEnt(m_moveTargetEntity))
			targetEntity = m_moveTargetEntity;
		else if (!FNullEnt(m_enemy))
			targetEntity = m_enemy;
	}
	else if (!FNullEnt(m_enemy))
		targetEntity = m_enemy;

	if (FNullEnt(targetEntity))
	{
		m_states &= ~(STATE_THROWEXPLODE | STATE_THROWFLASH | STATE_THROWSMOKE);
		m_grenadeCheckTime = engine->GetTime()  + 1.0f;
		return;
	}

	const Vector targetOrigin = GetEntityOrigin(targetEntity);
	if ((grenadeToThrow == WEAPON_HEGRENADE || grenadeToThrow == WEAPON_SMGRENADE) && !(m_states & (STATE_SEEINGENEMY | STATE_THROWEXPLODE | STATE_THROWFLASH | STATE_THROWSMOKE)))
	{
		float distance = (targetOrigin - pev->origin).GetLengthSquared();

		// is enemy to high to throw
		if ((targetOrigin.z > (pev->origin.z + 650.0f)) || !(targetEntity->v.flags & (FL_PARTIALGROUND | FL_DUCKING)))
			distance = FLT_MAX; // just some crazy value

		// enemy is within a good throwing distance ?
		if (distance > (grenadeToThrow == WEAPON_SMGRENADE ? squaredf(400.0f) : squaredf(600.0f)) && distance < squaredf(800.0f))
		{
			bool allowThrowing = true;

			// check for teammates
			if (grenadeToThrow == WEAPON_HEGRENADE && GetGameMode() == MODE_BASE && m_numFriendsLeft > 0 && GetNearbyFriendsNearPosition(targetOrigin, 256.0f) > 0)
				allowThrowing = false;

			if (allowThrowing && m_seeEnemyTime + 2.0f < engine->GetTime())
			{
				Vector enemyPredict = (targetEntity->v.velocity * 0.5f).SkipZ() + targetOrigin;
				int searchTab[4], count = 4;

				float searchRadius = targetEntity->v.velocity.GetLength2D();

				// check the search radius
				if (searchRadius < 128.0f)
					searchRadius = 128.0f;

				// search waypoints
				g_waypoint->FindInRadius(enemyPredict, searchRadius, searchTab, &count);

				Vector src;
				while (count > 0)
				{
					allowThrowing = true;

					// check the throwing
					m_throw = g_waypoint->GetPath(searchTab[count--])->origin;
					src = CheckThrow(EyePosition(), m_throw);

					if (src.GetLengthSquared() < squaredf(100.0f))
						src = CheckToss(EyePosition(), m_throw);

					if (src == nullvec)
						allowThrowing = false;
					else
						break;
				}
			}

			// start explosive grenade throwing?
			if (allowThrowing)
			{
				if (grenadeToThrow == WEAPON_HEGRENADE)
					m_states |= STATE_THROWEXPLODE;
				else
					m_states |= STATE_THROWSMOKE;
			}
			else
			{
				if (grenadeToThrow == WEAPON_HEGRENADE)
					m_states &= ~STATE_THROWEXPLODE;
				else
					m_states &= ~STATE_THROWSMOKE;
			}
		}
	}
	else if (grenadeToThrow == WEAPON_FBGRENADE && (targetOrigin - pev->origin).GetLengthSquared() < squaredf(800.0f) && !(m_aimFlags & AIM_ENEMY))
	{
		bool allowThrowing = true;
		MiniArray <int> inRadius;

		Vector src;
		g_waypoint->FindInRadius(inRadius, 256.0f, (targetOrigin + (targetEntity->v.velocity * 0.5f).SkipZ()));

		uint16_t i;
		for (i = 0; i < inRadius.Size(); i++)
		{
			if (m_numFriendsLeft > 0 && GetNearbyFriendsNearPosition(g_waypoint->GetPath(i)->origin, 256.0f) > 0)
				continue;

			m_throw = g_waypoint->GetPath(i)->origin;
			src = CheckThrow(EyePosition(), m_throw);

			if (src.GetLengthSquared() <= squaredf(100.0f))
				src = CheckToss(EyePosition(), m_throw);

			if (src == nullvec)
				continue;

			allowThrowing = true;
			break;
		}

		// start concussion grenade throwing?
		if (allowThrowing)
			m_states |= STATE_THROWFLASH;
		else
			m_states &= ~STATE_THROWFLASH;
	} 

	const float randTime = crandomfloat(2.0f, 4.0f);

	if (m_states & STATE_THROWEXPLODE)
		PushTask(TASK_THROWHEGRENADE, TASKPRI_THROWGRENADE, -1, randTime, false);
	else if (m_states & STATE_THROWFLASH)
		PushTask(TASK_THROWFBGRENADE, TASKPRI_THROWGRENADE, -1, randTime, false);
	else if (m_states & STATE_THROWSMOKE)
		PushTask(TASK_THROWSMGRENADE, TASKPRI_THROWGRENADE, -1, randTime, false);
}

bool Bot::IsOnAttackDistance(edict_t* targetEntity, float distance)
{
	if (FNullEnt(targetEntity))
		return false;

	if ((pev->origin - GetEntityOrigin(targetEntity)).GetLengthSquared() < squaredf(distance))
		return true;

	return false;
}

bool Bot::ReactOnEnemy(void)
{
	// NO!
	if (IsOnLadder())
		return m_isEnemyReachable = false;

	if (FNullEnt(m_enemy))
		return m_isEnemyReachable = false;

	if (m_enemyReachableTimer < engine->GetTime())
	{
		const int ownIndex = IsValidWaypoint(m_currentWaypointIndex) ? m_currentWaypointIndex : (m_cachedWaypointIndex = g_waypoint->FindNearest(pev->origin, 999999.0f, -1, GetEntity()));
		const int enemyIndex = g_waypoint->FindNearest(m_enemy->v.origin, 999999.0f, -1, GetEntity());
		const auto currentWaypoint = g_waypoint->GetPath(ownIndex);

		if (m_isZombieBot)
		{
			if (ownIndex == enemyIndex)
			{
				m_isEnemyReachable = true;
				goto last;
			}
			else
				m_isEnemyReachable = false;

			if (currentWaypoint->flags & WAYPOINT_FALLRISK || currentWaypoint->flags & WAYPOINT_ZOMBIEPUSH)
				goto last;

			const float enemyDistance = (pev->origin - m_enemy->v.origin).GetLengthSquared();

			if (pev->flags & FL_DUCKING)
			{
				if (enemyDistance < squaredf(32.0f) || m_navNode.IsEmpty())
					m_isEnemyReachable = true;

				pev->speed = pev->maxspeed;
				m_moveSpeed = pev->maxspeed;

				goto last;
			}

			// end of the path, before repathing check the distance if we can reach to enemy
			if (m_navNode.IsEmpty())
			{
				m_isEnemyReachable = enemyDistance < squaredf(512.0f);
				if (m_isEnemyReachable)
					goto last;
			}
			else
			{
				float radius = pev->maxspeed;
				if (!(currentWaypoint->flags & WAYPOINT_FALLCHECK) && !(currentWaypoint->flags & WAYPOINT_FALLRISK))
					radius += currentWaypoint->radius * 4.0f;

				if (enemyDistance < squaredf(radius))
				{
					TraceResult tr{};
					TraceHull(pev->origin, GetPlayerHeadOrigin(m_enemy), true, head_hull, GetEntity(), &tr);

					if (tr.flFraction == 1.0f || (!FNullEnt(tr.pHit) && tr.pHit == m_enemy))
					{
						m_isEnemyReachable = true;
						goto last;
					}	
				}
			}
		}
		else
		{
			m_isEnemyReachable = false;
			if (IsZombieMode())
			{
				if (enemyIndex == ownIndex)
				{
					m_isEnemyReachable = true;
					goto last;
				}

				const Vector enemyVel = m_enemy->v.velocity;
				const float enemySpeed = cabsf(m_enemy->v.speed);

				const Vector enemyHead = GetPlayerHeadOrigin(m_enemy);
				const Vector myVec = pev->origin + pev->velocity * m_frameInterval;

				const float enemyDistance = (myVec - (enemyHead + enemyVel * m_frameInterval)).GetLengthSquared();

				extern ConVar ebot_zp_escape_distance;
				const float escapeDist =  squaredf(enemySpeed + ebot_zp_escape_distance.GetFloat());

				if (pev->flags & FL_DUCKING) // danger...
				{
					if (enemyDistance < escapeDist)
					{
						m_isEnemyReachable = true;
						goto last;
					}
				}
				else if (currentWaypoint->flags & WAYPOINT_FALLRISK)
					goto last;
				else if (GetCurrentTaskID() == TASK_CAMP)
				{
					if (enemyIndex == m_zhCampPointIndex)
						m_isEnemyReachable = true;
					else
					{
						if (enemyDistance < escapeDist)
						{
							int8_t j;
							Path* enemyWaypoint = g_waypoint->GetPath(enemyIndex);
							for (j = 0; j < Const_MaxPathIndex; j++)
							{
								if (enemyWaypoint->index[j] != -1 && enemyWaypoint->index[j] == ownIndex && !(enemyWaypoint->connectionFlags[j] & PATHFLAG_JUMP))
								{
									m_isEnemyReachable = true;
									break;
								}
							}

							if (!m_isEnemyReachable)
							{
								const Vector origin = GetBottomOrigin(GetEntity());
								TraceResult tr{};
								TraceLine(Vector(origin.x, origin.y, (origin.z + (pev->flags & FL_DUCKING) ? 6.0f : 12.0f)), enemyHead, true, true, GetEntity(), &tr);
								if (tr.flFraction == 1.0f)
								{
									m_isEnemyReachable = true;
									goto last;
								}
							}
						}
					}

					goto last;
				}
				else if (enemyDistance < escapeDist)
				{
					m_isEnemyReachable = true;
					goto last;
				}
			}
		}

	last:
		if (!m_isEnemyReachable && (m_isZombieBot || GetCurrentTaskID() != TASK_CAMP))
			m_enemyReachableTimer = engine->GetTime() + crandomfloat(0.15f, 0.35f);
		else
			m_enemyReachableTimer = engine->GetTime() + crandomfloat(0.25f, 0.55f);
	}

	if (m_isEnemyReachable)
	{
		m_navTimeset = engine->GetTime(); // override existing movement by attack movement
		return true;
	}

	return false;
}

void Bot::CheckRadioCommands(void)
{
	if (!m_isSlowThink)
		return;

	if (IsZombieMode())
		return;

	if (m_numFriendsLeft == 0)
		return;

	// don't allow bot listen you if bot is busy
	if (GetCurrentTaskID() == TASK_DEFUSEBOMB || GetCurrentTaskID() == TASK_PICKUPITEM || GetCurrentTaskID() == TASK_PLANTBOMB || HasHostage() || m_isBomber || m_isVIP)
	{
		m_radioOrder = 0;
		return;
	}

	if (FNullEnt(m_radioEntity) || !IsAlive(m_radioEntity))
		return;

	// dynamic range :)
	// less bots = more teamwork required
	if (crandomfloat(1.0f, 100.0f) <= 1.0f * m_numFriendsLeft * 0.33333333333f)
		return;

	const float distance = (GetEntityOrigin(m_radioEntity) - pev->origin).GetLengthSquared();

	switch (m_radioOrder)
	{
	case Radio_FollowMe:
	{
		if (IsVisible(GetTopOrigin(m_radioEntity), GetEntity()) && FNullEnt(m_enemy) && FNullEnt(m_lastEnemy))
		{
			int numFollowers = 0;

			// check if no more followers are allowed
			for (const auto& bot : g_botManager->m_bots)
			{
				if (bot != nullptr && bot->m_isAlive)
				{
					if (bot->m_targetEntity == m_radioEntity)
						numFollowers++;
				}
			}

			int allowedFollowers = ebot_followuser.GetInt();
			if (GetGameMode() == MODE_BASE && (m_isVIP || m_isBomber))
				allowedFollowers = ebot_followuser.GetInt() * ebot_followuser.GetInt();

			if (numFollowers < allowedFollowers)
			{
				RadioMessage(Radio_Affirmative);
				m_targetEntity = m_radioEntity;

				const BotTask taskID = GetCurrentTaskID();
				if (taskID == TASK_PAUSE || taskID == TASK_CAMP || taskID == TASK_GOINGFORCAMP)
					TaskComplete();

				DeleteSearchNodes();
				PushTask(TASK_FOLLOWUSER, TASKPRI_FOLLOWUSER, -1, 20.0f, true);
			}
		}
		else
			RadioMessage(Radio_Negative);

		break;
	}
	case Radio_StickTogether:
	{
		if (IsVisible(GetTopOrigin(m_radioEntity), GetEntity()) && FNullEnt(m_enemy) && FNullEnt(m_lastEnemy))
		{
			RadioMessage(Radio_Affirmative);
			m_targetEntity = m_radioEntity;

			// don't pause/camp/follow anymore
			const BotTask taskID = GetCurrentTaskID();
			if (taskID == TASK_PAUSE || taskID == TASK_CAMP || taskID == TASK_HIDE || taskID == TASK_GOINGFORCAMP)
				TaskComplete();

			DeleteSearchNodes();

			PushTask(TASK_FOLLOWUSER, TASKPRI_FOLLOWUSER, -1, 20.0f, true);
		}
		else if (FNullEnt(m_enemy) && FNullEnt(m_lastEnemy))
		{
			RadioMessage(Radio_Affirmative);

			m_position = GetEntityOrigin(m_radioEntity);

			DeleteSearchNodes();
			PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
		}
		else
			RadioMessage(Radio_Negative);

		break;
	}
	case Radio_CoverMe:
	{
		// check if line of sight to object is not blocked (i.e. visible)
		if (IsVisible(pev->origin, m_radioEntity))
		{
			RadioMessage(Radio_Affirmative);

			if (GetCurrentTaskID() == TASK_CAMP || GetCurrentTaskID() == TASK_PAUSE)
				return;

			const int campindex = FindDefendWaypoint(GetTopOrigin(m_radioEntity));
			if (!IsValidWaypoint(campindex))
				return;

			m_campposition = g_waypoint->GetPath(campindex)->origin;

			DeleteSearchNodes();
			PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, campindex, 9999.0f, true);
		}

		break;
	}
	case Radio_HoldPosition:
	{
		if (m_numEnemiesLeft > 0 && !g_waypoint->m_campPoints.IsEmpty())
		{
			const int index = FindDefendWaypoint(GetTopOrigin(m_radioEntity));
			if (IsValidWaypoint(index))
			{
				RadioMessage(Radio_Affirmative);

				m_campposition = g_waypoint->GetPath(index)->origin;

				DeleteSearchNodes();
				PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, 9999.0f, true);
			}
			else
				RadioMessage(Radio_Negative);
		}
		else
			RadioMessage(Radio_Negative);

		break;
	}
	case Radio_TakingFire:
	{
		if (FNullEnt(m_targetEntity))
		{
			if (FNullEnt(m_enemy) && m_seeEnemyTime + 10.0f < engine->GetTime())
			{
				// decrease fear levels to lower probability of bot seeking cover again
				m_fearLevel -= 0.2f;

				if (m_fearLevel < 0.0f)
					m_fearLevel = 0.0f;

				RadioMessage(Radio_Affirmative);
				m_targetEntity = m_radioEntity;
				m_position = GetEntityOrigin(m_radioEntity);

				DeleteSearchNodes();
				PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 10.0f, true);
			}
			else
				RadioMessage(Radio_Negative);
		}

		break;
	}
	case Radio_YouTakePoint:
	{
		if (IsVisible(GetTopOrigin(m_radioEntity), GetEntity()) && m_isLeader)
		{
			RadioMessage(Radio_Affirmative);

			if (g_bombPlanted)
				m_position = g_waypoint->GetBombPosition();
			else
				m_position = GetEntityOrigin(m_radioEntity);

			if (chanceof(50))
			{
				DeleteSearchNodes();
				PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
			}
			else
			{
				const int index = FindDefendWaypoint(m_position);
				if (IsValidWaypoint(index) && !IsWaypointOccupied(index))
				{
					m_campposition = g_waypoint->GetPath(index)->origin;
					DeleteSearchNodes();
					PushTask(TASK_GOINGFORCAMP, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
				}
				else
				{
					DeleteSearchNodes();
					PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
				}
			}

		}
		else
			RadioMessage(Radio_Negative);

		break;
	}
	case Radio_EnemySpotted:
	{
		if (m_personality == PERSONALITY_RUSHER && FNullEnt(m_enemy) && FNullEnt(m_lastEnemy)) // rusher bots will like that, they want fight!
		{
			RadioMessage(Radio_Affirmative);
			m_position = GetEntityOrigin(m_radioEntity);
			DeleteSearchNodes();
			PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
		}
		else if (m_waypoint.flags & WAYPOINT_GOAL) // he's in goal waypoint, its danger!!!
		{
			RadioMessage(Radio_Affirmative);
			m_position = GetEntityOrigin(m_radioEntity);
			DeleteSearchNodes();
			PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
		}

		break;
	}
	case Radio_NeedBackup:
	{
		if ((FNullEnt(m_enemy) && IsVisible(GetPlayerHeadOrigin(m_radioEntity), GetEntity()) || distance < squaredf(1536.0f) || !m_moveToC4) && m_seeEnemyTime + 5.0f < engine->GetTime())
		{
			m_fearLevel -= 0.1f;

			if (m_fearLevel < 0.0f)
				m_fearLevel = 0.0f;

			RadioMessage(Radio_Affirmative);

			m_targetEntity = m_radioEntity;
			m_position = GetEntityOrigin(m_radioEntity);

			DeleteSearchNodes();
			PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 10.0f, true);
		}
		else
			RadioMessage(Radio_Negative);

		break;
	}
	case Radio_GoGoGo:
	{
		if (m_radioEntity == m_targetEntity)
		{
			RadioMessage(Radio_Affirmative);

			m_targetEntity = nullptr;
			m_fearLevel -= 0.2f;

			if (m_fearLevel < 0.0f)
				m_fearLevel = 0.0f;

			if (GetCurrentTaskID() == TASK_CAMP || GetCurrentTaskID() == TASK_PAUSE || GetCurrentTaskID() == TASK_HIDE || GetCurrentTaskID() == TASK_GOINGFORCAMP)
				TaskComplete();
		}
		else if (FNullEnt(m_enemy) && IsVisible(GetTopOrigin(m_radioEntity), GetEntity()) || distance < squaredf(1536.0f))
		{
			const BotTask taskID = GetCurrentTaskID();
			if (taskID == TASK_PAUSE || taskID == TASK_CAMP)
			{
				TaskComplete();

				m_fearLevel -= 0.2f;

				if (m_fearLevel < 0.0f)
					m_fearLevel = 0.0f;

				RadioMessage(Radio_Affirmative);

				m_targetEntity = nullptr;
				m_position = GetTopOrigin(m_radioEntity) + (m_radioEntity->v.v_angle + (g_pGlobals->v_forward * 20));

				DeleteSearchNodes();
				PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
			}
		}
		else if (!FNullEnt(m_doubleJumpEntity))
		{
			RadioMessage(Radio_Affirmative);
			ResetDoubleJumpState();
		}
		else
			RadioMessage(Radio_Negative);
		break;
	}
	case Radio_ShesGonnaBlow:
	{
		if (FNullEnt(m_enemy) && distance < squaredf(2048.0f) && g_bombPlanted && m_team == TEAM_TERRORIST)
		{
			RadioMessage(Radio_Affirmative);
			if (GetCurrentTaskID() == TASK_CAMP || GetCurrentTaskID() == TASK_PAUSE)
				TaskComplete();

			m_targetEntity = nullptr;
			PushTask(TASK_ESCAPEFROMBOMB, TASKPRI_ESCAPEFROMBOMB, -1, GetBombTimeleft(), true);
		}
		else
			RadioMessage(Radio_Negative);
		break;
	}
	case Radio_RegroupTeam:
	{
		// if no more enemies found AND bomb planted, switch to knife to get to bombplace faster
		if (m_team == TEAM_COUNTER && m_currentWeapon != WEAPON_KNIFE && m_numEnemiesLeft == 0 && g_bombPlanted && GetCurrentTaskID() != TASK_DEFUSEBOMB)
		{
			SelectKnife();
			DeleteSearchNodes();
			m_position = g_waypoint->GetBombPosition();
			PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
			RadioMessage(Radio_Affirmative);
		}
		else if (m_team == TEAM_TERRORIST && m_numEnemiesLeft > 0 && g_bombPlanted && GetCurrentTaskID() != TASK_PLANTBOMB && GetCurrentTaskID() != TASK_CAMP)
		{
			TaskComplete();
			if (!m_isReloading)
				SelectBestWeapon();

			DeleteSearchNodes();
			m_position = g_waypoint->GetBombPosition();
			PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
			RadioMessage(Radio_Affirmative);
		}
		else if (FNullEnt(m_enemy) && FNullEnt(m_lastEnemy))
		{
			TaskComplete();
			if (!m_isReloading)
				SelectBestWeapon();

			DeleteSearchNodes();
			m_position = GetEntityOrigin(m_radioEntity);
			PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);
			RadioMessage(Radio_Affirmative);
		}
		else
			RadioMessage(Radio_Negative);

		break;
	}
	case Radio_StormTheFront:
	{
		if ((FNullEnt(m_enemy) && IsVisible(GetTopOrigin(m_radioEntity), GetEntity())) || distance < squaredf(1024.0f))
		{
			RadioMessage(Radio_Affirmative);

			const BotTask taskID = GetCurrentTaskID();
			if (taskID == TASK_PAUSE || taskID == TASK_CAMP)
				TaskComplete();

			m_targetEntity = nullptr;
			m_position = GetEntityOrigin(m_radioEntity) + (m_radioEntity->v.v_angle * crandomfloat(10.0f, 20.0f));

			DeleteSearchNodes();
			PushTask(TASK_MOVETOPOSITION, TASKPRI_MOVETOPOSITION, -1, 1.0f, true);

			m_fearLevel -= 0.3f;
			if (m_fearLevel < 0.0f)
				m_fearLevel = 0.0f;

			m_agressionLevel += 0.3f;
			if (m_agressionLevel > 1.0f)
				m_agressionLevel = 1.0f;
		}

		break;
	}
	case Radio_Fallback:
	{
		if ((FNullEnt(m_enemy) && IsVisible(GetPlayerHeadOrigin(m_radioEntity), GetEntity())) || distance < squaredf(1024.0f))
		{
			m_fearLevel += 0.5f;
			if (m_fearLevel > 1.0f)
				m_fearLevel = 1.0f;

			m_agressionLevel -= 0.5f;
			if (m_agressionLevel < 0.0f)
				m_agressionLevel = 0.0f;

			if (GetCurrentTaskID() == TASK_CAMP && !FNullEnt(m_lastEnemy))
			{
				RadioMessage(Radio_Negative);
				GetCurrentTask()->time += crandomfloat(ebot_camp_min.GetFloat(), ebot_camp_min.GetFloat());
			}
			else
			{
				const BotTask taskID = GetCurrentTaskID();
				if (taskID == TASK_PAUSE || taskID == TASK_CAMP || taskID == TASK_HIDE || taskID == TASK_HUNTENEMY || taskID == TASK_GOINGFORCAMP)
				{
					RadioMessage(Radio_Affirmative);
					TaskComplete();
				}

				m_targetEntity = nullptr;
				m_seeEnemyTime = engine->GetTime();

				// if bot has no enemy
				if (FNullEnt(m_lastEnemy))
				{
					float nearestDistance = 9999999.0f;
					float curDist;

					// take nearest enemy to ordering player
					for (const auto& client : g_clients)
					{
						if (FNullEnt(client.ent))
							continue;

						if (!(client.flags & CFLAG_USED) || !(client.flags & CFLAG_ALIVE) || client.team == m_team)
							continue;

						curDist = (GetEntityOrigin(m_radioEntity) - client.origin).GetLengthSquared2D();
						if (curDist < nearestDistance)
						{
							nearestDistance = curDist;
							m_lastEnemy = client.ent;
							m_lastEnemyOrigin = client.origin + pev->view_ofs;
						}
					}
				}
			}
		}

		break;
	}
	case Radio_ReportTeam:
	{
		switch (GetCurrentTaskID())
		{
		case TASK_NORMAL:
		{
			if (IsValidWaypoint(GetCurrentGoalID()))
			{
				if (!FNullEnt(m_enemy))
				{
					if (IsAlive(m_enemy))
						RadioMessage(Radio_EnemySpotted);
					else
						RadioMessage(Radio_EnemyDown);
				}
				else if (!FNullEnt(m_lastEnemy))
				{
					if (IsAlive(m_lastEnemy))
						RadioMessage(Radio_EnemySpotted);
					else
						RadioMessage(Radio_EnemyDown);
				}
				else if (m_seeEnemyTime + 10.0f > engine->GetTime())
					RadioMessage(Radio_EnemySpotted);
				else
					RadioMessage(Radio_SectorClear);
			}
			break;
		}
		case TASK_MOVETOPOSITION:
		{
			if (m_seeEnemyTime + 10.0f > engine->GetTime())
				RadioMessage(Radio_EnemySpotted);
			else if (FNullEnt(m_enemy) && FNullEnt(m_lastEnemy))
				RadioMessage(Radio_SectorClear);
			break;
		}
		case TASK_CAMP:
		{
			RadioMessage(Radio_InPosition);
			break;
		}
		case TASK_GOINGFORCAMP:
		{
			RadioMessage(Radio_HoldPosition);
			break;
		}
		case TASK_PLANTBOMB:
		{
			RadioMessage(Radio_HoldPosition);
			break;
		}
		case TASK_DEFUSEBOMB:
		{
			RadioMessage(Radio_CoverMe);
			break;
		}
		case TASK_FIGHTENEMY:
		{
			if (!FNullEnt(m_enemy))
			{
				if (IsAlive(m_enemy))
					RadioMessage(Radio_EnemySpotted);
				else
					RadioMessage(Radio_EnemyDown);
			}
			else if (m_seeEnemyTime + 10.0f > engine->GetTime())
				RadioMessage(Radio_EnemySpotted);
			break;
		}
		default:
		{
			if (chanceof(15))
				RadioMessage(Radio_ReportingIn);
			else if (chanceof(15))
				RadioMessage(Radio_FollowMe);
			else if (m_seeEnemyTime + 10.0f > engine->GetTime())
				RadioMessage(Radio_EnemySpotted);
			else
				RadioMessage(Radio_Negative);
			break;
		}
		}
		break;
	}
	case Radio_SectorClear:
	{
		if (m_seeEnemyTime + 10.0f > engine->GetTime())
		{
			const int random = crandomint(1, 12);
			switch (random)
			{
			case 1:
			{
				RadioMessage(Radio_EnemySpotted);
				break;
			}
			case 2:
			{
				RadioMessage(Radio_NeedBackup);
				break;
			}
			case 3:
			{
				RadioMessage(Radio_Fallback);
				break;
			}
			case 4:
			{
				RadioMessage(Radio_StickTogether);
				break;
			}
			case 5:
			{
				RadioMessage(Radio_YouTakePoint);
				break;
			}
			case 6:
			{
				RadioMessage(Radio_HoldPosition);
				break;
			}
			case 7:
			{
				RadioMessage(Radio_RegroupTeam);
				break;
			}
			case 8:
			{
				RadioMessage(Radio_CoverMe);
				break;
			}
			case 9:
			case 10:
			case 11:
			case 12:
			{
				if (!FNullEnt(m_enemy))
				{
					if (!IsAlive(m_enemy))
						RadioMessage(Radio_EnemyDown);
					else
						RadioMessage(Radio_TakingFire);
				}
			}
			}
		}
		else if (crandomint(1, 5) == 1)
		{
			if (crandomint(1, 3) == 1)
				RadioMessage(Radio_ReportTeam);
			else
				RadioMessage(Radio_Affirmative);
		}
	}
	case Radio_GetInPosition:
	{
		if ((FNullEnt(m_enemy) && IsVisible(GetTopOrigin(m_radioEntity), GetEntity())) || distance < squaredf(1024.0f))
		{
			RadioMessage(Radio_Affirmative);

			if (GetCurrentTaskID() == TASK_CAMP)
				GetCurrentTask()->time = engine->GetTime() + crandomfloat(ebot_camp_min.GetFloat(), ebot_camp_max.GetFloat());
			else
			{
				const BotTask taskID = GetCurrentTaskID();
				if (taskID == TASK_PAUSE || taskID == TASK_CAMP)
					TaskComplete();

				m_targetEntity = nullptr;
				m_seeEnemyTime = engine->GetTime();

				// if bot has no enemy
				if (FNullEnt(m_lastEnemy))
				{
					float nearestDistance = FLT_MAX;
					float dist;

					// take nearest enemy to ordering player
					for (const auto& client : g_clients)
					{
						if (FNullEnt(client.ent))
							continue;

						if (!(client.flags & CFLAG_USED) || !(client.flags & CFLAG_USED) || client.team == m_team)
							continue;

						dist = (GetEntityOrigin(m_radioEntity) - client.origin).GetLengthSquared();
						if (dist < nearestDistance)
						{
							nearestDistance = dist;
							m_lastEnemy = client.ent;
							m_lastEnemyOrigin = client.origin + pev->view_ofs;
						}
					}
				}

				const int index = FindDefendWaypoint(GetTopOrigin(m_radioEntity));
				if (IsValidWaypoint(index))
				{
					DeleteSearchNodes();
					m_campposition = g_waypoint->GetPath(index)->origin;
					PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, engine->GetTime() + 9999.0f, true);
				}
			}
		}
		break;
	}
	}

	m_radioOrder = 0; // radio command has been handled, reset
}

float Bot::GetWalkSpeed(void)
{
	if (!ebot_walkallow.GetBool() || g_gameVersion == HALFLIFE)
		return pev->maxspeed;

	if (IsZombieMode() || IsOnLadder() || m_numEnemiesLeft == 0 || 
		m_currentTravelFlags & PATHFLAG_JUMP || 
		pev->button & IN_JUMP || pev->oldbuttons & IN_JUMP || 
		pev->flags & FL_DUCKING || pev->button & IN_DUCK || pev->oldbuttons & IN_DUCK || IsInWater())
		return pev->maxspeed;

	return pev->maxspeed * 0.5f;
}

bool Bot::IsNotAttackLab(edict_t* entity)
{
	if (FNullEnt(entity))
		return true;

	if (entity->v.takedamage == DAMAGE_NO)
		return true;

	if (entity->v.rendermode == kRenderTransAlpha)
	{
		// enemy fires gun
		if ((entity->v.weapons & WeaponBits_Primary) || (entity->v.weapons & WeaponBits_Secondary) && (entity->v.button & IN_ATTACK) || (entity->v.oldbuttons & IN_ATTACK))
			return false;

		const float renderamt = entity->v.renderamt;
		if (renderamt < 30.0f)
			return true;

		if (renderamt > 160.0f)
			return false;

		return (squaredf(renderamt) < (GetEntityOrigin(entity) - pev->origin).GetLengthSquared2D());
	}

	return false;
}

void Bot::ChooseAimDirection(void)
{
	if (!m_canChooseAimDirection)
		return;

	if (m_aimFlags & AIM_OVERRIDE)
	{
		m_aimStopTime = 0.0f;
		m_lookAt = m_camp;
	}
	else if (m_aimFlags & AIM_GRENADE)
		m_lookAt = m_throw + Vector(0.0f, 0.0f, 1.0f * m_grenade.z);
	else if (m_aimFlags & AIM_ENEMY)
	{
		if (m_isZombieBot)
			m_lookAt = m_enemyOrigin;
		else
			FocusEnemy();

		if (m_currentWeapon == WEAPON_KNIFE)
			SelectBestWeapon();
	}
	else if (m_aimFlags & AIM_ENTITY)
		m_lookAt = m_entity;

	if (m_aimStopTime > engine->GetTime() && FNullEnt(m_enemy) && FNullEnt(m_breakableEntity))
		return;

	if (m_aimFlags & AIM_LASTENEMY)
	{
		if (IsZombieMode())
		{
			if (m_nextCampDirTime < engine->GetTime())
			{
				if (m_seeEnemyTime + 2.0f + m_difficulty > engine->GetTime() && m_lastEnemyOrigin != nullvec)
					m_camp = m_lastEnemyOrigin;
				else if (m_lastEnemyOrigin != nullvec && chanceof(30))
					m_camp = m_lastEnemyOrigin;
				else
				{
					const int aimIndex = GetCampAimingWaypoint();
					if (IsValidWaypoint(aimIndex))
						m_camp = g_waypoint->GetPath(aimIndex)->origin;
				}

				m_nextCampDirTime = engine->GetTime() + crandomfloat(1.5f, 5.0f);
			}

			m_lookAt = m_camp;
		}
		else
			m_lookAt = m_lastEnemyOrigin;
	}
	else if (m_aimFlags & AIM_CAMP)
	{
		m_aimFlags &= ~AIM_NAVPOINT;
		if (m_lastDamageOrigin != nullvec && m_damageTime + (static_cast<float>(m_skill + 55) * 0.05f) > engine->GetTime() && IsVisible(m_lastDamageOrigin, GetEntity()))
			m_lookAt = m_lastDamageOrigin;
		else if (m_lastEnemyOrigin != nullvec)
		{
			if (m_seeEnemyTime + (static_cast<float>(m_skill + 55) * 0.05f) > engine->GetTime() && IsVisible(m_lastEnemyOrigin, GetEntity()))
				m_lookAt = m_lastEnemyOrigin;

			if (m_nextCampDirTime < engine->GetTime())
			{
				if (m_seeEnemyTime + 2.0f + m_difficulty > engine->GetTime())
					m_camp = m_lastEnemyOrigin;
				else if (chanceof(30) && IsVisible(m_lastEnemyOrigin, GetEntity()))
					m_camp = m_lastEnemyOrigin;
				else if (chanceof(30) && IsVisible(m_lastDamageOrigin, GetEntity()))
					m_camp = m_lastDamageOrigin;
				else
				{
					const int aimIndex = GetCampAimingWaypoint();
					if (IsValidWaypoint(aimIndex))
						m_camp = g_waypoint->GetPath(aimIndex)->origin;
				}

				m_nextCampDirTime = engine->GetTime() + crandomfloat(1.5f, 5.0f);
			}
		}
		else if (m_nextCampDirTime < engine->GetTime())
		{
			if (m_lastDamageOrigin != nullvec && chanceof(30) && IsVisible(m_lastDamageOrigin, GetEntity()))
				m_camp = m_lastDamageOrigin;
			else
			{
				const int aimIndex = GetCampAimingWaypoint();
				if (IsValidWaypoint(aimIndex))
					m_camp = g_waypoint->GetPath(aimIndex)->origin;
			}

			m_nextCampDirTime = engine->GetTime() + crandomfloat(1.5f, 5.0f);
		}

		m_lookAt = m_camp;
	}
	else if (m_aimFlags & AIM_NAVPOINT)
	{
		if (!IsValidWaypoint(m_currentWaypointIndex))
			return;

		if (!FNullEnt(m_breakableEntity) && m_breakableEntity->v.health > 0.0f)
			m_lookAt = m_breakable;
		else if (m_waypoint.flags & WAYPOINT_USEBUTTON)
		{
			edict_t* button = FindButton();
			if (button != nullptr)
			{
				m_aimStopTime = 0.0f;
				m_lookAt = GetEntityOrigin(button);
				return;
			}
		}

		if (m_waypoint.flags & WAYPOINT_LADDER)
		{
			m_aimStopTime = 0.0f;
			m_lookAt = m_destOrigin + pev->view_ofs;
		}
	    else if (!m_isZombieBot && m_seeEnemyTime + 4.0f > engine->GetTime())
		{
			if (m_skill > 50)
				m_lookAt = GetEntityOrigin(m_lastEnemy);
			else
				m_lookAt = m_lastEnemyOrigin;

			if (m_lookAt == nullvec)
				m_lookAt = m_lastEnemyOrigin;

			m_aimStopTime = 0.0f;
		}
		else if (!m_navNode.IsEmpty() && m_navNode.HasNext())
			m_lookAt = g_waypoint->GetPath(m_navNode.Next())->origin + pev->view_ofs;
		else
			m_lookAt = m_destOrigin + pev->velocity + pev->view_ofs;
	}
}

void Bot::RunPlayer(void)
{
	if (pev != nullptr)
		(*g_engfuncs.pfnRunPlayerMove) (pev->pContainingEntity, m_moveAngles, m_moveSpeed, m_strafeSpeed, 0.0f, static_cast<uint16_t>(pev->button), static_cast<uint8_t>(pev->impulse), static_cast<uint8_t>(cclampf((engine->GetTime() - m_msecInterval) * 1000.0f, 0.0f, 255.0f)));
	m_msecInterval = engine->GetTime();
}

void Bot::Think(void)
{
	// i'm really tired of getting random pev is nullptr debug logs...
	// might seems ugly and useless, but... i have made some experiments with it,
	// still not sure exactly why, but bad third party server plugins can cause this
	if (pev == nullptr)
		return;

	pev->button = 0;
	m_moveSpeed = 0.0f;
	m_strafeSpeed = 0.0f;

	m_canChooseAimDirection = true;

	m_frameInterval = engine->GetTime() - m_lastThinkTime;
	m_lastThinkTime = engine->GetTime();

	if (m_slowthinktimer < engine->GetTime())
	{
		m_isSlowThink = true;
		SecondThink();

		if (m_stayTime <= 0.0f)
		{
			extern ConVar ebot_random_join_quit;
			if (ebot_random_join_quit.GetBool())
			{
				extern ConVar ebot_stay_min;
				extern ConVar ebot_stay_max;
				m_stayTime = engine->GetTime() + crandomfloat(ebot_stay_min.GetFloat(), ebot_stay_max.GetFloat());
			}
			else
				m_stayTime = engine->GetTime() + 999999.0f;
		}

		if (FNullEnt(m_breakableEntity) || m_breakableEntity->v.health <= 0.0f)
		{
			m_breakableEntity = nullptr;
			m_breakable = nullvec;
		}

		m_isZombieBot = IsZombieEntity(GetEntity());
		m_team = GetTeam(GetEntity());
		m_isAlive = IsAlive(GetEntity());
		m_isBomber = pev->weapons & (1 << WEAPON_C4);
		m_index = GetIndex();

		if (!FNullEnt(m_lastEnemy) && !IsAlive(m_lastEnemy))
			m_lastEnemy = nullptr;

		// at least walk randomly
		if (!IsZombieMode() && !IsValidWaypoint(GetCurrentGoalID()))
			m_chosenGoalIndex = crandomint(0, g_numWaypoints - 1);

		m_slowthinktimer = engine->GetTime() + crandomint(0.9f, 1.1f);
		CalculatePing();

		if (ebot_use_radio.GetInt() == 2)
		{
			if (m_chatterTimer < engine->GetTime())
				SwitchChatterIcon(false);
		}
	}
	else
		m_isSlowThink = false;

	// is bot movement enabled
	bool botMovement = false;

	if (m_notStarted) // if the bot hasn't selected stuff to start the game yet, go do that...
		StartGame(); // select team & class
	else if (!m_isAlive)
	{
		if (g_gameVersion == HALFLIFE && !(pev->oldbuttons & IN_ATTACK))
			pev->button |= IN_ATTACK;

		extern ConVar ebot_random_join_quit;
		if (ebot_random_join_quit.GetBool() && m_stayTime > 0.0f && m_stayTime < engine->GetTime())
		{
			Kick();
			return;
		}

		extern ConVar ebot_chat;
		if (ebot_chat.GetBool() && !RepliesToPlayer() && m_lastChatTime + 10.0f < engine->GetTime() && g_lastChatTime + 5.0f < engine->GetTime()) // bot chatting turned on?
		{
			m_lastChatTime = engine->GetTime();
			if (!g_chatFactory[CHAT_DEAD].IsEmpty())
			{
				g_lastChatTime = engine->GetTime();

				char* pickedPhrase = g_chatFactory[CHAT_DEAD].GetRandomElement();
				bool sayBufferExists = false;

				// search for last messages, sayed
				uint16_t i;
				for (i = 0; i < m_sayTextBuffer.lastUsedSentences.Size(); i++)
				{
					if (cstrncmp(m_sayTextBuffer.lastUsedSentences[i], pickedPhrase, m_sayTextBuffer.lastUsedSentences[i].GetLength()) == 0)
						sayBufferExists = true;
				}

				if (!sayBufferExists)
				{
					PrepareChatMessage(pickedPhrase);
					PushMessageQueue(CMENU_SAY);

					// add to ignore list
					m_sayTextBuffer.lastUsedSentences.Push(pickedPhrase);
				}

				// clear the used line buffer every now and then
				if (m_sayTextBuffer.lastUsedSentences.Size() > crandomint(4, 6))
					m_sayTextBuffer.lastUsedSentences.Destroy();
			}
		}
	}
	else
	{
		if (g_gameVersion == HALFLIFE)
		{
			// idk why ???
			if (pev->maxspeed < 10.0f)
			{
				const auto maxSpeed = g_engfuncs.pfnCVarGetPointer("sv_maxspeed");
				if (maxSpeed != nullptr)
					pev->maxspeed = maxSpeed->value;
				else // default is 270
					pev->maxspeed = 270.0f;
			}

			if (!ebot_stopbots.GetBool())
				botMovement = true;
		}
		else
		{
			if (!m_buyingFinished)
				ResetCollideState();

			if (m_buyingFinished && !(pev->maxspeed < 10.0f && GetCurrentTaskID() != TASK_PLANTBOMB && GetCurrentTaskID() != TASK_DEFUSEBOMB) && !ebot_stopbots.GetBool())
				botMovement = true;

			if (m_randomattacktimer < engine->GetTime() && !engine->IsFriendlyFireOn() && !HasHostage()) // simulate players with random knife attacks
			{
				if (m_isStuck && m_personality != PERSONALITY_CAREFUL)
				{
					if (m_personality == PERSONALITY_RUSHER)
						m_randomattacktimer = 0.0f;
					else
						m_randomattacktimer = engine->GetTime() + crandomfloat(0.1f, 10.0f);
				}
				else if (m_personality == PERSONALITY_RUSHER)
					m_randomattacktimer = engine->GetTime() + crandomfloat(0.1f, 30.0f);
				else if (m_personality == PERSONALITY_CAREFUL)
					m_randomattacktimer = engine->GetTime() + crandomfloat(10.0f, 100.0f);
				else
					m_randomattacktimer = engine->GetTime() + crandomfloat(0.15f, 75.0f);

				if (m_currentWeapon == WEAPON_KNIFE)
				{
					if (crandomint(1, 3) == 1)
						pev->button |= IN_ATTACK;
					else
						pev->button |= IN_ATTACK2;

					if (crandomint(1, 11) == 1)
						PlayChatterMessage(ChatterMessage::Happy);
				}
			}
		}
	}

	CheckMessageQueue(); // check for pending messages

	if (botMovement && m_isAlive)
	{
		BotAI();
		MoveAction();
		DebugModeMsg();
	}

	m_thinkDelay = engine->GetTime() + 0.1f;
}

void Bot::SecondThink(void)
{
	m_numEnemiesLeft = GetNearbyEnemiesNearPosition(pev->origin, 99999999.0f);

	if (g_gameVersion != HALFLIFE)
	{
		m_numFriendsLeft = GetNearbyFriendsNearPosition(pev->origin, 99999999.0f);
		if (IsZombieMode())
		{
			if (ebot_use_flare.GetBool() && !m_isZombieBot && pev->weapons & (1 << WEAPON_SMGRENADE) && !m_isReloading && chanceof(25) && FNullEnt(m_enemy) && !FNullEnt(m_lastEnemy))
			{
				float range;
				cvar_t* zpCvar = g_engfuncs.pfnCVarGetPointer("zp_flare_size");
				if (zpCvar != nullptr)
					range = squaredf(squaredf(zpCvar->value) * 0.5f);
				else
					range = squaredf(384.0f);

				if (!FindSmokeGrenadeThere(m_lastEnemyOrigin, range) && !FindSmokeGrenadeThere(m_lookAt, range))
					PushTask(TASK_THROWFLARE, TASKPRI_THROWGRENADE, -1, crandomfloat(0.6f, 0.9f), false);
			}

			// zp & biohazard flashlight support
			if (ebot_force_flashlight.GetBool() && !m_isZombieBot && !(pev->effects & EF_DIMLIGHT))
				pev->impulse = 100;
		}

		if (g_bombPlanted && m_team == TEAM_COUNTER && (pev->origin - g_waypoint->GetBombPosition()).GetLengthSquared() < squaredf(768.0f) && !IsBombDefusing(g_waypoint->GetBombPosition()))
			ResetTasks();
	}
	else
	{
		m_startAction = CMENU_IDLE;
		m_numFriendsLeft = 0;
	}

	if (m_voteMap != 0) // host wants the bots to vote for a map?
	{
		FakeClientCommand(GetEntity(), "votemap %d", m_voteMap);
		m_voteMap = 0;
	}
}

void Bot::CalculatePing(void)
{
	extern ConVar ebot_ping;
	if (!ebot_ping.GetBool())
		return;

	// save cpu power if no one is lookin' at scoreboard...
	if (g_fakePingUpdate < engine->GetTime())
		return;

	int averagePing = 0;
	int numHumans = 0;
	int ping, loss, j, botPing;

	for (const auto& client : g_clients)
	{
		if (client.index < 0)
			continue;

		if (IsValidBot(client.index))
			continue;

		numHumans++;
		PLAYER_CNX_STATS(client.ent, &ping, &loss);

		if (ping <= 0 || ping > 150)
			ping = crandomint(5, 50);

		averagePing += ping;
	}

	if (numHumans > 0)
		averagePing /= numHumans;
	else
		averagePing = crandomint(30, 60);

	botPing = m_basePingLevel + crandomint(averagePing - averagePing * 0.2f, averagePing + averagePing * 0.2f) + crandomint(m_difficulty + 3, m_difficulty + 6);
	if (botPing <= 9)
		botPing = crandomint(9, 19);
	else if (botPing > 133)
		botPing = crandomint(99, 119);

	for (j = 0; j < 2; j++)
	{
		for (m_pingOffset[j] = 0; m_pingOffset[j] < 4; m_pingOffset[j]++)
		{
			if ((botPing - m_pingOffset[j]) % 4 == 0)
			{
				m_ping[j] = (botPing - m_pingOffset[j]) * 0.25f;
				break;
			}
		}
	}

	m_ping[2] = botPing;
}

void Bot::MoveAction(void)
{
	// careful bots will stop moving when reloading if they see enemy before, they will scared!
	if (m_personality == PERSONALITY_CAREFUL && m_isReloading && FNullEnt(m_enemy) && !FNullEnt(m_lastEnemy) && IsAlive(m_lastEnemy) && !IsZombieMode())
	{
		m_moveSpeed = 0.0f;
		m_strafeSpeed = 0.0f;
		ResetCollideState();
		return;
	}

	if (!(pev->button & (IN_FORWARD | IN_BACK)))
	{
		if (m_moveSpeed > 0)
			pev->button |= IN_FORWARD;
		else if (m_moveSpeed < 0)
			pev->button |= IN_BACK;
	}

	if (!(pev->button & (IN_MOVELEFT | IN_MOVERIGHT)))
	{
		if (m_strafeSpeed > 0)
			pev->button |= IN_MOVERIGHT;
		else if (m_strafeSpeed < 0)
			pev->button |= IN_MOVELEFT;
	}
}

void Bot::TaskNormal(void)
{
	m_aimFlags |= AIM_NAVPOINT;
	if (IsZombieMode())
	{
		// we're stuck while trying reach to human camp waypoint?
		if (m_isStuck)
		{
			if (IsValidWaypoint(m_zhCampPointIndex) && (g_waypoint->GetPath(m_zhCampPointIndex)->origin - pev->origin).GetLengthSquared() < squaredf(8.0f + g_waypoint->GetPath(m_zhCampPointIndex)->radius))
			{
				TraceResult tr2{};
				TraceLine(pev->origin, g_waypoint->GetPath(m_zhCampPointIndex)->origin, false, false, GetEntity(), &tr2);

				// nothing blocking visibility, we can camp at here if we're blocked by own teammates
				if (tr2.flFraction == 1.0f)
					PushTask(TASK_CAMP, TASKPRI_CAMP, m_zhCampPointIndex, engine->GetTime() + 99999.0f, false);
			}
		}
	}
	else if (g_gameVersion != HALFLIFE)
	{
		if (GetGameMode() == MODE_BASE)
		{
			if ((m_waypoint.flags & WAYPOINT_CAMP || m_waypoint.flags & WAYPOINT_GOAL) && !g_bombPlanted && !HasHostage() && !m_isZombieBot && !m_isBomber && !m_isVIP && chanceof(m_personality == PERSONALITY_RUSHER ? 7 : m_personality == PERSONALITY_CAREFUL ? 35 : 15))
			{
				const int index = FindDefendWaypoint(m_waypoint.origin);
				if (IsValidWaypoint(index))
				{
					m_campposition = g_waypoint->GetPath(index)->origin;
					PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, ebot_camp_max.GetFloat(), false);
				}
			}

			if (m_team == TEAM_COUNTER)
			{
				if (g_mapType & MAP_CS)
				{
					const int hostageWptIndex = FindHostage();
					if (IsValidWaypoint(hostageWptIndex) && m_currentWaypointIndex != hostageWptIndex)
						GetCurrentTask()->data = hostageWptIndex;
					else // no hostage? search goal waypoints
					{
						const int goalindex = g_waypoint->m_goalPoints.Random();
						if (IsValidWaypoint(goalindex))
						{
							if (m_isStuck || m_currentWaypointIndex == goalindex)
								GetCurrentTask()->data = goalindex;
						}
					}
				}
			}
		}

		if (ebot_walkallow.GetBool() && engine->IsFootstepsOn() && m_moveSpeed != 0.0f && !(m_aimFlags & AIM_ENEMY) && m_seeEnemyTime + 13.0f > engine->GetTime() && !g_bombPlanted)
		{
			if (FNullEnt(m_enemy)) // don't walk if theres a enemy
			{
				m_moveSpeed = GetWalkSpeed();
				if (m_currentWeapon == WEAPON_KNIFE)
					SelectBestWeapon();
			}
			else
				m_moveSpeed = pev->maxspeed;
		}

		// bot hasn't seen anything in a long time and is asking his teammates to report in or sector clear
		if ((GetGameMode() == MODE_BASE || GetGameMode() == MODE_TDM) && m_seeEnemyTime != 0.0f && m_seeEnemyTime + crandomfloat(30.0f, 80.0f) < engine->GetTime() && chanceof(70) && g_timeRoundStart + 20.0f < engine->GetTime() && m_askCheckTime + crandomfloat(20.0, 30.0f) < engine->GetTime())
		{
			m_askCheckTime = engine->GetTime();
			if (chanceof(40))
				RadioMessage(Radio_SectorClear);
			else
				RadioMessage(Radio_ReportTeam);
		}
	}

	// user forced a waypoint as a goal?
	if (IsValidWaypoint(ebot_debuggoal.GetInt()))
	{
		// check if we reached it
		if (IsVisible(g_waypoint->GetPath(ebot_debuggoal.GetInt())->origin, GetEntity()) && (m_waypoint.origin - pev->origin).GetLengthSquared() < squaredf(20.0f) && GetCurrentGoalID() == ebot_debuggoal.GetInt())
		{
			m_moveSpeed = 0.0f;
			m_strafeSpeed = 0.0f;
			m_checkTerrain = false;
			m_moveToGoal = false;
			return; // we can safely return here
		}

		if (GetCurrentTask()->data != ebot_debuggoal.GetInt())
		{
			DeleteSearchNodes();
			GetCurrentTask()->data = ebot_debuggoal.GetInt();
		}
	}

	// this also checks bot has shield or not ?
	if (IsShieldDrawn())
		pev->button |= IN_ATTACK2;
	else
	{
		if (m_currentWeapon == WEAPON_KNIFE && !FNullEnt(m_enemy) && (pev->origin - m_enemyOrigin).GetLengthSquared() < squaredf(198.0f))
		{
			if (m_knifeAttackTime < engine->GetTime())
			{
				KnifeAttack();
				m_knifeAttackTime = engine->GetTime() + crandomfloat(2.6f, 3.8f);
			}
		}
		else if (m_reloadState == RSTATE_NONE && GetAmmo() != 0)
			m_reloadState = RSTATE_PRIMARY;
	}

	// reached the destination (goal) waypoint?
	if (DoWaypointNav())
	{
		TaskComplete();
		m_prevGoalIndex = -1;

		if (GetGameMode() == MODE_ZP && !m_isZombieBot)
			ZmCampPointAction(1);
		else if (GetGameMode() == MODE_BASE)
		{
			// reached waypoint is a camp waypoint
			if (m_waypoint.flags & WAYPOINT_CAMP)
			{
				// check if bot has got a primary weapon and hasn't camped before
				if (HasPrimaryWeapon() && m_timeCamping + 10.0f < engine->GetTime() && !HasHostage())
				{
					bool campingAllowed = true;

					// Check if it's not allowed for this team to camp here
					if (m_team == TEAM_TERRORIST)
					{
						if (m_waypoint.flags & WAYPOINT_COUNTER)
							campingAllowed = false;
					}
					else
					{
						if (m_waypoint.flags & WAYPOINT_TERRORIST)
							campingAllowed = false;
					}

					// don't allow vip on as_ maps to camp + don't allow terrorist carrying c4 to camp
					if (((g_mapType & MAP_AS) && *(INFOKEY_VALUE(GET_INFOKEYBUFFER(GetEntity()), "model")) == 'v') || ((g_mapType & MAP_DE) && m_team == TEAM_TERRORIST && !g_bombPlanted && (m_isBomber)))
						campingAllowed = false;

					// check if another bot is already camping here
					if (!IsValidWaypoint(m_currentWaypointIndex) || IsWaypointOccupied(m_currentWaypointIndex))
						campingAllowed = false;

					if (campingAllowed)
					{
						SelectBestWeapon();

						if (!m_reloadState)
							m_reloadState = RSTATE_PRIMARY;

						MakeVectors(pev->v_angle);

						m_timeCamping = engine->GetTime() + crandomfloat(g_skillTab[m_skill / 20].campStartDelay, g_skillTab[m_skill / 20].campEndDelay);
						PushTask(TASK_CAMP, TASKPRI_CAMP, -1, m_timeCamping, true);
						m_aimFlags |= AIM_CAMP;

						// tell the world we're camping
						if (chanceof(90))
							RadioMessage(Radio_InPosition);

						m_moveToGoal = false;
						m_checkTerrain = false;

						m_moveSpeed = 0.0f;
						m_strafeSpeed = 0.0f;
					}
				}
			}
			// some goal waypoints are map dependant so check it out...
			else if (g_mapType & MAP_CS)
			{
				if (m_team == TEAM_TERRORIST)
				{
					if (m_skill >= 80 || crandomint(0, 100) < m_skill)
					{
						const int index = FindDefendWaypoint(m_waypoint.origin);
						if (IsValidWaypoint(index))
						{
							m_campposition = g_waypoint->GetPath(index)->origin;
							PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, engine->GetTime() + ebot_camp_max.GetFloat(), true);
						}
					}
				}
				else if (m_team == TEAM_COUNTER)
				{
					if (HasHostage())
					{
						// and reached a Rescue Point?
						if (m_waypoint.flags & WAYPOINT_RESCUE)
						{
							for (auto& hostage : m_hostages)
							{
								if (FNullEnt(hostage))
									continue;

								if (g_waypoint->GetPath(g_waypoint->FindNearest(GetEntityOrigin(hostage)))->flags & WAYPOINT_RESCUE)
									hostage = nullptr;
							}
						}
					}
				}
			}
			else if ((g_mapType & MAP_DE) && ((m_waypoint.flags & WAYPOINT_GOAL) || m_inBombZone) && FNullEnt(m_enemy))
			{
				// is it a terrorist carrying the bomb?
				if (m_isBomber)
				{
					if ((m_states & STATE_SEEINGENEMY) && m_numFriendsLeft > 0 && m_numEnemiesLeft > 0 && GetNearbyFriendsNearPosition(pev->origin, 768.0f) == 0)
					{
						// request an help also
						RadioMessage(Radio_NeedBackup);
						const int index = FindDefendWaypoint(pev->origin);
						if (IsValidWaypoint(index))
						{
							m_campposition = g_waypoint->GetPath(index)->origin;
							PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, engine->GetTime() + ebot_camp_max.GetFloat(), true);
						}
					}
					else
						PushTask(TASK_PLANTBOMB, TASKPRI_PLANTBOMB, -1, 0.0, false);
				}
				else if (m_team == TEAM_COUNTER && m_timeCamping + 10.0f < engine->GetTime())
				{
					if (!g_bombPlanted && chanceof(60) && GetNearbyFriendsNearPosition(pev->origin, 250.0f) < 4)
					{
						m_timeCamping = engine->GetTime() + crandomfloat(g_skillTab[m_skill / 20].campStartDelay, g_skillTab[m_skill / 20].campEndDelay);
						const int index = FindDefendWaypoint(m_waypoint.origin);
						if (IsValidWaypoint(index))
						{
							m_campposition = g_waypoint->GetPath(index)->origin;
							PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, engine->GetTime() + ebot_camp_max.GetFloat(), true); // push camp task on to stack
						}
					}
				}
			}
		}
	}
	else if (!GoalIsValid()) // no more nodes to follow - search new ones (or we have a momb)
	{
		int destIndex = FindGoal();

		// do pathfinding if it's not the current waypoint
		if (IsValidWaypoint(destIndex))
		{
			m_prevGoalIndex = destIndex;
			GetCurrentTask()->data = destIndex;
			FindPath(m_currentWaypointIndex, destIndex);
		}
	}
	else
		m_moveSpeed = pev->maxspeed;
}

// this is core function that handle task execution
void Bot::RunTask(void)
{
	switch (GetCurrentTaskID())
	{
	case TASK_NORMAL:
	{
		TaskNormal();
		break;
	}
	case TASK_HUNTENEMY:
	{
		m_aimFlags |= AIM_NAVPOINT;
		m_checkTerrain = true;

		// if we've got new enemy...
		if (!FNullEnt(m_enemy) || FNullEnt(m_lastEnemy))
		{
			// forget about it...
			TaskComplete();
			m_prevGoalIndex = -1;

			SetLastEnemy(nullptr);
		}
		else if (GetTeam(m_lastEnemy) == m_team)
		{
			// don't hunt down our teammate...
			RemoveCertainTask(TASK_HUNTENEMY);
			m_prevGoalIndex = -1;
		}
		else if (DoWaypointNav()) // reached last enemy pos?
		{
			// forget about it...
			TaskComplete();
			m_prevGoalIndex = -1;

			SetLastEnemy(nullptr);
		}
		else if (!GoalIsValid()) // do we need to calculate a new path?
		{
			DeleteSearchNodes();
			int destIndex;

			// is there a remembered index?
			if (IsValidWaypoint(GetCurrentGoalID()))
				destIndex = GetCurrentGoalID();
			else // no. we need to find a new one
				destIndex = GetEntityWaypoint(m_lastEnemy);

			// remember index
			m_prevGoalIndex = destIndex;
			GetCurrentTask()->data = destIndex;

			if (destIndex != m_currentWaypointIndex && IsValidWaypoint(destIndex))
				FindPath(m_currentWaypointIndex, destIndex);
		}

		// bots skill higher than 50?
		if (m_skill > 50 && engine->IsFootstepsOn())
		{
			// then make him move slow if near enemy
			if (!(m_currentTravelFlags & PATHFLAG_JUMP) && !m_isStuck && !m_isReloading &&
				m_currentWeapon != WEAPON_KNIFE &&
				m_currentWeapon != WEAPON_HEGRENADE &&
				m_currentWeapon != WEAPON_C4 &&
				m_currentWeapon != WEAPON_FBGRENADE &&
				m_currentWeapon != WEAPON_SHIELDGUN &&
				m_currentWeapon != WEAPON_SMGRENADE)
			{
				if (IsValidWaypoint(m_currentWaypointIndex))
				{
					if (m_waypoint.radius < 32 && !IsOnLadder() && !IsInWater() && m_seeEnemyTime + 4.0f > engine->GetTime() && m_skill < 80)
						m_duckTime = engine->GetTime() + 1.0f;
				}

				if (!FNullEnt(m_lastEnemy) && IsAlive(m_lastEnemy) && (m_lastEnemyOrigin - pev->origin).GetLengthSquared() <= squaredf(768.0f) && !(pev->flags & FL_DUCKING))
				{
					m_moveSpeed = GetWalkSpeed();
					if (m_currentWeapon == WEAPON_KNIFE)
						SelectBestWeapon();
				}
			}
		}
		break;
	}
	case TASK_FIGHTENEMY:
	{
		if (IsZombieMode() && !m_isZombieBot)
		{
			TaskNormal();
			return;
		}

		m_moveToGoal = true;
		m_checkTerrain = true;

		if (!FNullEnt(m_enemy) || m_seeEnemyTime + 0.54f > engine->GetTime())
			CombatFight();
		else
		{
			if (!m_isZombieBot || !(m_waypoint.flags & WAYPOINT_ZOMBIEPUSH))
			{
				SetEntityWaypoint(GetEntity());
				m_currentWaypointIndex = -1;
				DeleteSearchNodes();
				GetValidWaypoint();
			}

			TaskComplete();
		}

		m_navTimeset = engine->GetTime();
		break;
	}
	case TASK_PAUSE:
	{
		m_moveToGoal = false;
		m_checkTerrain = false;

		m_navTimeset = engine->GetTime();
		m_moveSpeed = 0.0f;
		m_strafeSpeed = 0.0f;
		m_aimFlags |= AIM_NAVPOINT;

		// is bot blinded and above average skill?
		if (m_viewDistance < 500.0f && m_skill > 50)
		{
			// go mad!
			m_moveSpeed = -cabsf((m_viewDistance - 500.0f) * 0.5f);

			if (m_moveSpeed < -pev->maxspeed)
				m_moveSpeed = -pev->maxspeed;

			MakeVectors(pev->v_angle);
			m_camp = EyePosition() + (g_pGlobals->v_forward * 500);

			m_aimFlags |= AIM_OVERRIDE;
			m_wantsToFire = true;
		}

		// stop camping if time over or gets hurt by something else than bullets
		if (GetCurrentTaskTime() < engine->GetTime())
			TaskComplete();
		break;
	}
	case TASK_BLINDED:
	{
		if (IsZombieMode() && !m_isZombieBot) // humans don't get flashed in biohazard
		{
			TaskComplete();
			return;
		}

		m_moveToGoal = false;
		m_checkTerrain = false;
		m_navTimeset = engine->GetTime();

		// if bot remembers last enemy position
		if (m_skill > 70 && m_lastEnemyOrigin != nullvec && IsValidPlayer(m_lastEnemy) && !UsesSniper())
		{
			m_lookAt = m_lastEnemyOrigin; // face last enemy
			m_wantsToFire = true; // and shoot it
		}

		if (!IsValidWaypoint(m_blindCampPoint))
		{
			m_moveSpeed = m_blindMoveSpeed;
			m_strafeSpeed = m_blindSidemoveSpeed;
			pev->button |= m_blindButton;
		}
		else
		{
			m_moveToGoal = true;
			DeleteSearchNodes();
			m_campposition = g_waypoint->GetPath(m_blindCampPoint)->origin;
			PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, -1, engine->GetTime() + ebot_camp_max.GetFloat(), true);
			m_blindCampPoint = -1;
		}

		if (m_blindTime < engine->GetTime())
			TaskComplete();

		break;
	}
	case TASK_GOINGFORCAMP:
	{
		if (m_isBomber || HasHostage())
		{
			TaskComplete();
			return;
		}

		m_aimFlags |= AIM_NAVPOINT;
		if (m_campposition == nullvec || m_campposition == -1) // we cant...
		{
			m_campposition = g_waypoint->GetPath(g_waypoint->m_campPoints.Random())->origin; // get random camping waypoint
			return;
		}

		if (DoWaypointNav() && (pev->origin - m_campposition).GetLengthSquared() < squaredf(21.0f)) // reached destination?
		{
			TaskComplete();
			RemoveCertainTask(TASK_GOINGFORCAMP); // we're done

			if (IsZombieMode())
				PushTask(TASK_CAMP, TASKPRI_CAMP, m_currentWaypointIndex, engine->GetTime() + 99999.0f, false);
			else
				PushTask(TASK_CAMP, TASKPRI_CAMP, m_currentWaypointIndex, engine->GetTime() + crandomfloat(ebot_camp_min.GetFloat(), ebot_camp_max.GetFloat()), false);
		}
		else if (!GoalIsValid()) // didn't choose goal waypoint yet?
		{
			int destIndex = GetCurrentGoalID();
			if (!IsValidWaypoint(destIndex) || (!(g_waypoint->m_paths[destIndex].flags & WAYPOINT_CAMP) && !(g_waypoint->m_paths[destIndex].flags & WAYPOINT_HMCAMPMESH) && !(g_waypoint->m_paths[destIndex].flags & WAYPOINT_ZMHMCAMP)))
				destIndex = g_waypoint->FindNearest(m_campposition);

			if (IsValidWaypoint(destIndex))
			{
				m_prevGoalIndex = destIndex;
				GetCurrentTask()->data = destIndex;
				FindPath(m_currentWaypointIndex, destIndex);
			}
			else
				TaskComplete();
		}
		break;
	}
	case TASK_CAMP:
	{
		if (GetGameMode() == MODE_BASE && (g_mapType & MAP_DE))
		{
			if (OutOfBombTimer())
			{
				TaskComplete();
				PushTask(TASK_ESCAPEFROMBOMB, TASKPRI_ESCAPEFROMBOMB, -1, 2.0f, true);
				return;
			}
		}

		if (!CampingAllowed())
		{
			TaskComplete();
			return;
		}

		if (IsZombieMode())
		{
			if (m_isSlowThink && crandomint(1, 3) == 1)
				SelectBestWeapon();

			m_aimFlags |= AIM_CAMP;

			// standing still
			if (!FNullEnt(m_enemy) && m_currentWeapon != WEAPON_KNIFE && m_personality != PERSONALITY_RUSHER && pev->velocity.GetLengthSquared2D() < pev->maxspeed)
			{
				bool crouch = true;
				if (m_currentWeapon != WEAPON_M3 ||
					m_currentWeapon != WEAPON_XM1014 ||
					m_currentWeapon != WEAPON_G3SG1 ||
					m_currentWeapon != WEAPON_SCOUT ||
					m_currentWeapon != WEAPON_AWP ||
					m_currentWeapon != WEAPON_M249 ||
					m_currentWeapon != WEAPON_SG550)
					crouch = false;

				if (m_personality == PERSONALITY_CAREFUL)
				{
					extern ConVar ebot_zp_escape_distance;
					if ((pev->origin - m_enemyOrigin).GetLengthSquared() < squaredf(ebot_zp_escape_distance.GetFloat() * 2.0f))
						crouch = false;
				}

				if (crouch && IsVisible(pev->origin - Vector(0.0f, 0.0f, 18.0f), m_enemy))
					m_duckTime = engine->GetTime() + m_frameInterval;
			}
		}
		else
		{
			SelectBestWeapon();
			m_aimFlags |= AIM_CAMP;
		}

		// fix
		if (GetGameMode() == MODE_BASE && (g_mapType & MAP_DE))
		{
			if (m_team == TEAM_COUNTER && m_defendedBomb && !IsBombDefusing(g_waypoint->GetBombPosition()) && !OutOfBombTimer())
			{
				m_defendedBomb = false;
				TaskComplete();
				break;
			}
		}

		if (m_isSlowThink && m_seeEnemyTime + 6.0f < engine->GetTime())
		{
			extern ConVar ebot_chat;
			if (ebot_chat.GetBool() && !RepliesToPlayer() && m_lastChatTime + 10.0f < engine->GetTime() && g_lastChatTime + 5.0f < engine->GetTime()) // bot chatting turned on?
				m_lastChatTime = engine->GetTime();
		}

		if (IsZombieMode() && IsValidWaypoint(m_zhCampPointIndex))
		{
			const Path zhPath = g_waypoint->m_paths[m_zhCampPointIndex];
			const float maxRange = zhPath.flags & WAYPOINT_CROUCH ? 128.0f : 256.0f;
			if (zhPath.mesh != 0 && ((zhPath.origin - pev->origin).GetLengthSquared2D() > squaredf(maxRange) || (zhPath.origin.z - 64.0f > pev->origin.z)))
			{
				m_zhCampPointIndex = -1;
				TaskComplete();
				GetValidWaypoint();
				break;
			}

			if (!g_waypoint->m_hmMeshPoints.IsEmpty() && GetCurrentTaskTime() > engine->GetTime() + 60.0f)
			{
				MiniArray <int16_t> MeshWaypoints;
				int16_t i, index;
				Path* pointer;
				for (i = 0; i < g_waypoint->m_hmMeshPoints.Random(); i++)
				{
					index = g_waypoint->m_hmMeshPoints.Get(i);
					pointer = g_waypoint->GetPath(index);
					if (pointer->mesh == 0)
						continue;

					if (zhPath.mesh != pointer->mesh)
						continue;

					MeshWaypoints.Push(index);
				}

				if (!MeshWaypoints.IsEmpty())
				{
					const int myCampPoint = MeshWaypoints.Random();
					m_chosenGoalIndex = myCampPoint;
					m_prevGoalIndex = myCampPoint;
					m_myMeshWaypoint = myCampPoint;
					MeshWaypoints.Destroy();

					float max = 12.0f;
					if (!FNullEnt(m_enemy))
					{
						if (m_personality == PERSONALITY_RUSHER)
							max = 16.0f;
						else if (m_personality != PERSONALITY_CAREFUL)
							max = 8.0f;
					}

					GetCurrentTask()->time = engine->GetTime() + crandomfloat(4.0f, max);
					FindPath(m_currentWaypointIndex, m_myMeshWaypoint);
				}
			}
		}

		m_checkTerrain = false;
		m_moveToGoal = false;

		ResetCollideState();

		m_navTimeset = engine->GetTime();
		m_timeCamping = engine->GetTime();

		m_moveSpeed = 0.0f;
		m_strafeSpeed = 0.0f;

		GetValidWaypoint();

		if (m_waypoint.flags & WAYPOINT_CROUCH)
			pev->button |= IN_DUCK;

		// stop camping if time over or gets hurt by something else than bullets
		if (GetCurrentTaskTime() < engine->GetTime())
			TaskComplete();
		break;
	}
	case TASK_HIDE:
	{
		if (m_isZombieBot)
		{
			TaskComplete();
			break;
		}

		m_aimFlags |= AIM_LASTENEMY;
		m_checkTerrain = false;
		m_moveToGoal = false;

		m_navTimeset = engine->GetTime();
		m_moveSpeed = 0;
		m_strafeSpeed = 0.0f;

		GetValidWaypoint();

		if (HasShield() && !m_isReloading)
		{
			if (!IsShieldDrawn())
				pev->button |= IN_ATTACK2; // draw the shield!
			else
				pev->button |= IN_DUCK; // duck under if the shield is already drawn
		}

		// if we see an enemy and aren't at a good camping point leave the spot
		if ((m_states & STATE_SEEINGENEMY) || m_inBombZone)
		{
			if (!(m_waypoint.flags & WAYPOINT_CAMP))
			{
				TaskComplete();
				m_prevGoalIndex = -1;

				if (!FNullEnt(m_enemy) || m_seeEnemyTime + 2.0f > engine->GetTime())
					CombatFight();

				break;
			}
		}
		else if (m_lastEnemyOrigin == nullvec) // If we don't have an enemy we're also free to leave
		{
			TaskComplete();
			m_prevGoalIndex = -1;

			if (GetCurrentTaskID() == TASK_HIDE)
				TaskComplete();

			break;
		}

		m_navTimeset = engine->GetTime();
		if (GetCurrentTaskTime() < engine->GetTime())
		{
			if (m_isReloading && (!FNullEnt(m_enemy) || !FNullEnt(m_lastEnemy)) && m_skill > 70)
				GetCurrentTask()->time += 2.0f;
			else if (IsDeathmatchMode() && pev->health <= 20.0f && m_skill > 70)
				GetCurrentTask()->time += 5.0f;
			else
				TaskComplete();
		}

		break;
	}
	case TASK_MOVETOPOSITION:
	{
		m_aimFlags |= AIM_NAVPOINT;

		if (IsShieldDrawn())
			pev->button |= IN_ATTACK2;

		// reached destination?
		if (DoWaypointNav())
		{
			TaskComplete(); // we're done
			m_prevGoalIndex = -1;
		}

		// didn't choose goal waypoint yet?
		else if (!GoalIsValid())
		{
			DeleteSearchNodes();

			int goal = GetCurrentGoalID();
			if (!IsValidWaypoint(goal) && m_position != nullvec)
				goal = g_waypoint->FindNearest(m_position);

			if (IsValidWaypoint(goal))
			{
				m_prevGoalIndex = goal;
				GetCurrentTask()->data = goal;
				FindPath(m_currentWaypointIndex, goal);
			}
			else
				TaskComplete();
		}

		break;
	}
	case TASK_PLANTBOMB:
	{
		m_aimFlags |= AIM_CAMP;

		if (m_isBomber) // we're still got the C4?
		{
			SelectWeaponByName("weapon_c4");
			RadioMessage(Radio_CoverMe);

			if (IsAlive(m_enemy) || !m_inBombZone)
				TaskComplete();
			else
			{
				m_moveToGoal = false;
				m_checkTerrain = false;
				m_navTimeset = engine->GetTime();

				if (m_waypoint.flags & WAYPOINT_CROUCH)
					pev->button |= (IN_ATTACK | IN_DUCK);
				else
					pev->button |= IN_ATTACK;

				m_moveSpeed = 0;
				m_strafeSpeed = 0;
			}
		}
		else // done with planting
		{
			TaskComplete();

			// tell teammates to move over here...
			if (m_numFriendsLeft > 0 && GetNearbyFriendsNearPosition(pev->origin, 768.0f) <= 1)
				RadioMessage(Radio_RegroupTeam);

			DeleteSearchNodes();

			const float halfTimer = engine->GetTime() + ((engine->GetC4TimerTime() * 0.5f) + (engine->GetC4TimerTime() * 0.25f));
			if (halfTimer > 0.0f)
			{
				const int index = FindDefendWaypoint(pev->origin);
				if (IsValidWaypoint(index))
				{
					m_campposition = g_waypoint->GetPath(index)->origin; // required for this task
					PushTask(TASK_GOINGFORCAMP, TASKPRI_GOINGFORCAMP, index, halfTimer, true);
				}
			}
		}
		break;
	}
	case TASK_DEFUSEBOMB:
	{
		bool exceptionCaught = false;
		const float timeToBlowUp = GetBombTimeleft();
		const float defuseRemainingTime = m_hasDefuser ? 7.0f : 12.0f - engine->GetTime();

		if (g_waypoint->GetBombPosition() == nullvec)
		{
			exceptionCaught = true;
			g_bombPlanted = false;
			RadioMessage(Radio_SectorClear);
		}
		else if (m_numEnemiesLeft > 0 && !FNullEnt(m_enemy))
		{
			const int friends = m_numFriendsLeft > 0 ? GetNearbyFriendsNearPosition(pev->origin, 768.0f) : 0;
			if (friends < 2 && defuseRemainingTime < timeToBlowUp)
			{
				exceptionCaught = true;

				if ((defuseRemainingTime + 2.0f) > timeToBlowUp)
					exceptionCaught = false;

				if (m_numFriendsLeft > friends)
					RadioMessage(Radio_NeedBackup);
			}
		}
		else if (defuseRemainingTime > timeToBlowUp)
			exceptionCaught = true;

		if (exceptionCaught)
		{
			m_checkTerrain = true;
			m_moveToGoal = true;

			if (GetCurrentTaskID() == TASK_DEFUSEBOMB)
			{
				m_aimFlags &= ~AIM_ENTITY;
				TaskComplete();
			}

			break;
		}

		m_aimFlags |= AIM_ENTITY;
		m_destOrigin = g_waypoint->GetBombPosition();
		m_entity = g_waypoint->GetBombPosition();

		if (m_entity != nullvec)
		{
			if (m_numEnemiesLeft <= 0)
				SelectKnife();

			if (((m_entity - pev->origin).GetLengthSquared2D()) <= squaredf(60.0f))
			{
				m_moveToGoal = false;
				m_checkTerrain = false;
				m_strafeSpeed = 0.0f;
				m_moveSpeed = 0.0f;

				if (m_isReloading && m_numEnemiesLeft > 0)
				{
					const int friendsN = m_numFriendsLeft > 0 ? GetNearbyFriendsNearPosition(pev->origin, 768.0f) : 0;
					if (friendsN > 2 && GetNearbyEnemiesNearPosition(pev->origin, 768.0f) < friendsN)
						SelectKnife();
				}

				m_aimStopTime = 0.0f;
				if (!g_isXash && m_pickupItem != nullptr)
					MDLL_Use(m_pickupItem, GetEntity());
				else
					pev->button |= IN_USE;

				RadioMessage(Radio_CoverMe);
			}
		}

		break;
	}
	case TASK_FOLLOWUSER:
	{
		if (FNullEnt(m_targetEntity) || !IsAlive(m_targetEntity))
		{
			m_targetEntity = nullptr;
			TaskComplete();

			break;
		}

		if (m_targetEntity->v.button & IN_ATTACK)
		{
			TraceResult tr{};
			MakeVectors(m_targetEntity->v.v_angle);
			TraceLine(GetEntityOrigin(m_targetEntity) + m_targetEntity->v.view_ofs, g_pGlobals->v_forward * 500, true, true, GetEntity(), &tr);

			m_followWaitTime = 0.0f;

			if (!FNullEnt(tr.pHit) && IsValidPlayer(tr.pHit) && GetTeam(tr.pHit) != m_team)
			{
				m_targetEntity = nullptr;
				SetLastEnemy(tr.pHit);

				TaskComplete();
				break;
			}
		}

		if (m_targetEntity->v.maxspeed != 0.0f)
		{
			if (m_targetEntity->v.maxspeed < pev->maxspeed)
			{
				m_moveSpeed = GetWalkSpeed();
				if (m_currentWeapon == WEAPON_KNIFE)
					SelectBestWeapon();
			}
			else
				m_moveSpeed = pev->maxspeed;
		}

		if (m_reloadState == RSTATE_NONE && GetAmmo() != 0)
			m_reloadState = RSTATE_PRIMARY;

		if (IsZombieMode() || ((GetEntityOrigin(m_targetEntity) - pev->origin).GetLengthSquared() > squaredf(80.0f)))
			m_followWaitTime = 0.0f;
		else
		{
			if (m_followWaitTime == 0.0f)
				m_followWaitTime = engine->GetTime();
			else
			{
				if (m_followWaitTime + 5.0f < engine->GetTime())
				{
					m_targetEntity = nullptr;

					RadioMessage(Radio_YouTakePoint);
					TaskComplete();

					break;
				}
			}
		}

		m_aimFlags |= AIM_NAVPOINT;

		if (m_targetEntity->v.maxspeed < m_moveSpeed)
		{
			m_moveSpeed = GetWalkSpeed();
			if (m_currentWeapon == WEAPON_KNIFE)
				SelectBestWeapon();
		}

		if (IsShieldDrawn())
			pev->button |= IN_ATTACK2;

		if (DoWaypointNav()) // reached destination?
			GetCurrentTask()->data = -1;

		if (!GoalIsValid()) // didn't choose goal waypoint yet?
		{
			DeleteSearchNodes();

			int destIndex = GetEntityWaypoint(m_targetEntity);

			MiniArray <int> points;
			g_waypoint->FindInRadius(points, 200, GetEntityOrigin(m_targetEntity));

			int newIndex;
			while (!points.IsEmpty())
			{
				newIndex = points.Pop();

				// if waypoint not yet used, assign it as dest
				if (IsValidWaypoint(newIndex) && !IsWaypointOccupied(newIndex) && (newIndex != m_currentWaypointIndex))
					destIndex = newIndex;
			}

			if (IsValidWaypoint(destIndex) && destIndex != m_currentWaypointIndex)
			{
				m_prevGoalIndex = destIndex;
				GetCurrentTask()->data = destIndex;
				FindPath(m_currentWaypointIndex, destIndex);
			}
			else
			{
				m_targetEntity = nullptr;
				TaskComplete();
			}
		}
		break;
	}
	case TASK_MOVETOTARGET:
	{
		m_moveTargetOrigin = GetEntityOrigin(m_moveTargetEntity);
		if (FNullEnt(m_moveTargetEntity) || m_moveTargetOrigin == nullvec || m_team == GetTeam(m_moveTargetEntity))
		{
			SetMoveTarget(nullptr);
			break;
		}

		if (m_currentWeapon == WEAPON_KNIFE)
			SelectBestWeapon();

		m_aimFlags |= AIM_NAVPOINT;
		m_destOrigin = m_moveTargetOrigin;
		m_moveSpeed = pev->maxspeed;

		if (DoWaypointNav())
			DeleteSearchNodes();

		int destIndex = GetEntityWaypoint(m_moveTargetEntity);
		if (IsValidWaypoint(destIndex))
		{
			bool needMoveToTarget = false;
			if (!GoalIsValid())
				needMoveToTarget = true;
			else if (GetCurrentGoalID() != destIndex)
			{
				needMoveToTarget = true;
				if (!m_navNode.IsEmpty())
				{
					if (m_currentWaypointIndex == destIndex)
						needMoveToTarget = false;
					else if (m_navNode.Last() == destIndex)
						needMoveToTarget = false;
				}
			}

			if (needMoveToTarget && (!(pev->flags & FL_DUCKING) || m_damageTime + 0.5f < engine->GetTime() || m_navNode.IsEmpty()))
			{
				m_prevGoalIndex = destIndex;
				GetCurrentTask()->data = destIndex;
				FindPath(m_currentWaypointIndex, destIndex);
			}
		}

		break;
	}
	case TASK_THROWHEGRENADE:
	{
		m_aimFlags |= AIM_GRENADE;
		Vector destination = m_throw;
		RemoveCertainTask(TASK_FIGHTENEMY);

		extern ConVar ebot_zp_escape_distance;
		if (IsZombieMode() && !FNullEnt(m_enemy))
		{
			if (m_isZombieBot)
			{
				if (m_enemyOrigin != nullvec)
				{
					destination = m_enemyOrigin;
					m_destOrigin = destination;
					m_moveSpeed = pev->maxspeed;
				}
				else
				{
					m_moveSpeed = 0.0f;
					m_strafeSpeed = 0.0f;
				}
			}
			else if (((pev->origin + pev->velocity * m_frameInterval) - m_enemyOrigin).GetLengthSquared() < squaredf(cabsf(m_enemy->v.speed) + ebot_zp_escape_distance.GetFloat()))
			{
				destination = m_enemyOrigin;
				m_destOrigin = destination;
				m_moveSpeed = -pev->maxspeed;
				m_moveToGoal = false;
			}
			else
			{
				destination = m_enemyOrigin;
				m_moveSpeed = 0.0f;
				m_moveToGoal = false;
			}
		}
		else if (!(m_states & STATE_SEEINGENEMY))
		{
			m_moveSpeed = 0.0f;
			m_strafeSpeed = 0.0f;

			m_moveToGoal = false;
		}
		else if (m_enemyOrigin != nullvec && !FNullEnt(m_enemy))
		{
			extern ConVar ebot_zombie_speed_factor;
			destination = m_enemyOrigin + (m_enemy->v.velocity.SkipZ() * ebot_zombie_speed_factor.GetFloat());
		}

		m_isUsingGrenade = true;
		m_checkTerrain = false;

		if (!IsZombieMode() && ((pev->origin + pev->velocity * m_frameInterval) - destination).GetLengthSquared() < squaredf(400.0f))
		{
			// heck, I don't wanna blow up myself
			m_grenadeCheckTime = engine->GetTime() + Const_GrenadeTimer;
			SelectBestWeapon();
			TaskComplete();
			break;
		}

		m_grenade = CheckThrow(EyePosition(), destination);
		if (m_grenade.GetLengthSquared() < squaredf(100.0f))
			m_grenade = CheckToss(EyePosition(), destination);

		if (!IsZombieMode() && m_grenade != nullvec && m_grenade.GetLengthSquared() < squaredf(100.0f))
		{
			m_grenadeCheckTime = engine->GetTime() + Const_GrenadeTimer;
			m_grenade = m_lookAt;
			SelectBestWeapon();
			TaskComplete();
		}
		else
		{
			edict_t* me = GetEntity();
			edict_t* ent = nullptr;
			while (!FNullEnt(ent = FIND_ENTITY_BY_CLASSNAME(ent, "grenade")))
			{
				if (ent->v.owner == me && cstrcmp(STRING(ent->v.model) + 9, "hegrenade.mdl") == 0)
				{
					if (m_grenade != nullvec && m_grenade.GetLengthSquared() > squaredf(100.0f))
					{
						cvar_t* maxVel = g_engfuncs.pfnCVarGetPointer("sv_maxvelocity");
						if (maxVel != nullptr)
						{
							const float fVel = maxVel->value;
							Vector fixedVel;
							fixedVel.x = cclampf(m_grenade.x, -fVel, fVel);
							fixedVel.y = cclampf(m_grenade.y, -fVel, fVel);
							fixedVel.z = cclampf(m_grenade.z, -fVel, fVel);
							ent->v.velocity = fixedVel;
						}
						else
							ent->v.velocity = m_grenade;
					}

					m_grenadeCheckTime = engine->GetTime() + Const_GrenadeTimer;
					SelectBestWeapon();
					TaskComplete();
					break;
				}
			}

			if (FNullEnt(ent))
			{
				if (m_currentWeapon != WEAPON_HEGRENADE)
				{
					if (pev->weapons & (1 << WEAPON_HEGRENADE))
						SelectWeaponByName("weapon_hegrenade");
					else // no grenade???
						TaskComplete();
				}
				else if (!(pev->oldbuttons & IN_ATTACK))
					pev->button |= IN_ATTACK;
			}
		}
		break;
	}
	case TASK_THROWFBGRENADE:
	{
		m_aimFlags |= AIM_GRENADE;
		Vector destination = m_throw;
		RemoveCertainTask(TASK_FIGHTENEMY);

		extern ConVar ebot_zp_escape_distance;
		if (IsZombieMode() && !FNullEnt(m_enemy))
		{
			if (m_isZombieBot)
			{
				if (m_enemyOrigin != nullvec)
				{
					destination = m_enemyOrigin;
					m_destOrigin = destination;
					m_moveSpeed = pev->maxspeed;
				}
				else
				{
					m_moveSpeed = 0.0f;
					m_strafeSpeed = 0.0f;
				}
			}
			else if (((pev->origin + pev->velocity * m_frameInterval) - m_enemyOrigin).GetLengthSquared() < squaredf(cabsf(m_enemy->v.speed) + ebot_zp_escape_distance.GetFloat()))
			{
				destination = m_enemyOrigin;
				m_destOrigin = destination;
				m_moveSpeed = -pev->maxspeed;
				m_moveToGoal = false;
			}
			else
			{
				destination = m_enemyOrigin;
				m_moveSpeed = pev->maxspeed;
				m_moveToGoal = true;
			}
		}
		else if (!(m_states & STATE_SEEINGENEMY))
		{
			m_moveSpeed = 0.0f;
			m_strafeSpeed = 0.0f;

			m_moveToGoal = false;
		}
		else if (m_enemyOrigin != nullvec && !FNullEnt(m_enemy))
		{
			extern ConVar ebot_zombie_speed_factor;
			destination = m_enemyOrigin + (m_enemy->v.velocity.SkipZ() * ebot_zombie_speed_factor.GetFloat());
		}

		m_isUsingGrenade = true;
		m_checkTerrain = false;

		m_grenade = CheckThrow(EyePosition(), destination);

		if (m_grenade.GetLengthSquared() < squaredf(100.0f))
			m_grenade = CheckToss(pev->origin, destination);

		if (!IsZombieMode() && m_grenade != nullvec && m_grenade.GetLengthSquared() < squaredf(100.0f))
		{
			m_grenadeCheckTime = engine->GetTime() + Const_GrenadeTimer;
			m_grenade = m_lookAt;
			SelectBestWeapon();
			TaskComplete();
		}
		else
		{
			edict_t* me = GetEntity();
			edict_t* ent = nullptr;
			while (!FNullEnt(ent = FIND_ENTITY_BY_CLASSNAME(ent, "grenade")))
			{
				if (ent->v.owner == me && cstrcmp(STRING(ent->v.model) + 9, "flashbang.mdl") == 0)
				{
					cvar_t* maxVel = g_engfuncs.pfnCVarGetPointer("sv_maxvelocity");
					if (maxVel != nullptr)
					{
						const float fVel = maxVel->value;
						Vector fixedVel;
						fixedVel.x = cclampf(m_grenade.x, -fVel, fVel);
						fixedVel.y = cclampf(m_grenade.y, -fVel, fVel);
						fixedVel.z = cclampf(m_grenade.z, -fVel, fVel);
						ent->v.velocity = fixedVel;
					}
					else
						ent->v.velocity = m_grenade;

					m_grenadeCheckTime = engine->GetTime() + Const_GrenadeTimer;
					SelectBestWeapon();
					TaskComplete();
					break;
				}
			}

			if (FNullEnt(ent))
			{
				if (m_currentWeapon != WEAPON_FBGRENADE)
				{
					if (pev->weapons & (1 << WEAPON_FBGRENADE))
						SelectWeaponByName("weapon_flashbang");
					else // no grenade???
						TaskComplete();
				}
				else if (!(pev->oldbuttons & IN_ATTACK))
					pev->button |= IN_ATTACK;
			}
		}
		break;
	}
	case TASK_THROWSMGRENADE:
	{
		m_aimFlags |= AIM_GRENADE;
		Vector destination = m_throw;
		RemoveCertainTask(TASK_FIGHTENEMY);

		extern ConVar ebot_zp_escape_distance;
		if (IsZombieMode() && !FNullEnt(m_enemy))
		{
			if (m_isZombieBot)
			{
				if (m_enemyOrigin != nullvec)
				{
					destination = m_enemyOrigin;
					m_destOrigin = destination;
					m_moveSpeed = pev->maxspeed;
				}
				else
				{
					m_moveSpeed = 0.0f;
					m_strafeSpeed = 0.0f;
				}

				m_moveToGoal = false;
			}
			else if (((pev->origin + pev->velocity * m_frameInterval) - m_enemyOrigin).GetLengthSquared() < squaredf(cabsf(m_enemy->v.speed) + ebot_zp_escape_distance.GetFloat()))
			{
				destination = m_enemyOrigin;
				m_destOrigin = destination;
				m_moveSpeed = -pev->maxspeed;
				m_moveToGoal = false;
			}
			else
			{
				destination = m_enemyOrigin;
				m_moveSpeed = 0.0f;
				m_moveToGoal = false;
			}
		}
		else if (!(m_states & STATE_SEEINGENEMY))
		{
			m_moveSpeed = 0.0f;
			m_strafeSpeed = 0.0f;
			m_moveToGoal = false;
		}
		else if (m_enemyOrigin != nullvec && !FNullEnt(m_enemy))
		{
			extern ConVar ebot_zombie_speed_factor;
			destination = m_enemyOrigin + (m_enemy->v.velocity.SkipZ() * ebot_zombie_speed_factor.GetFloat());
		}

		m_isUsingGrenade = true;
		m_checkTerrain = false;
		m_grenade = CheckThrow(EyePosition(), destination);

		edict_t* me = GetEntity();
		edict_t* ent = nullptr;
		while (!FNullEnt(ent = FIND_ENTITY_BY_CLASSNAME(ent, "grenade")))
		{
			if (ent->v.owner == me && cstrcmp(STRING(ent->v.model) + 9, "smokegrenade.mdl") == 0)
			{
				if (m_grenade != nullvec && m_grenade.GetLengthSquared() > squaredf(100.0f))
				{
					cvar_t* maxVel = g_engfuncs.pfnCVarGetPointer("sv_maxvelocity");
					if (maxVel != nullptr)
					{
						const float fVel = maxVel->value;
						Vector fixedVel;
						fixedVel.x = cclampf(m_grenade.x, -fVel, fVel);
						fixedVel.y = cclampf(m_grenade.y, -fVel, fVel);
						fixedVel.z = cclampf(m_grenade.z, -fVel, fVel);
						ent->v.velocity = fixedVel;
					}
					else
						ent->v.velocity = m_grenade;
				}

				m_grenadeCheckTime = engine->GetTime() + Const_GrenadeTimer;
				SelectBestWeapon();
				TaskComplete();
				break;
			}
		}

		if (FNullEnt(ent))
		{
			if (m_currentWeapon != WEAPON_SMGRENADE)
			{
				if (pev->weapons & (1 << WEAPON_SMGRENADE))
					SelectWeaponByName("weapon_smokegrenade");
				else // no grenade???
					TaskComplete();
			}
			else if (!(pev->oldbuttons & IN_ATTACK))
				pev->button |= IN_ATTACK;
		}
		break;
	}
	case TASK_THROWFLARE:
	{
		m_aimFlags |= AIM_GRENADE;
		Vector destination = m_throw;
		RemoveCertainTask(TASK_FIGHTENEMY);

		if (!(m_states & STATE_SEEINGENEMY))
		{
			if (!FNullEnt(m_lastEnemy))
				destination = m_lastEnemyOrigin;

			m_moveSpeed = 0.0f;
			m_strafeSpeed = 0.0f;
			m_moveToGoal = false;
		}
		else if (m_enemyOrigin != nullvec && !FNullEnt(m_enemy))
		{
			extern ConVar ebot_zombie_speed_factor;
			destination = m_enemyOrigin + (m_enemy->v.velocity.SkipZ() * ebot_zombie_speed_factor.GetFloat());
		}

		m_isUsingGrenade = true;
		m_checkTerrain = false;

		m_grenade = CheckThrow(EyePosition(), destination);

		if (m_grenade.GetLengthSquared() < squaredf(100.0f))
			m_grenade = CheckToss(EyePosition(), destination);

		if (!IsZombieMode() && m_grenade != nullvec && m_grenade.GetLengthSquared() < squaredf(100.0f))
		{
			m_grenadeCheckTime = engine->GetTime() + Const_GrenadeTimer;
			m_grenade = m_lookAt;
			SelectBestWeapon();
			TaskComplete();
		}
		else
		{
			edict_t* me = GetEntity();
			edict_t* ent = nullptr;
			while (!FNullEnt(ent = FIND_ENTITY_BY_CLASSNAME(ent, "grenade")))
			{
				if (ent->v.owner == me && cstrcmp(STRING(ent->v.model) + 9, "smokegrenade.mdl") == 0)
				{
					if (m_grenade != nullvec && m_grenade.GetLengthSquared() > squaredf(100.0f))
					{
						cvar_t* maxVel = g_engfuncs.pfnCVarGetPointer("sv_maxvelocity");
						if (maxVel != nullptr)
						{
							const float fVel = maxVel->value;
							Vector fixedVel;
							fixedVel.x = cclampf(m_grenade.x, -fVel, fVel);
							fixedVel.y = cclampf(m_grenade.y, -fVel, fVel);
							fixedVel.z = cclampf(m_grenade.z, -fVel, fVel);
							ent->v.velocity = fixedVel;
						}
						else
							ent->v.velocity = m_grenade;
					}

					m_grenadeCheckTime = engine->GetTime() + Const_GrenadeTimer;
					SelectBestWeapon();
					TaskComplete();
					break;
				}
			}

			if (FNullEnt(ent))
			{
				if (m_currentWeapon != WEAPON_SMGRENADE)
				{
					if (pev->weapons & (1 << WEAPON_SMGRENADE))
						SelectWeaponByName("weapon_smokegrenade");
					else // no grenade???
						TaskComplete();
				}
				else if (!(pev->oldbuttons & IN_ATTACK))
					pev->button |= IN_ATTACK;
			}
		}
		break;
	}
	case TASK_DOUBLEJUMP:
	{
		if (FNullEnt(m_doubleJumpEntity) || !IsAlive(m_doubleJumpEntity) || !IsVisible(GetEntityOrigin(m_doubleJumpEntity), GetEntity()) || (m_aimFlags & AIM_ENEMY) || (IsValidWaypoint(m_travelStartIndex) && GetCurrentTaskTime() + (g_waypoint->GetTravelTime(m_moveSpeed, g_waypoint->GetPath(m_travelStartIndex)->origin, m_doubleJumpOrigin) + 11.0f) < engine->GetTime()))
		{
			ResetDoubleJumpState();
			return;
		}

		m_aimFlags |= AIM_NAVPOINT;

		if (m_jumpReady)
		{
			m_moveToGoal = false;
			m_checkTerrain = false;

			m_navTimeset = engine->GetTime();
			m_moveSpeed = 0.0f;
			m_strafeSpeed = 0.0f;

			bool inJump = (m_doubleJumpEntity->v.button & IN_JUMP) || (m_doubleJumpEntity->v.oldbuttons & IN_JUMP);

			if (m_duckForJump < engine->GetTime())
				pev->button |= IN_DUCK;
			else if (inJump && !(pev->oldbuttons & IN_JUMP))
				pev->button |= IN_JUMP;

			const auto& srcb = pev->origin + Vector(0.0f, 0.0f, 45.0f);
			const auto& dest = srcb + Vector(0.0f, pev->angles.y, 0.0f) + (g_pGlobals->v_up) * 256.0f;

			TraceResult trc{};
			TraceLine(srcb, dest, false, true, GetEntity(), &trc);

			if (trc.flFraction < 1.0f && trc.pHit == m_doubleJumpEntity && inJump)
			{
				m_duckForJump = engine->GetTime() + crandomfloat(3.0f, 5.0f);
				GetCurrentTask()->time = engine->GetTime();
			}

			return;
		}

		if (pev->button & IN_DUCK && IsOnFloor())
		{
			m_moveSpeed = pev->maxspeed;
			m_destOrigin = GetEntityOrigin(m_doubleJumpEntity);
			m_moveToGoal = false;
		}

		if (m_currentWaypointIndex == m_prevGoalIndex)
			m_destOrigin = m_doubleJumpOrigin;

		if (DoWaypointNav())
			GetCurrentTask()->data = -1;

		// didn't choose goal waypoint yet?
		if (!GoalIsValid())
		{
			DeleteSearchNodes();

			int boostIndex = g_waypoint->FindNearest(m_doubleJumpOrigin);
			if (IsValidWaypoint(boostIndex))
			{
				m_prevGoalIndex = boostIndex;
				m_travelStartIndex = m_currentWaypointIndex;
				GetCurrentTask()->data = boostIndex;

				// always take the shortest path
				FindPath(m_currentWaypointIndex, boostIndex);

				if (m_currentWaypointIndex == boostIndex)
					m_jumpReady = true;
			}
			else
				ResetDoubleJumpState();
		}
		break;
	}
	case TASK_ESCAPEFROMBOMB:
	{
		m_aimFlags |= AIM_NAVPOINT;

		if (!g_bombPlanted || DoWaypointNav())
		{
			TaskComplete();

			if (m_numEnemiesLeft != 0)
			{
				if (IsShieldDrawn())
					pev->button |= IN_ATTACK2;
			}

			PushTask(TASK_PAUSE, TASKPRI_CAMP, -1, engine->GetTime() + 10.0f, true);
		}
		else if (!GoalIsValid())
		{
			if (m_numEnemiesLeft <= 0)
				SelectKnife();

			int i;
			int destIndex = -1;

			DeleteSearchNodes();

			float safeRadius = 2048.0f, minPathDistance = 4096.0f;
			float pathDistance;
			for (i = 0; i < g_numWaypoints; i++)
			{
				if ((g_waypoint->GetPath(i)->origin - g_waypoint->GetBombPosition()).GetLengthSquared() < squaredf(safeRadius))
					continue;

				pathDistance = g_waypoint->GetPathDistance(m_currentWaypointIndex, i);
				if (minPathDistance > pathDistance)
				{
					minPathDistance = pathDistance;
					destIndex = i;
				}
			}

			if (IsValidWaypoint(destIndex))
				destIndex = g_waypoint->FindFarest(pev->origin, safeRadius);

			m_prevGoalIndex = destIndex;
			GetCurrentTask()->data = destIndex;
			FindPath(m_currentWaypointIndex, destIndex);
		}
		break;
	}
	case TASK_DESTROYBREAKABLE:
	{
		// breakable destroyed?
		if (FNullEnt(m_breakableEntity) || m_breakableEntity->v.health <= 0.0f)
		{
			TaskComplete();
			return;
		}

		if (ebot_kill_breakables.GetBool())
			m_breakableEntity->v.health = -1.0f;

		m_aimFlags |= AIM_OVERRIDE;

		if (m_isZombieBot)
			KnifeAttack();
		else
			SelectBestWeapon();

		if (m_currentWeapon == WEAPON_KNIFE && pev->origin.z > m_breakable.z)
			pev->button |= IN_DUCK;
		else if (!IsVisible(m_breakable, GetEntity()))
			pev->button |= IN_DUCK;

		m_checkTerrain = false;
		m_moveToGoal = false;
		m_navTimeset = engine->GetTime();
		m_camp = m_breakable;

		// is bot facing the breakable?
		if (GetShootingConeDeviation(GetEntity(), m_breakable) > 0.9f)
		{
			if (m_isZombieBot)
			{
				m_moveSpeed = pev->maxspeed;
				m_destOrigin = m_breakable;
			}
			else if (m_currentWeapon == WEAPON_KNIFE)
			{
				SelectBestWeapon();
				if (m_currentWeapon == WEAPON_KNIFE)
				{
					m_moveSpeed = pev->maxspeed;
					m_destOrigin = m_breakable;
				}
				else
					m_moveSpeed = 0.0f;
			}
			else
				m_moveSpeed = 0.0f;

			m_strafeSpeed = 0.0f;
			m_wantsToFire = true;
			m_shootTime = engine->GetTime();
		}
		else
		{
			m_checkTerrain = true;
			m_moveToGoal = true;
			TaskComplete();
		}

		if (!(pev->button & IN_ATTACK) && !(pev->oldbuttons & IN_ATTACK))
			pev->button |= IN_ATTACK;

		break;
	}
	case TASK_PICKUPITEM:
	{
		if (m_isZombieBot)
		{
			m_pickupItem = nullptr;
			TaskComplete();
			break;
		}

		if (FNullEnt(m_pickupItem))
		{
			m_pickupItem = nullptr;
			TaskComplete();
			break;
		}

		const Vector destination = GetEntityOrigin(m_pickupItem);
		m_destOrigin = destination;
		m_entity = destination;
		m_moveSpeed = pev->maxspeed;

		switch (m_pickupType)
		{
		case PICKTYPE_GETENTITY:
		{
			m_aimFlags |= AIM_NAVPOINT;
			if (FNullEnt(m_pickupItem) || (GetTeam(m_pickupItem) != -1 && m_team != GetTeam(m_pickupItem)))
			{
				m_pickupItem = nullptr;
				m_pickupType = PICKTYPE_NONE;
			}
			break;
		}
		case PICKTYPE_WEAPON:
		{
			m_aimFlags |= AIM_NAVPOINT;

			// near to weapon
			const float itemDistance = (destination - pev->origin).GetLengthSquared();
			if (itemDistance < squaredf(90.0f))
			{
				int i;
				for (i = 0; i < 7; i++)
				{
					if (cstrcmp(g_weaponSelect[i].modelName, STRING(m_pickupItem->v.model) + 9) == 0)
						break;
				}

				if (i < 7)
				{
					// secondary weapon. i.e., pistol
					int weaponID = 0;
					for (i = 0; i < 7; i++)
					{
						if (pev->weapons & (1 << g_weaponSelect[i].id))
							weaponID = i;
					}

					if (weaponID > 0)
					{
						SelectWeaponbyNumber(weaponID);
						FakeClientCommand(GetEntity(), "drop");

						if (HasShield()) // If we have the shield...
							FakeClientCommand(GetEntity(), "drop"); // discard both shield and pistol
					}

					EquipInBuyzone(0);
				}
				else
				{
					SelectWeaponbyNumber(GetHighestWeapon());
					FakeClientCommand(GetEntity(), "drop");
					EquipInBuyzone(0);
				}

				CheckSilencer(); // check the silencer
				if (IsValidWaypoint(m_currentWaypointIndex))
				{
					if (itemDistance > squaredf(m_waypoint.radius))
					{
						SetEntityWaypoint(GetEntity());
						m_currentWaypointIndex = -1;
						GetValidWaypoint();
					}
				}
			}
			break;
		}
		case PICKTYPE_SHIELDGUN:
		{
			m_aimFlags |= AIM_NAVPOINT;

			if (HasShield())
			{
				m_pickupItem = nullptr;
				break;
			}

			// near to shield?
			const float itemDistance = (destination - pev->origin).GetLengthSquared();
			if (itemDistance < squaredf(90.0f))
			{
				// get current best weapon to check if it's a primary in need to be dropped
				SelectWeaponbyNumber(GetHighestWeapon());
				FakeClientCommand(GetEntity(), "drop");

				if (IsValidWaypoint(m_currentWaypointIndex))
				{
					if (itemDistance > squaredf(m_waypoint.radius))
					{
						SetEntityWaypoint(GetEntity());
						m_currentWaypointIndex = -1;
						GetValidWaypoint();
					}
				}
			}
			break;
		}
		case PICKTYPE_PLANTEDC4:
		{
			m_aimFlags |= AIM_ENTITY;
			if (m_team == TEAM_COUNTER && (destination - pev->origin).GetLengthSquared() < squaredf(90.0f))
			{
				// notify team of defusing
				if (m_numFriendsLeft > 0)
					RadioMessage(Radio_CoverMe);

				m_moveToGoal = false;
				m_checkTerrain = false;
				m_moveSpeed = 0.0f;
				m_strafeSpeed = 0.0f;
				PushTask(TASK_DEFUSEBOMB, TASKPRI_DEFUSEBOMB, -1, 0.0, false);
			}
			break;
		}
		case PICKTYPE_HOSTAGE:
		{
			if (m_team != TEAM_COUNTER || !IsAlive(m_pickupItem))
			{
				// don't pickup dead hostages
				m_pickupItem = nullptr;
				TaskComplete();
				break;
			}

			m_aimStopTime = 0.0f;
			m_aimFlags |= AIM_ENTITY;

			if ((destination - pev->origin).GetLengthSquared() < squaredf(90.0f))
			{
				// use game dll function to make sure the hostage is correctly 'used'
				if (g_isXash)
					pev->button |= IN_USE;
				else
					MDLL_Use(m_pickupItem, GetEntity());

				for (auto& hostage : m_hostages)
				{
					if (hostage != nullptr) // store pointer to hostage so other bots don't steal from this one or bot tries to reuse it
						continue;

					hostage = m_pickupItem; 
					m_pickupItem = nullptr;
					break;
				}

				m_itemCheckTime = engine->GetTime() + 0.2f;
				m_lastCollTime = engine->GetTime() + 0.2f; // also don't consider being stuck
			}
			break;
		}
		case PICKTYPE_DEFUSEKIT:
		{
			m_aimFlags |= AIM_NAVPOINT;
			if (m_hasDefuser || m_team != TEAM_COUNTER)
			{
				m_pickupItem = nullptr;
				m_pickupType = PICKTYPE_NONE;
			}
			break;
		}
		case PICKTYPE_BUTTON:
		{
			m_aimFlags |= AIM_ENTITY;
			if (FNullEnt(m_pickupItem) || m_buttonPushTime < engine->GetTime()) // it's safer...
			{
				TaskComplete();
				m_pickupType = PICKTYPE_NONE;
				break;
			}

			// find angles from bot origin to entity...
			if ((destination - pev->origin).GetLengthSquared() < squaredf(90.0f)) // near to the button?
			{
				m_moveSpeed = 0.0f;
				m_strafeSpeed = 0.0f;
				m_moveToGoal = false;
				m_checkTerrain = false;

				if (!g_isXash || InFieldOfView(destination - EyePosition()) < 13.0f) // facing it directly?
				{
					if (g_isXash)
						pev->button |= IN_USE;
					else
						MDLL_Use(m_pickupItem, GetEntity());

					m_pickupItem = nullptr;
					m_pickupType = PICKTYPE_NONE;
					m_buttonPushTime = engine->GetTime() + 3.0f;
					TaskComplete();
				}
			}
			break;
		}
		}
		break;
	}
	}
}

void Bot::DebugModeMsg(void)
{
	const int debugMode = ebot_debug.GetInt();
	if (FNullEnt(g_hostEntity) || debugMode <= 0 || debugMode == 2)
		return;

	static float timeDebugUpdate = 0.0f;
	const int specIndex = g_hostEntity->v.iuser2;
	if (specIndex != ENTINDEX(GetEntity()))
		return;

	static int index, goal, taskID;
	if (GetCurrentTask() != nullptr)
	{
		if (taskID != GetCurrentTask()->id || index != m_currentWaypointIndex || goal != GetCurrentTask()->data || timeDebugUpdate < engine->GetTime())
		{
			taskID = GetCurrentTask()->id;
			index = m_currentWaypointIndex;
			goal = GetCurrentTask()->data;
			char taskName[80];

			switch (taskID)
			{
			case TASK_NORMAL:
				sprintf(taskName, "Normal");
				break;

			case TASK_PAUSE:
				sprintf(taskName, "Pause");
				break;

			case TASK_MOVETOPOSITION:
				sprintf(taskName, "Move To Position");
				break;

			case TASK_FOLLOWUSER:
				sprintf(taskName, "Follow User");
				break;

			case TASK_MOVETOTARGET:
				sprintf(taskName, "Move To Target");
				break;

			case TASK_PICKUPITEM:
				sprintf(taskName, "Pickup Item");
				break;

			case TASK_CAMP:
				sprintf(taskName, "Camping");
				break;

			case TASK_PLANTBOMB:
				sprintf(taskName, "Plant Bomb");
				break;

			case TASK_DEFUSEBOMB:
				sprintf(taskName, "Defuse Bomb");
				break;

			case TASK_FIGHTENEMY:
				sprintf(taskName, "Attack Enemy");
				break;

			case TASK_HUNTENEMY:
				sprintf(taskName, "Hunt Enemy");
				break;

			case TASK_THROWHEGRENADE:
				sprintf(taskName, "Throw HE Grenade");
				break;

			case TASK_THROWFBGRENADE:
				sprintf(taskName, "Throw Flash Grenade");
				break;

			case TASK_THROWSMGRENADE:
				sprintf(taskName, "Throw Smoke Grenade");
				break;

			case TASK_DOUBLEJUMP:
				sprintf(taskName, "Perform Double Jump");
				break;

			case TASK_ESCAPEFROMBOMB:
				sprintf(taskName, "Escape From Bomb");
				break;

			case TASK_DESTROYBREAKABLE:
				sprintf(taskName, "Shooting To Breakable");
				break;

			case TASK_HIDE:
				sprintf(taskName, "Hide");
				break;

			case TASK_BLINDED:
				sprintf(taskName, "Blinded");
				break;

			case TASK_GOINGFORCAMP:
				sprintf(taskName, "Going To Camp Spot");
				break;
			}

			char weaponName[80], aimFlags[32], botType[32];
			char enemyName[80], pickName[80];

			if (IsAlive(m_enemy))
				sprintf(enemyName, "[E]: %s", GetEntityName(m_enemy));
			else if (IsAlive(m_moveTargetEntity))
				sprintf(enemyName, "[MT]: %s", GetEntityName(m_moveTargetEntity));
			else if (IsAlive(m_lastEnemy))
				sprintf(enemyName, "[LE]: %s", GetEntityName(m_lastEnemy));
			else
				sprintf(enemyName, ": %s", GetEntityName(nullptr));

			sprintf(pickName, "%s", GetEntityName(m_pickupItem));

			WeaponSelect* selectTab = &g_weaponSelect[0];
			char weaponCount = 0;

			while (m_currentWeapon != selectTab->id && weaponCount < Const_NumWeapons)
			{
				selectTab++;
				weaponCount++;
			}

			// set the aim flags
			sprintf(aimFlags, "%s%s%s%s%s%s%s",
				m_aimFlags & AIM_NAVPOINT ? "NavPoint " : "",
				m_aimFlags & AIM_CAMP ? "CampPoint " : "",
				m_aimFlags & AIM_LASTENEMY ? "LastEnemy " : "",
				m_aimFlags & AIM_ENTITY ? "Entity " : "",
				m_aimFlags & AIM_ENEMY ? "Enemy " : "",
				m_aimFlags & AIM_GRENADE ? "Grenade " : "",
				m_aimFlags & AIM_OVERRIDE ? "Override " : "");

			// set the bot type
			sprintf(botType, "%s%s%s", m_personality == PERSONALITY_RUSHER ? "Rusher" : "",
				m_personality == PERSONALITY_CAREFUL ? "Careful" : "",
				m_personality == PERSONALITY_NORMAL ? "Normal" : "");

			if (weaponCount >= Const_NumWeapons)
			{
				// prevent printing unknown message from known weapons
				switch (m_currentWeapon)
				{
				case WEAPON_HEGRENADE:
					sprintf(weaponName, "weapon_hegrenade");
					break;

				case WEAPON_FBGRENADE:
					sprintf(weaponName, "weapon_flashbang");
					break;

				case WEAPON_SMGRENADE:
					sprintf(weaponName, "weapon_smokegrenade");
					break;

				case WEAPON_C4:
					sprintf(weaponName, "weapon_c4");
					break;

				default:
					sprintf(weaponName, "Unknown! (%d)", m_currentWeapon);
				}
			}
			else
				sprintf(weaponName, selectTab->weaponName);

			char gamemodName[80];
			switch (GetGameMode())
			{
			case MODE_BASE:
				sprintf(gamemodName, "Normal");
				break;

			case MODE_DM:
				sprintf(gamemodName, "Deathmatch");
				break;

			case MODE_ZP:
				sprintf(gamemodName, "Zombie Mode");
				break;

			case MODE_NOTEAM:
				sprintf(gamemodName, "No Team");
				break;

			case MODE_ZH:
				sprintf(gamemodName, "Zombie Hell");
				break;

			case MODE_TDM:
				sprintf(gamemodName, "Team Deathmatch");
				break;

			default:
				sprintf(gamemodName, "UNKNOWN MODE");
			}

			const int client = ENTINDEX(GetEntity()) - 1;

			char outputBuffer[512];
			sprintf(outputBuffer, "\n\n\n\n\n\n\n Game Mode: %s"
				"\n [%s] \n Task: %s  AimFlags:%s \n"
				"Weapon: %s  Clip: %d   Ammo: %d \n"
				"Type: %s  Money: %d  Bot AI: %s \n"
				"Enemy%s  Pickup: %s  \n\n"

				"CurrentIndex: %d  GoalIndex: %d  TD: %d \n"
				"Nav: %d  Next Nav: %d \n"
				"GEWI: %d GEWI2: %d \n"
				"Move Speed: %2.f  Strafe Speed: %2.f \n "
				"Check Terran: %d  Stuck: %d \n",
				gamemodName,
				GetEntityName(GetEntity()), taskName, aimFlags,
				&weaponName[7], GetAmmoInClip(), GetAmmo(),
				botType, m_moneyAmount, m_isZombieBot ? "Zombie" : "Normal",
				enemyName, pickName,

				m_currentWaypointIndex, m_prevGoalIndex, GetCurrentTask()->data,
				m_navNode.IsEmpty() ? -1 : m_navNode.First(), (!m_navNode.IsEmpty() && m_navNode.HasNext()) ? m_navNode.Next() : -1,
				g_clients[client].wpIndex, g_clients[client].wpIndex2,
				m_moveSpeed, m_strafeSpeed,
				m_checkTerrain, m_isStuck);

			MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, SVC_TEMPENTITY, nullptr, g_hostEntity);
			WRITE_BYTE(TE_TEXTMESSAGE);
			WRITE_BYTE(1);
			WRITE_SHORT(FixedSigned16(-1.0f, (1 << 13)));
			WRITE_SHORT(FixedSigned16(0.0f, (1 << 13)));
			WRITE_BYTE(0);
			WRITE_BYTE(m_team == TEAM_COUNTER ? 0 : 255);
			WRITE_BYTE(100);
			WRITE_BYTE(m_team != TEAM_COUNTER ? 0 : 255);
			WRITE_BYTE(0);
			WRITE_BYTE(255);
			WRITE_BYTE(255);
			WRITE_BYTE(255);
			WRITE_BYTE(0);
			WRITE_SHORT(FixedUnsigned16(0.0f, (1 << 8)));
			WRITE_SHORT(FixedUnsigned16(0.0f, (1 << 8)));
			WRITE_SHORT(FixedUnsigned16(1.0f, (1 << 8)));
			WRITE_STRING(const_cast<const char*>(&outputBuffer[0]));
			MESSAGE_END();

			timeDebugUpdate = engine->GetTime() + 1.0f;
		}

		if (m_moveTargetOrigin != nullvec && !FNullEnt(m_moveTargetEntity))
			engine->DrawLine(g_hostEntity, EyePosition(), m_moveTargetOrigin, Color(0, 255, 0, 255), 10, 0, 5, 1, LINE_SIMPLE);

		if (m_enemyOrigin != nullvec && !FNullEnt(m_enemy))
			engine->DrawLine(g_hostEntity, EyePosition(), m_enemyOrigin, Color(255, 0, 0, 255), 10, 0, 5, 1, LINE_SIMPLE);

		if (m_destOrigin != nullvec)
			engine->DrawLine(g_hostEntity, pev->origin, m_destOrigin, Color(0, 0, 255, 255), 10, 0, 5, 1, LINE_SIMPLE);

		uint16_t i;
		for (i = 0; i < m_navNode.Length() && i + 1 < m_navNode.Length(); ++i)
			engine->DrawLine(g_hostEntity, g_waypoint->GetPath(m_navNode.Get(i))->origin, g_waypoint->GetPath(m_navNode.Get(i + 1))->origin, Color(255, 100, 55, 255), 15, 0, 8, 1, LINE_SIMPLE);

		if (IsValidWaypoint(m_prevWptIndex))
		{
			const Vector src = g_waypoint->GetPath(m_prevWptIndex)->origin;
			engine->DrawLine(g_hostEntity, src - Vector(0.0f, 0.0f, 35.0f), src + Vector(0.0f, 0.0f, 35.0f), Color(255, 0, 0, 255), 15, 0, 8, 1, LINE_SIMPLE);
		}

		if (IsValidWaypoint(m_currentWaypointIndex))
		{
			const Vector src = m_waypoint.origin;
			engine->DrawLine(g_hostEntity, src - Vector(0.0f, 0.0f, 35.0f), src + Vector(0.0f, 0.0f, 35.0f), Color(0, 255, 0, 255), 15, 0, 8, 1, LINE_SIMPLE);
		}

		if (IsValidWaypoint(m_prevGoalIndex))
		{
			const Vector src = g_waypoint->GetPath(m_prevGoalIndex)->origin;
			engine->DrawLine(g_hostEntity, src - Vector(0.0f, 0.0f, 35.0f), src + Vector(0.0f, 0.0f, 35.0f), Color(0, 255, 255, 255), 15, 0, 8, 1, LINE_SIMPLE);
		}
	}
}

// this function gets called each frame and is the core of all bot ai. from here all other subroutines are called
void Bot::BotAI(void)
{
	m_checkTerrain = true;
	m_moveToGoal = true;
	m_wantsToFire = false;

	float movedDistance = 4.0f; // length of different vector (distance bot moved)

	// bot could be blinded by flashbang or smoke, recover from it
	m_viewDistance += 3.0f;

	if (m_viewDistance > m_maxViewDistance)
		m_viewDistance = m_maxViewDistance;

	if (m_blindTime > engine->GetTime())
		m_maxViewDistance = 4096.0f;

	m_moveSpeed = pev->maxspeed;

	if (m_prevTime < engine->GetTime())
	{
		// see how far bot has moved since the previous position...
		movedDistance = (m_prevOrigin - pev->origin).GetLengthSquared();

		// save current position as previous
		m_prevOrigin = pev->origin;
		m_prevTime = engine->GetTime() + 0.2f;
	}

	// do all sensing, calculate/filter all actions here
	SetConditions();

	m_isUsingGrenade = false;

	RunTask(); // execute current task
	ChooseAimDirection();

	// the bots wants to fire at something?
	if (m_wantsToFire && !m_isUsingGrenade && m_shootTime < engine->GetTime())
		FireWeapon(); // if bot didn't fire a bullet try again next frame

	 // check for reloading
	if (m_reloadCheckTime < engine->GetTime())
		CheckReload();

	const Vector directionOld = m_destOrigin - (pev->origin + pev->velocity * m_frameInterval);
	const Vector directionNormal = directionOld.Normalize2D();
	const Vector direction = directionNormal;

	m_moveAngles = directionOld.ToAngles();
	m_moveAngles.ClampAngles();
	m_moveAngles.x *= -1.0f; // invert for engine

	if (GetCurrentTaskID() == TASK_CAMP || GetCurrentTaskID() == TASK_DESTROYBREAKABLE)
		SelectBestWeapon();

	if (IsZombieMode())
	{
		if (!IsOnLadder())
		{
			if (m_isEnemyReachable)
			{
				m_moveToGoal = false; // don't move to goal
				m_checkTerrain = false;
				m_navTimeset = engine->GetTime();
				CombatFight();
			}
			else if (m_isZombieBot)
				ZombieModeAi();

			extern ConVar ebot_escape;
			if (ebot_escape.GetBool() && GetCurrentTaskID() != TASK_CAMP && GetCurrentTaskID() != TASK_DESTROYBREAKABLE)
			{
				if (m_seeEnemyTime + 2.0f < engine->GetTime() && FNullEnt(m_enemy) && FNullEnt(m_breakableEntity))
					SelectKnife();
				else if (m_currentWeapon == WEAPON_KNIFE)
					SelectBestWeapon();
			}
			else if (m_currentWeapon == WEAPON_KNIFE)
				SelectBestWeapon();
		}
	}
	else if (g_gameVersion != HALFLIFE)
	{
		// if there's some radio message to respond, check it
		if (m_radioOrder != 0)
			CheckRadioCommands();

		if (m_checkKnifeSwitch && m_buyingFinished && m_spawnTime + crandomfloat(4.0f, 8.0f) < engine->GetTime())
		{
			m_checkKnifeSwitch = false;

			if (!IsZombieMode())
			{
				if (!IsDeathmatchMode() && chanceof(m_personality == PERSONALITY_RUSHER ? 99 : m_personality == PERSONALITY_CAREFUL ? 33 : 66) && !m_isReloading && (g_mapType & (MAP_CS | MAP_DE | MAP_ES | MAP_AS)))
					SelectKnife();
			}
		}

		// check if we already switched weapon mode
		if (m_checkWeaponSwitch && m_buyingFinished && m_spawnTime + crandomfloat(2.0f, 3.5f) < engine->GetTime())
		{
			if (IsShieldDrawn())
				pev->button |= IN_ATTACK2;
			else
			{
				switch (m_currentWeapon)
				{
				case WEAPON_M4A1:
				case WEAPON_USP:
				{
					CheckSilencer();
					break;
				}
				case WEAPON_FAMAS:
				case WEAPON_GLOCK18:
				{
					if (chanceof(50))
						pev->button |= IN_ATTACK2;
					break;
				}
				}
			}

			m_checkWeaponSwitch = false;
		}

		if (!IsOnLadder())
		{
			if (!FNullEnt(m_enemy) && GetCurrentTaskID() != TASK_CAMP)
			{
				m_moveToGoal = false; // don't move to goal
				m_navTimeset = engine->GetTime();
				CombatFight();
			}
		}
	}
	else if (m_aimFlags & AIM_ENEMY)
	{
		m_moveToGoal = false;
		m_checkTerrain = false;
		m_navTimeset = engine->GetTime();
		CombatFight();
	}

	// allowed to move to a destination position?
	if (m_moveToGoal)
	{
		if (IsValidWaypoint(m_currentWaypointIndex) && m_waypoint.flags & WAYPOINT_WAITUNTIL)
		{
			TraceResult tr{};
			TraceLine(m_waypoint.origin, m_waypoint.origin - Vector(0.0f, 0.0f, 60.0f), false, false, GetEntity(), &tr);
			if (tr.flFraction == 1.0f)
			{
				m_moveSpeed = 0.0f;
				m_strafeSpeed = 0.0f;
				IgnoreCollisionShortly();
				return;
			}
		}

		GetValidWaypoint();

		// time to reach waypoint
		if (m_navTimeset + GetEstimatedReachTime() < engine->GetTime())
		{
			if (FNullEnt(m_enemy))
			{
				// clear these pointers, bot mingh be stuck getting to them
				if (!FNullEnt(m_pickupItem) && m_pickupType != PICKTYPE_PLANTEDC4)
					m_itemIgnore = m_pickupItem;

				m_pickupItem = nullptr;
				m_breakableEntity = nullptr;
				m_breakable = nullvec;
				m_itemCheckTime = engine->GetTime() + 5.0f;
				m_pickupType = PICKTYPE_NONE;
			}
		}

		if (IsInWater()) // special movement for swimming here
		{
			// check if we need to go forward or back press the correct buttons
			if (InFieldOfView(m_destOrigin - EyePosition()) > 90.0f)
				pev->button |= IN_BACK;
			else
				pev->button |= IN_FORWARD;

			if (m_moveAngles.x > 60.0f)
				pev->button |= IN_DUCK;
			else if (m_moveAngles.x < -60.0f)
				pev->button |= IN_JUMP;
		}
	}

	if (m_waypoint.flags & WAYPOINT_FALLRISK)
		m_checkTerrain = false;

	if (m_checkTerrain)
	{
		TraceResult tr{};

		m_isStuck = false;
		CheckCloseAvoidance(directionNormal);

		// for rebuild path
		if (!FNullEnt(m_avoid) && m_isStuck && m_waypoint.flags & WAYPOINT_ONLYONE)
		{
			int ref = FindWaypoint(false);
			if (IsValidWaypoint(ref))
				FindPath(m_currentWaypointIndex, ref);
		}

		const float minSpeed = pev->flags & FL_DUCKING ? 4.0f : 12.0f;
		if ((m_moveSpeed < -minSpeed || m_moveSpeed > minSpeed || m_strafeSpeed > minSpeed || m_strafeSpeed < -minSpeed) && m_lastCollTime < engine->GetTime())
		{
			if (movedDistance < 2.0f && (m_prevSpeed > 20.0f || cabsf(m_prevVelocity.GetLength2D()) < (m_moveSpeed * 0.5f)))
			{
				m_prevTime = engine->GetTime();
				m_isStuck = true;

				if (m_firstCollideTime == 0.0f)
					m_firstCollideTime = engine->GetTime() + 0.2f;
			}
			else
			{
				// test if there's something ahead blocking the way
				if (!IsOnLadder() && CantMoveForward(directionNormal, &tr))
				{
					if (m_firstCollideTime == 0.0f)
						m_firstCollideTime = engine->GetTime() + 0.2f;
					else if (m_firstCollideTime < engine->GetTime())
						m_isStuck = true;
				}
				else
					m_firstCollideTime = 0.0f;
			}
		}

		if (!m_isStuck) // not stuck?
		{
			// boosting improve
			if (m_isZombieBot && m_waypoint.flags & WAYPOINT_DJUMP && IsOnFloor() && ((pev->origin - m_waypoint.origin).GetLengthSquared() < squaredf(54.0f)))
				pev->button |= IN_DUCK;
			else
			{
				if (m_probeTime + 1.0f < engine->GetTime())
					ResetCollideState(); // reset collision memory if not being stuck for 1 sec
				else
				{
					// remember to keep pressing duck if it was necessary ago
					if (m_collideMoves[m_collStateIndex] == COSTATE_DUCK && IsOnFloor() || IsInWater())
						pev->button |= IN_DUCK;
				}
			}
		}
		else
		{
			GetValidWaypoint();

			// not yet decided what to do?
			if (m_collisionState == COSTATE_UNDECIDED)
			{
				uint32_t bits = 0;

				if (IsOnLadder())
					bits |= COPROBE_STRAFE;
				else if (IsInWater())
					bits |= (COPROBE_JUMP | COPROBE_STRAFE);
				else
					bits |= (COPROBE_STRAFE | (chanceof(35) ? COPROBE_JUMP : 0));

				// collision check allowed if not flying through the air
				if (IsOnFloor() || IsOnLadder() || IsInWater())
				{
					Vector src, dest;

					int state[8];
					int i = 0;

					// first 4 entries hold the possible collision states
					state[i++] = COSTATE_STRAFELEFT;
					state[i++] = COSTATE_STRAFERIGHT;
					state[i++] = COSTATE_JUMP;
					state[i++] = COSTATE_DUCK;

					if (bits & COPROBE_STRAFE)
					{
						state[i] = 0;
						state[i + 1] = 0;

						// to start strafing, we have to first figure out if the target is on the left side or right side
						MakeVectors(m_moveAngles);

						Vector dirToPoint = (pev->origin - m_destOrigin).Normalize2D();
						Vector rightSide = g_pGlobals->v_right.Normalize2D();

						bool dirRight = false;
						bool dirLeft = false;
						bool blockedLeft = false;
						bool blockedRight = false;

						if ((dirToPoint | rightSide) > 0.0f)
							dirRight = true;
						else
							dirLeft = true;

						const Vector testDir = m_moveSpeed > 0.0f ? g_pGlobals->v_forward : -g_pGlobals->v_forward;

						// now check which side is blocked
						src = pev->origin + g_pGlobals->v_right * 32.0f;
						dest = src + testDir * 32.0f;

						TraceHull(src, dest, true, head_hull, pev->pContainingEntity, &tr);

						if (tr.flFraction != 1.0f)
							blockedRight = true;

						src = pev->origin - g_pGlobals->v_right * 32.0f;
						dest = src + testDir * 32.0f;

						TraceHull(src, dest, true, head_hull, pev->pContainingEntity, &tr);

						if (tr.flFraction != 1.0f)
							blockedLeft = true;

						if (dirLeft)
							state[i] += 5;
						else
							state[i] -= 5;

						if (blockedLeft)
							state[i] -= 5;

						i++;

						if (dirRight)
							state[i] += 5;
						else
							state[i] -= 5;

						if (blockedRight)
							state[i] -= 5;
					}

					// now weight all possible states
					if (bits & COPROBE_JUMP)
					{
						state[i] = 0;

						if (CanJumpUp(directionNormal))
							state[i] += 10;

						if (m_destOrigin.z >= pev->origin.z + 18.0f)
							state[i] += 5;

						if (EntityIsVisible(m_destOrigin))
						{
							MakeVectors(m_moveAngles);

							src = EyePosition();
							src = src + g_pGlobals->v_right * 16.0f;

							TraceHull(src, m_destOrigin, true, point_hull, pev->pContainingEntity, &tr);

							if (tr.flFraction >= 1.0f)
							{
								src = EyePosition();
								src = src - g_pGlobals->v_right * 16.0f;

								TraceHull(src, m_destOrigin, true, point_hull, pev->pContainingEntity, &tr);

								if (tr.flFraction >= 1.0f)
									state[i] += 5;
							}
						}

						if (pev->flags & FL_DUCKING)
							src = pev->origin;
						else
							src = pev->origin + Vector(0.0f, 0.0f, -17.0f);

						dest = src + directionNormal * 32.0f;
						TraceHull(src, dest, true, point_hull, pev->pContainingEntity, &tr);

						if (tr.flFraction != 1.0f)
							state[i] += 10;
					}
					else
						state[i] = 0;

					i++;

					if (bits & COPROBE_DUCK)
					{
						state[i] = 0;
						if (CanDuckUnder(directionNormal))
							state[i] += 10;

						if ((m_destOrigin.z + 36.0f < pev->origin.z) && IsVisible(m_destOrigin, pev->pContainingEntity))
							state[i] += 5;
					}
					else
						state[i] = 0;

					i++;

					// weighted all possible moves, now sort them to start with most probable
					bool isSorting = false;

					do
					{
						isSorting = false;
						for (i = 0; i < 3; i++)
						{
							if (state[i + 3] < state[i + 3 + 1])
							{
								int temp = state[i];

								state[i] = state[i + 1];
								state[i + 1] = temp;

								temp = state[i + 3];

								state[i + 3] = state[i + 4];
								state[i + 4] = temp;

								isSorting = true;
							}
						}
					} while (isSorting);

					for (i = 0; i < 3; i++)
						m_collideMoves[i] = state[i];

					m_collideTime = engine->GetTime();
					m_probeTime = engine->GetTime() + 0.5f;
					m_collisionProbeBits = bits;
					m_collisionState = COSTATE_PROBING;
					m_collStateIndex = 0;
				}
			}

			if (m_collisionState == COSTATE_PROBING)
			{
				if (m_probeTime < engine->GetTime())
				{
					m_collStateIndex++;
					m_probeTime = engine->GetTime() + 0.5f;

					if (m_collStateIndex > 3)
					{
						m_navTimeset = engine->GetTime() - 5.0f;
						ResetCollideState();
					}
				}

				if (m_collStateIndex < 3)
				{
					switch (m_collideMoves[m_collStateIndex])
					{
					case COSTATE_JUMP:
						if (IsOnFloor() || IsInWater())
						{
							if (IsInWater() || !m_isZombieBot || m_damageTime < engine->GetTime() || m_currentTravelFlags & PATHFLAG_JUMP || KnifeAttack())
								pev->button |= IN_JUMP;
						}
						break;

					case COSTATE_DUCK:
						if (IsOnFloor() || IsInWater())
							pev->button |= IN_DUCK;
						break;

					case COSTATE_STRAFELEFT:
						pev->button |= IN_MOVELEFT;
						SetStrafeSpeed(directionNormal, -pev->maxspeed);
						break;

					case COSTATE_STRAFERIGHT:
						pev->button |= IN_MOVERIGHT;
						SetStrafeSpeed(directionNormal, pev->maxspeed);
						break;
					}
				}
			}
		}

	}
	else
		m_isStuck = false;

	bool OnLadderNoDuck = false;
	if (IsOnLadder() || (m_waypoint.flags & WAYPOINT_LADDER))
	{
		if (!(m_waypoint.flags & WAYPOINT_CROUCH))
			OnLadderNoDuck = true;
	}

	if (OnLadderNoDuck)
		pev->button &= ~IN_DUCK;
	else if (m_duckTime > engine->GetTime())
		pev->button |= IN_DUCK;

	if (pev->button & IN_JUMP)
	{
		if (m_currentTravelFlags & PATHFLAG_JUMP)
		{
			Vector point1Origin, point2Origin;
			if (IsValidWaypoint(m_prevWptIndex))
				point1Origin = g_waypoint->GetPath(m_prevWptIndex)->origin;
			else if (IsOnFloor())
				point1Origin = pev->origin;

			point2Origin = m_waypoint.origin;

			if (point1Origin != nullvec && point2Origin != nullvec)
			{
				if ((point1Origin - point2Origin).GetLengthSquared() >= squaredf(100.0f))
					m_jumpTime = engine->GetTime() + crandomfloat(1.0f, 1.5f);
				else if (point1Origin.z > point2Origin.z)
					m_jumpTime = engine->GetTime() + crandomfloat(0.25f, 0.5f);
				else
					m_jumpTime = engine->GetTime() + crandomfloat(0.5f, 1.0f);
			}
		}
		else
			m_jumpTime = engine->GetTime() + crandomfloat(0.3f, 0.5f);
	}

	if (m_jumpTime > engine->GetTime() && !IsOnFloor() && !IsInWater() && !IsOnLadder())
		pev->button |= IN_DUCK;

	// save the previous speed (for checking if stuck)
	m_prevSpeed = cabsf(m_moveSpeed);
	m_prevVelocity = pev->velocity;
}

void Bot::ChatMessage(const int type, const bool isTeamSay)
{
	extern ConVar ebot_chat;
	if (g_chatFactory[type].IsEmpty() || !ebot_chat.GetBool())
		return;

	char* pickedPhrase = g_chatFactory[type].GetRandomElement();
	if (IsNullString(pickedPhrase))
		return;

	PrepareChatMessage(pickedPhrase);
	PushMessageQueue(isTeamSay ? CMENU_TEAMSAY : CMENU_SAY);
}

bool Bot::HasHostage(void)
{
	if (!(g_mapType & MAP_CS))
		return false;

	if (m_team != TEAM_COUNTER)
		return false;

	for (auto& hostage : m_hostages)
	{
		if (!FNullEnt(hostage))
		{
			// don't care about dead hostages
			if (hostage->v.health <= 0.0f || (pev->origin - GetEntityOrigin(hostage)).GetLengthSquared() > squaredf(600.0f))
			{
				hostage = nullptr;
				continue;
			}

			return true;
		}
	}

	return false;
}

void Bot::ResetCollideState(void)
{
	m_probeTime = 0.0f;
	m_collideTime = 0.0f;

	m_collisionState = COSTATE_UNDECIDED;
	m_collStateIndex = 0;

	m_isStuck = false;

	for (auto& collide : m_collideMoves)
		collide = 0;
}

int Bot::GetAmmo(void)
{
	if (m_currentWeapon >= Const_MaxWeapons)
		return 0;

	if (g_weaponDefs[m_currentWeapon].ammo1 == -1)
		return 0;

	return m_ammo[g_weaponDefs[m_currentWeapon].ammo1];
}

void Bot::TakeDamage(edict_t* inflictor)
{
	if (m_blindTime > engine->GetTime())
		return;

	if (!IsValidPlayer(inflictor) || inflictor == GetEntity())
		return;

	m_lastDamageOrigin = GetPlayerHeadOrigin(inflictor);
	m_damageTime = engine->GetTime() + 1.0f;

	if (GetTeam(inflictor) == m_team)
		return;

	if (m_isZombieBot)
	{
		IgnoreCollisionShortly();
		if (FNullEnt(m_enemy) && FNullEnt(m_moveTargetEntity))
		{
			if (IsEnemyViewable(inflictor, false, true))
				SetMoveTarget(inflictor);
			else
				goto lastly;
		}

		return;
	}
	else
	{
		// remove some tasks
		RemoveCertainTask(TASK_HIDE);
		RemoveCertainTask(TASK_PAUSE);
		RemoveCertainTask(TASK_PLANTBOMB);
		RemoveCertainTask(TASK_DOUBLEJUMP);

		if (!IsZombieMode() && m_currentWeapon == WEAPON_KNIFE)
			SelectBestWeapon();
	}

	if (FNullEnt(m_enemy))
	{
	lastly:
		SetLastEnemy(inflictor);
		m_seeEnemyTime = engine->GetTime();
	}
}

// this function gets called by network message handler, when screenfade message get's send
// it's used to make bot blind froumd the grenade
void Bot::TakeBlinded(const Vector fade, const uint8_t alpha)
{
	if (fade.x != 255 || fade.y != 255 || fade.z != 255 || alpha <= 170)
		return;

	SetEnemy(nullptr);

	m_maxViewDistance = crandomfloat(10.0f, 20.0f);
	m_blindTime = (engine->GetTime() + static_cast <float> (alpha - 200)) * 0.0625f;

	m_blindCampPoint = FindDefendWaypoint(pev->origin);
	if ((g_waypoint->GetPath(m_blindCampPoint)->origin - pev->origin).GetLengthSquared() >= squaredf(512.0f))
		m_blindCampPoint = -1;

	if (IsValidWaypoint(m_blindCampPoint))
		return;

	m_blindMoveSpeed = -pev->maxspeed;
	m_blindSidemoveSpeed = 0.0f;

	if (chanceof(50))
		m_blindSidemoveSpeed = pev->maxspeed;
	else
		m_blindSidemoveSpeed = -pev->maxspeed;

	if (m_personality == PERSONALITY_RUSHER)
		m_blindMoveSpeed = -GetWalkSpeed();
	else if (m_personality == PERSONALITY_CAREFUL)
		m_blindMoveSpeed = -pev->maxspeed;
	else
	{
		m_blindMoveSpeed = 0.0f;
		m_blindButton = IN_DUCK;
	}
}

// this function, asks bot to discard his current primary weapon (or c4) to the user that requsted it with /drop*
// command, very useful, when i don't have money to buy anything... )
void Bot::DiscardWeaponForUser(edict_t* user, const bool discardC4)
{
	if (FNullEnt(user))
		return;
	
	const Vector userOrigin = GetEntityOrigin(user);
	if (IsAlive(user) && m_moneyAmount >= 2000 && HasPrimaryWeapon() && (userOrigin - pev->origin).GetLengthSquared() <= squaredf(240.0f))
	{
		m_aimFlags |= AIM_ENTITY;
		m_lookAt = userOrigin;

		if (discardC4)
		{
			SelectWeaponByName("weapon_c4");
			FakeClientCommand(GetEntity(), "drop");
		}
		else
		{
			SelectBestWeapon();
			FakeClientCommand(GetEntity(), "drop");
		}

		m_pickupItem = nullptr;
		m_pickupType = PICKTYPE_NONE;
		m_itemCheckTime = engine->GetTime() + 5.0f;

		if (m_inBuyZone)
		{
			m_buyingFinished = false;
			m_buyState = 0;
			PushMessageQueue(CMENU_BUY);
			m_nextBuyTime = engine->GetTime();
		}
	}
	else
	{
		RadioMessage(Radio_Negative);
		ChatSay(false, FormatBuffer("Sorry %s, i don't want discard my %s to you!", GetEntityName(user), discardC4 ? "bomb" : "weapon"));
	}
}

void Bot::ResetDoubleJumpState(void)
{
	TaskComplete();
	DeleteSearchNodes();

	m_doubleJumpEntity = nullptr;
	m_duckForJump = 0.0f;
	m_doubleJumpOrigin = nullvec;
	m_travelStartIndex = -1;
	m_jumpReady = false;
}

// this function returns the velocity at which an object should looped from start to land near end.
// returns null vector if toss is not feasible
Vector Bot::CheckToss(const Vector& start, Vector end)
{
	TraceResult tr{};
	float gravity = engine->GetGravity() * 0.54f;

	end = end - pev->velocity;
	end.z -= 15.0f;

	if (cabsf(end.z - start.z) > 500.0f)
		return nullvec;

	Vector midPoint = start + (end - start) * 0.5f;
	TraceHull(midPoint, midPoint + Vector(0.0f, 0.0f, 500.0f), true, head_hull, ENT(pev), &tr);

	if (tr.flFraction < 1.0f)
	{
		midPoint = tr.vecEndPos;
		midPoint.z = tr.pHit->v.absmin.z - 1.0f;
	}

	if ((midPoint.z < start.z) || (midPoint.z < end.z))
		return nullvec;

	const float half = 0.5f * gravity;
	float timeOne = csqrtf((midPoint.z - start.z) / half);
	float timeTwo = csqrtf((midPoint.z - end.z) / half);

	if (timeOne < 0.1)
		return nullvec;

	Vector nadeVelocity = (end - start) / (timeOne + timeTwo);
	nadeVelocity.z = gravity * timeOne;

	Vector apex = start + nadeVelocity * timeOne;
	apex.z = midPoint.z;

	TraceHull(start, apex, false, head_hull, ENT(pev), &tr);

	if (tr.flFraction < 1.0f || tr.fAllSolid)
		return nullvec;

	TraceHull(end, apex, true, head_hull, ENT(pev), &tr);

	if (tr.flFraction != 1.0f)
	{
		float dot = -(tr.vecPlaneNormal | (apex - end).Normalize());

		if (dot > 0.7f || tr.flFraction < 0.8f) // 60 degrees
			return nullvec;
	}

	return nadeVelocity * 0.777f;
}

// this function returns the velocity vector at which an object should be thrown from start to hit end returns null vector if throw is not feasible
Vector Bot::CheckThrow(const Vector& start, Vector end)
{
	Vector nadeVelocity = (end - start);
	TraceResult tr{};

	float gravity = engine->GetGravity() * 0.55f;
	float time = nadeVelocity.GetLengthSquared() / squaredf(196.0f);

	if (time < 0.01f)
		return nullvec;
	else if (time > 2.0f)
		time = 1.2f;

	nadeVelocity = nadeVelocity * (1.0f / time);
	nadeVelocity.z += gravity * time * 0.5f;

	Vector apex = start + (end - start) * 0.5f;
	apex.z += 0.5f * gravity * (time * 0.5f) * (time * 0.5f);

	TraceLine(start, apex, true, false, GetEntity(), &tr);

	if (tr.flFraction != 1.0f)
		return nullvec;

	TraceLine(end, apex, true, false, GetEntity(), &tr);

	if (tr.flFraction != 1.0f || tr.fAllSolid)
	{
		float dot = -(tr.vecPlaneNormal | (apex - end).Normalize());

		if (dot > 0.7f || tr.flFraction < 0.8f)
			return nullvec;
	}

	return nadeVelocity * 0.7793f;
}

// this function checks burst mode, and switch it depending distance to to enemy
void Bot::CheckBurstMode(float distance)
{
	if (HasShield())
		return; // no checking when shiled is active

	 // if current weapon is glock, disable burstmode on long distances, enable it else
	if (m_currentWeapon == WEAPON_GLOCK18 && distance < 300.0f && m_weaponBurstMode == BURST_DISABLED)
		pev->button |= IN_ATTACK2;
	else if (m_currentWeapon == WEAPON_GLOCK18 && distance >= 30.0f && m_weaponBurstMode == BURST_ENABLED)
		pev->button |= IN_ATTACK2;

	// if current weapon is famas, disable burstmode on short distances, enable it else
	if (m_currentWeapon == WEAPON_FAMAS && distance > 400.0f && m_weaponBurstMode == BURST_DISABLED)
		pev->button |= IN_ATTACK2;
	else if (m_currentWeapon == WEAPON_FAMAS && distance <= 400.0f && m_weaponBurstMode == BURST_ENABLED)
		pev->button |= IN_ATTACK2;
}

void Bot::CheckSilencer(void)
{
	if ((m_currentWeapon == WEAPON_USP && m_skill < 90) || m_currentWeapon == WEAPON_M4A1 && !HasShield())
	{
		int random = (m_personality == PERSONALITY_RUSHER ? 33 : m_personality == PERSONALITY_CAREFUL ? 99 : 66);

		// aggressive bots don't like the silencer
		if (chanceof(m_currentWeapon == WEAPON_USP ? random / 3 : random))
		{
			if (pev->weaponanim > 6) // is the silencer not attached...
				pev->button |= IN_ATTACK2; // attach the silencer
		}
		else
		{
			if (pev->weaponanim <= 6) // is the silencer attached...
				pev->button |= IN_ATTACK2; // detach the silencer
		}
	}
}

float Bot::GetBombTimeleft(void)
{
	if (!g_bombPlanted)
		return 0.0f;

	float timeLeft = ((g_timeBombPlanted + engine->GetC4TimerTime()) - engine->GetTime());

	if (timeLeft < 0.0f)
		return 0.0f;

	return timeLeft;
}

float Bot::GetEstimatedReachTime(void)
{
	float estimatedTime = 6.0f; // time to reach next waypoint

	// calculate 'real' time that we need to get from one waypoint to another
	if (IsValidWaypoint(m_currentWaypointIndex) && IsValidWaypoint(m_prevWptIndex))
	{
		const float distance = (g_waypoint->GetPath(m_prevWptIndex)->origin - m_waypoint.origin).GetLengthSquared();

		// caclulate estimated time
		estimatedTime = 5.0f * (distance / squaredf(pev->maxspeed));

		// check for special waypoints, that can slowdown our movement
		if ((m_waypoint.flags & WAYPOINT_CROUCH) || (m_waypoint.flags & WAYPOINT_LADDER) || (pev->button & IN_DUCK))
			estimatedTime *= 3.0f;

		// check for too low values
		if (estimatedTime < 3.0f)
			estimatedTime = 3.0f;

		// check for too high values
		if (estimatedTime > 8.0f)
			estimatedTime = 8.0f;
	}

	return estimatedTime;
}

bool Bot::CampingAllowed(void)
{
	if (IsZombieMode())
	{
		if (m_isZombieBot || g_waypoint->m_zmHmPoints.IsEmpty())
			return false;

		return true;
	}
	else if (g_waypoint->m_campPoints.IsEmpty())
		return false;

	if (GetGameMode() == MODE_BASE)
	{
		if (m_isBomber || m_isVIP || m_isUsingGrenade)
			return false;

		if (!(g_mapType & MAP_DE) && (m_numEnemiesLeft <= 1 || m_numFriendsLeft <= 0))
			return false;

		if (m_team == TEAM_COUNTER)
		{
			if (g_mapType & MAP_CS && HasHostage())
				return false;
			else if (g_bombPlanted && g_mapType & MAP_DE && (!IsBombDefusing(g_waypoint->GetBombPosition()) || OutOfBombTimer()))
				return false;
		}
		else
		{
			if (g_mapType & MAP_DE)
			{
				if (g_bombPlanted)
				{
					const Vector bomb = g_waypoint->GetBombPosition();

					// where is the bomb?
					if (bomb == nullvec)
						return false;

					return true;
				}
				else
				{
					if (m_isSlowThink)
						m_loosedBombWptIndex = FindLoosedBomb();

					if (IsValidWaypoint(m_loosedBombWptIndex))
						return false;
				}
			}
		}

		if (m_personality == PERSONALITY_RUSHER)
		{
			if (!FNullEnt(m_lastEnemy) && m_numFriendsLeft < m_numEnemiesLeft)
				return false;
			else if (FNullEnt(m_lastEnemy) && m_numFriendsLeft > m_numEnemiesLeft)
				return false;
		}
	}

	if (ebot_followuser.GetInt() > 0 && (m_radioOrder == Radio_FollowMe || GetCurrentTaskID() == TASK_CAMP))
	{
		int numFollowers = 0;

		// check if no more followers are allowed
		for (const auto& bot : g_botManager->m_bots)
		{
			if (bot != nullptr && bot->m_isAlive)
			{
				if (bot->m_targetEntity == m_radioEntity)
					numFollowers++;
			}
		}

		// don't camp if bots following
		if (numFollowers > 0)
			return false;
	}

	return true;
}

bool Bot::OutOfBombTimer(void)
{
	if (!(g_mapType & MAP_DE))
		return false;

	if (!g_bombPlanted)
		return false;

	if (GetCurrentTaskID() == TASK_DEFUSEBOMB || m_hasProgressBar || GetCurrentTaskID() == TASK_ESCAPEFROMBOMB)
		return false; // if CT bot already start defusing, or already escaping, return false

	// calculate left time
	float timeLeft = GetBombTimeleft();

	// if time left greater than 12, no need to do other checks
	if (timeLeft >= 12.0f)
		return false;
	else if (m_team == TEAM_TERRORIST)
		return true;

	const Vector& bombOrigin = g_waypoint->GetBombPosition();

	// bot will belive still had a chance
	if ((m_hasDefuser && IsVisible(bombOrigin, GetEntity())) || (bombOrigin - pev->origin).GetLengthSquared() < squaredf(512.0f))
		return false;

	bool hasTeammatesWithDefuserKit = false;
	// check if our teammates has defusal kit
	if (m_numFriendsLeft > 0)
	{
		for (const auto& bot : g_botManager->m_bots)
		{
			// search players with defuse kit
			if (bot != nullptr && bot->m_team == TEAM_COUNTER && bot->m_hasDefuser && (bombOrigin - bot->pev->origin).GetLengthSquared() < squaredf(512.0f))
			{
				hasTeammatesWithDefuserKit = true;
				break;
			}
		}
	}

	// add reach time to left time
	float reachTime = g_waypoint->GetTravelTime(pev->maxspeed, m_waypoint.origin, bombOrigin);

	// for counter-terrorist check alos is we have time to reach position plus average defuse time
	if ((timeLeft < reachTime && !m_hasDefuser && !hasTeammatesWithDefuserKit) || (timeLeft < reachTime && m_hasDefuser))
		return true;

	if ((m_hasDefuser ? 6.0f : 12.0f) + GetEstimatedReachTime() < GetBombTimeleft())
		return true;

	return false; // return false otherwise
}

// this function is gets called when bot enters a buyzone, to allow bot to buy some stuff
void Bot::EquipInBuyzone(const int iBuyCount)
{
	if (g_gameVersion == HALFLIFE)
		return;

	static float lastEquipTime = 0.0f;

	// if bot is in buy zone, try to buy ammo for this weapon...
	if (lastEquipTime + 15.0f < engine->GetTime() && m_inBuyZone && g_timeRoundStart + crandomfloat(10.0f, 20.0f) + engine->GetBuyTime() < engine->GetTime() && !g_bombPlanted && m_moneyAmount > 700)
	{
		m_buyingFinished = false;
		m_buyState = iBuyCount;

		// push buy message
		PushMessageQueue(CMENU_BUY);

		m_nextBuyTime = engine->GetTime();
		lastEquipTime = engine->GetTime();
	}
}

// this function finds if somebody currently defusing the bomb
bool Bot::IsBombDefusing(const Vector bombOrigin)
{
	if (!g_bombPlanted)
		return false;

	bool defusingInProgress = false;
	const float distanceToBomb = squaredf(140.0f);
	Bot* bot;

	for (const auto& client : g_clients)
	{
		if (FNullEnt(client.ent))
			continue;

		if (!(client.flags & CFLAG_USED) || !(client.flags & CFLAG_ALIVE))
			continue;

		bot = g_botManager->GetBot(client.index);
		if (bot != nullptr)
		{
			if (m_team != bot->m_team || bot->GetCurrentTaskID() == TASK_ESCAPEFROMBOMB || bot->GetCurrentTaskID() == TASK_CAMP)
				continue; // skip other mess

			// if close enough, mark as progressing
			if ((client.origin - bombOrigin).GetLengthSquared() < distanceToBomb && (bot->GetCurrentTaskID() == TASK_DEFUSEBOMB || bot->m_hasProgressBar))
			{
				defusingInProgress = true;
				break;
			}

			continue;
		}

		// take in account peoples too
		if (client.team == m_team)
		{
			// if close enough, mark as progressing
			if ((client.origin - bombOrigin).GetLengthSquared() < distanceToBomb && ((client.ent->v.button | client.ent->v.oldbuttons) & IN_USE))
			{
				defusingInProgress = true;
				break;
			}

			continue;
		}
	}

	return defusingInProgress;
}