//
// HPB bot - botman's High Ping Bastard bot
//
// (http://planethalflife.com/botman/)
//
// bot_combat.cpp
//

#ifndef METAMOD_BUILD
   #include "extdll.h"
   #include "enginecallback.h"
   #include "util.h"
   #include "cbase.h"
   #include "entity_state.h"
#else
   #include <extdll.h>
   #include <dllapi.h>
   #include <h_export.h>
   #include <meta_api.h>
#endif

#include "bot.h"
#include "bot_func.h"
#include "bot_weapons.h"
#include "waypoint.h"

extern int mod_id;
extern bot_weapon_t weapon_defs[MAX_WEAPONS];
extern bool b_observer_mode;
extern int team_allies[4];
extern edict_t *pent_info_ctfdetect;
extern float is_team_play;
extern int is_gameplay;
extern bool checked_teamplay;
extern edict_t *listenserver_edict;
extern bool b_chat_debug;
extern float bot_aim_difficulty;
FILE *fp;

static edict_t *BotGetKtsSnowballCached()
 {
 	static float flCachedTime = -1.0f;
 	static edict_t *pCachedBall = NULL;
 	if (flCachedTime != gpGlobals->time)
 	{
 		flCachedTime = gpGlobals->time;
 		pCachedBall = UTIL_FindEntityByClassname((edict_t *)NULL, "kts_snowball");
 	}
 	return pCachedBall;
 }

//=========================================================
// BotFindBestSkull — per-bot skull selection with per-frame
// entity-scan caching.  The entity list is walked once per
// server frame; scoring (value/sqrt(dist)) is per-bot since
// it depends on the bot's position.
//
// Returns the best skull edict (or NULL).  Writes the
// distance to that skull into *pflDist if non-NULL.
//=========================================================
edict_t *BotFindBestSkull( edict_t *pBotEdict, float *pflDist )
{
	// --- per-frame entity cache ---
	static float  s_flCacheTime = -1.0f;
	static int    s_nSkulls = 0;
	static edict_t *s_pSkulls[128];

	if (s_flCacheTime != gpGlobals->time)
	{
		s_flCacheTime = gpGlobals->time;
		s_nSkulls = 0;
		edict_t *pScan = NULL;
		while ((pScan = UTIL_FindEntityByClassname(pScan, "skull")) != NULL)
		{
			if (FNullEnt(pScan) || pScan->free)
				continue;
			if (s_nSkulls < 128)
				s_pSkulls[s_nSkulls++] = pScan;
		}
	}

	// --- per-bot scoring ---
	edict_t *pBest = NULL;
	float flBestScore = 0.0f;
	float flBestDist  = 9e9f;

	for (int i = 0; i < s_nSkulls; i++)
	{
		edict_t *pSkull = s_pSkulls[i];
		if (FNullEnt(pSkull) || pSkull->free)
			continue;

		float flDist = (pSkull->v.origin - pBotEdict->v.origin).Length();
		if (flDist < 1.0f) flDist = 1.0f;

		float flValue = pSkull->v.fuser1;
		if (flValue < 1.0f) flValue = 1.0f;

		float flScore = flValue / sqrt(flDist);
		if (flScore > flBestScore)
		{
			flBestScore = flScore;
			flBestDist  = flDist;
			pBest       = pSkull;
		}
	}

	if (pflDist)
		*pflDist = pBest ? flBestDist : 0.0f;

	return pBest;
}

float aim_tracking_x_scale[5] = {2.0, 3.0, 4.5, 6.0, 7.5};
float aim_tracking_y_scale[5] = {2.0, 3.0, 4.5, 6.0, 7.5};
// continuous per-frame aim jitter magnitude (degrees) by skill — layered
// on top of the step-error above so tracking is never pixel-perfect
// between refreshes.  Scaled by bot_aim_difficulty at use time.
float aim_jitter_scale[5] = {0.15f, 0.25f, 0.35f, 0.50f, 0.60f};
// who is vomiting?
float g_flVomiting[32];
// reaction time multiplier
extern float bot_reaction_time;
float react_time_min[5] = {0.51, 0.55, 0.58, 0.6, 0.65};
float react_time_max[5] = {0.51, 0.58, 0.6, 0.75, 0.7};
// how transparent does our target have to be before we
// can't see them?  Diffs per bot level
int renderamt_threshold[5] = {16, 32, 48, 64, 80};

bot_research_t g_Researched[2][NUM_RESEARCH_OPTIONS];

float g_flWeaponSwitch = 0;

void BotCheckTeamplay()
{
//	ALERT(at_console, "BotCheckTeamplay\n");

	is_team_play = CVAR_GET_FLOAT("mp_teamplay");  // teamplay enabled?
	const char *gameMode = CVAR_GET_STRING("mp_gamemode");

	if (strstr(gameMode, "jvs") || atoi(gameMode) == GAME_ICEMAN)
		is_team_play = TRUE;

	if (strstr(gameMode, "ctc") || atoi(gameMode) == GAME_CTC)
		is_gameplay = GAME_CTC;

	if (strstr(gameMode, "shidden") || atoi(gameMode) == GAME_SHIDDEN)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_SHIDDEN;
	}

	if (strstr(gameMode, "chilldemic") || atoi(gameMode) == GAME_CHILLDEMIC)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_CHILLDEMIC;
	}

	if (strstr(gameMode, "ctf") || atoi(gameMode) == GAME_CTF)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_CTF;
	}

	if (strstr(gameMode, "coldspot") || atoi(gameMode) == GAME_COLDSPOT)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_COLDSPOT;
	}

	if (strstr(gameMode, "horde") || atoi(gameMode) == GAME_HORDE)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_HORDE;
	}

	if (strstr(gameMode, "prophunt") || atoi(gameMode) == GAME_PROPHUNT)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_PROPHUNT;
	}

	if (strstr(gameMode, "busters") || atoi(gameMode) == GAME_BUSTERS)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_BUSTERS;
	}

	if ((strstr(gameMode, "lms") || atoi(gameMode) == GAME_LMS) && CVAR_GET_FLOAT("mp_royaleteam"))
		is_team_play = TRUE;

	if (strstr(gameMode, "gungame") || atoi(gameMode) == GAME_GUNGAME)
	{
		is_gameplay = GAME_GUNGAME;
	}

	if (strstr(gameMode, "loot") || atoi(gameMode) == GAME_LOOT)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_LOOT;
	}

	if (strstr(gameMode, "kts") || atoi(gameMode) == GAME_KTS)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_KTS;
	}

	if (strstr(gameMode, "coldskull") || atoi(gameMode) == GAME_COLDSKULL)
	{
		is_gameplay = GAME_COLDSKULL;
	}

	if (strstr(gameMode, "arena") || atoi(gameMode) == GAME_ARENA)
	{
		is_team_play = TRUE;
		is_gameplay = GAME_ARENA;
	}

	checked_teamplay = TRUE;
}

//=========================================================
// BotColdskullThink — called from BotThink when in Cold
// Skulls mode and no combat is active.
//
// Scans all skull entities and picks the best one using a
// value/distance weighting.  Sets v_goal + f_move_speed so
// the movement block steers the bot toward the skull.
//
// Returns true when a skull target was found and movement
// intent has been set; false to fall back to normal nav.
//=========================================================
bool BotColdskullThink( bot_t *pBot )
{
	if (is_gameplay != GAME_COLDSKULL)
		return false;

	edict_t *pEdict = pBot->pEdict;

	// Don't chase skulls when critically low on health — seek health first
	if (pEdict->v.health <= 25)
		return false;

	float flBestDist = 0.0f;
	edict_t *pBestSkull = BotFindBestSkull(pEdict, &flBestDist);

	if (pBestSkull == NULL)
		return false;  // No skulls in the world — fall back to normal combat/nav

	// Set movement target toward the best skull
	Vector vecTarget = pBestSkull->v.origin;
	pBot->v_goal           = vecTarget;

	// When very close to the skull, suppress combat so the bot doesn't
	// get distracted fighting mid-collection.
	if (flBestDist < 200.0f && pBot->pBotEnemy)
	{
		pBot->pBotEnemy = NULL;
	}

	// Always run toward the skull.  Skulls have a zero-size bounding box
	// (SOLID_TRIGGER with g_vecZero size) so the bot must physically
	// overlap the skull origin to trigger SkullTouch.  A small proximity
	// value keeps the bot moving all the way in; magnetic skulls that fly
	// toward the bot will simply touch it mid-run.
	pBot->f_goal_proximity = 20.0f;
	pBot->f_move_speed     = pBot->f_max_speed;

	// Face the skull directly so the bot turns toward it immediately.
	Vector vecDir    = vecTarget - pEdict->v.origin;
	Vector vecAngles = UTIL_VecToAngles(vecDir);
	pEdict->v.ideal_yaw = vecAngles.y;
	BotFixIdealYaw(pEdict);

	return true;
}

//=========================================================
// CTF radar constants — defined in the game DLL's const.h
// but not in the bot's copy.  The flag and base entities
// use pev->fuser4 set to these values.
//=========================================================
#define RADAR_FLAG_BLUE  10
#define RADAR_FLAG_RED   11
#define RADAR_BASE_BLUE  12
#define RADAR_BASE_RED   13

//=========================================================
// BotCtfFindEntities — per-frame cached scan for the two
// flag entities and two base entities used by CTF mode.
// Output pointers are set to the matching edicts or NULL.
//=========================================================
static float   s_ctf_cache_time = -1.0f;
static edict_t *s_pBlueFlag = NULL;
static edict_t *s_pRedFlag  = NULL;
static edict_t *s_pBlueBase = NULL;
static edict_t *s_pRedBase  = NULL;

static void BotCtfFindEntities()
{
	if (s_ctf_cache_time == gpGlobals->time)
		return;  // already scanned this frame

	s_ctf_cache_time = gpGlobals->time;
	s_pBlueFlag = NULL;
	s_pRedFlag  = NULL;
	s_pBlueBase = NULL;
	s_pRedBase  = NULL;

	edict_t *pEnt = NULL;

	// Scan for flag entities
	while ((pEnt = UTIL_FindEntityByClassname(pEnt, "flag")) != NULL)
	{
		int radar = (int)pEnt->v.fuser4;
		if (radar == RADAR_FLAG_BLUE)
			s_pBlueFlag = pEnt;
		else if (radar == RADAR_FLAG_RED)
			s_pRedFlag = pEnt;
	}

	// Scan for base entities
	pEnt = NULL;
	while ((pEnt = UTIL_FindEntityByClassname(pEnt, "base")) != NULL)
	{
		int radar = (int)pEnt->v.fuser4;
		if (radar == RADAR_BASE_BLUE)
			s_pBlueBase = pEnt;
		else if (radar == RADAR_BASE_RED)
			s_pRedBase = pEnt;
	}
}

// Return the flag entity belonging to the given team (1=blue, 2=red)
static edict_t *BotCtfGetTeamFlag( int botTeam )
{
	return (botTeam == 1) ? s_pBlueFlag : s_pRedFlag;
}

// Return the enemy flag entity for the given team
static edict_t *BotCtfGetEnemyFlag( int botTeam )
{
	return (botTeam == 1) ? s_pRedFlag : s_pBlueFlag;
}

// Return the base entity belonging to the given team
static edict_t *BotCtfGetOwnBase( int botTeam )
{
	return (botTeam == 1) ? s_pBlueBase : s_pRedBase;
}

// Return the enemy base entity for the given team
static edict_t *BotCtfGetEnemyBase( int botTeam )
{
	return (botTeam == 1) ? s_pRedBase : s_pBlueBase;
}

// Return the edict carrying a flag, or NULL if the flag is loose/home.
// The carrier is stored in pev->aiment by the game rules code.
static edict_t *BotCtfGetFlagCarrier( edict_t *pFlag )
{
	if (FNullEnt(pFlag))
		return NULL;
	edict_t *pCarrier = pFlag->v.aiment;
	if (FNullEnt(pCarrier))
		return NULL;
	if (!IsAlive(pCarrier))
		return NULL;
	return pCarrier;
}

//=========================================================
// BotCtfPreUpdate — called from bot.cpp BEFORE BotFindEnemy
// every frame.  Sets b_ctf_has_flag, bot_has_flag, and
// pre-sets v_goal so the movement block has a target even
// on ticks where the enemy branch runs instead of BotCtfThink.
//=========================================================
void BotCtfPreUpdate( bot_t *pBot )
{
	edict_t *pEdict = pBot->pEdict;
	int botTeam = UTIL_GetTeam(pEdict);

	BotCtfFindEntities();

	edict_t *pEnemyFlag = BotCtfGetEnemyFlag(botTeam);
	edict_t *pOwnFlag   = BotCtfGetTeamFlag(botTeam);
	edict_t *pOwnBase   = BotCtfGetOwnBase(botTeam);

	// Detect if this bot is carrying the enemy flag
	edict_t *pEnemyFlagCarrier = BotCtfGetFlagCarrier(pEnemyFlag);
	pBot->b_ctf_has_flag = (pEnemyFlagCarrier == pEdict);
	pBot->bot_has_flag   = pBot->b_ctf_has_flag;

	if (pBot->b_ctf_has_flag)
	{
		// Carrier: suppress item pickup, pre-set goal to own base
		pBot->pBotPickupItem = NULL;
		pBot->item_waypoint  = -1;
		if (!FNullEnt(pOwnBase))
		{
			pBot->v_goal           = pOwnBase->v.origin;
			pBot->f_goal_proximity = 0.0f;  // run through the trigger
		}
	}
	else
	{
		// Non-carrier: pre-set v_goal based on current role
		switch (pBot->i_ctf_role)
		{
		case CTF_ROLE_RETRIEVER:
		{
			if (!FNullEnt(pOwnFlag))
			{
				edict_t *pOwnFlagCarrier = BotCtfGetFlagCarrier(pOwnFlag);
				if (pOwnFlagCarrier)
					pBot->v_goal = pOwnFlagCarrier->v.origin;
				else
					pBot->v_goal = pOwnFlag->v.origin;
			}
			break;
		}
		case CTF_ROLE_ESCORT:
		{
			if (pEnemyFlagCarrier && pEnemyFlagCarrier != pEdict)
				pBot->v_goal = pEnemyFlagCarrier->v.origin;
			break;
		}
		case CTF_ROLE_DEFENDER:
		{
			if (!FNullEnt(pOwnBase))
				pBot->v_goal = pOwnBase->v.origin;
			break;
		}
		case CTF_ROLE_SEEKER:
		default:
		{
			if (!FNullEnt(pEnemyFlag))
				pBot->v_goal = pEnemyFlag->v.origin;
			break;
		}
		}
	}
}

//=========================================================
// BotGoalElevatedJump — general-purpose multi-jump toward
// an elevated goal.  Call every frame when the bot is close
// horizontally but the goal is above.
//
// Uses a 3-phase jump sequence matching the mod's jump system:
//   phase 0 → detect stall, start 1st jump
//   phase 1 → 2nd jump (double-jump, while airborne)
//   phase 2 → 3rd jump (triple-jump / flip, while airborne)
//   phase 3 → sequence complete, reset after cooldown
//
// Returns true if the bot is currently in a jump sequence
// (caller should keep running forward).
//=========================================================
static bool BotGoalElevatedJump( bot_t *pBot, Vector vecGoal )
{
	edict_t *pEdict = pBot->pEdict;

	float heightDiff = vecGoal.z - pEdict->v.origin.z;
	Vector vecFlat = vecGoal - pEdict->v.origin;
	vecFlat.z = 0;
	float horzDist = vecFlat.Length();

	// Only start a new jump sequence when close horizontally (< 300u) and
	// goal is above step height (> 20u).  Once a sequence is in-progress
	// (phase 1-2), skip this check because the bot's own Z rises during
	// the jump, making heightDiff temporarily drop below the threshold.
	if (pBot->i_goal_jump_phase == 0 && (horzDist > 300.0f || heightDiff < 20.0f))
	{
		pBot->f_goal_jump_stall_time = 0.0f;
		return false;
	}

	// Track how long we've been stuck below the goal
	if (pBot->f_goal_jump_stall_time == 0.0f)
		pBot->f_goal_jump_stall_time = gpGlobals->time;

	// Wait 0.5s before starting jump sequence (gives waypoint nav a chance)
	if (gpGlobals->time - pBot->f_goal_jump_stall_time < 0.5f)
		return false;

	// Face the goal and run toward it
	Vector vecDir = vecGoal - pEdict->v.origin;
	Vector vecAngles = UTIL_VecToAngles(vecDir);
	pEdict->v.ideal_yaw = vecAngles.y;
	BotFixIdealYaw(pEdict);
	pBot->f_move_speed = pBot->f_max_speed;

	// Phase 0: Start first jump (ground jump)
	if (pBot->i_goal_jump_phase == 0 && pBot->f_goal_jump_time < gpGlobals->time)
	{
		pEdict->v.button |= IN_JUMP;
		pBot->i_goal_jump_phase = 1;
		pBot->f_goal_jump_time = gpGlobals->time + 0.15f;
		return true;
	}

	// Phase 1: Double jump (2nd press while airborne)
	if (pBot->i_goal_jump_phase == 1 && pBot->f_goal_jump_time < gpGlobals->time)
	{
		// Timer expired: perform the 2nd jump press now while airborne.
		// This phase delays the press until the scheduled time; it does not
		// insert a separate release-only frame before advancing to Phase 2.
		pEdict->v.button |= IN_JUMP;
		pBot->i_goal_jump_phase = 2;
		pBot->f_goal_jump_time = gpGlobals->time + 0.15f;
		return true;
	}

	// Phase 2: Triple jump / flip (3rd press while airborne)
	if (pBot->i_goal_jump_phase == 2 && pBot->f_goal_jump_time < gpGlobals->time)
	{
		pEdict->v.button |= IN_JUMP;
		pBot->i_goal_jump_phase = 3;
		pBot->f_goal_jump_time = gpGlobals->time + 1.0f;  // cooldown before retrying
		return true;
	}

	// Phase 3: Sequence complete — wait for cooldown then retry
	if (pBot->i_goal_jump_phase == 3 && pBot->f_goal_jump_time < gpGlobals->time)
	{
		pBot->i_goal_jump_phase      = 0;
		pBot->f_goal_jump_stall_time = gpGlobals->time;  // re-arm the 0.5s wait
		return false;
	}

	return (pBot->i_goal_jump_phase > 0);
}

//=========================================================
// BotCtfThink — called from BotThink when in CTF mode.
//
// Role-based objective AI:
//  1. CARRIER  — bot has enemy flag → run to own base to score
//  2. RETRIEVER — own flag is not at home → rush to touch/return it
//  3. ESCORT   — teammate has enemy flag → follow and protect
//  4. DEFENDER — guard own flag/base area
//  5. SEEKER   — go grab enemy flag (default)
//
// Returns true when movement intent has been set; false to
// fall back to normal nav/combat.
//=========================================================
bool BotCtfThink( bot_t *pBot )
{
	if (is_gameplay != GAME_CTF)
		return false;

	edict_t *pEdict = pBot->pEdict;
	int botTeam = UTIL_GetTeam(pEdict);

	// Refresh the cached flag/base entity pointers
	BotCtfFindEntities();

	edict_t *pOwnFlag   = BotCtfGetTeamFlag(botTeam);
	edict_t *pEnemyFlag = BotCtfGetEnemyFlag(botTeam);
	edict_t *pOwnBase   = BotCtfGetOwnBase(botTeam);

	// If flags/bases don't exist yet, fall back to normal nav
	if (FNullEnt(pEnemyFlag) || FNullEnt(pOwnBase))
		return false;

	// ------------------------------------------------------------------
	// Detect carrier status (authoritative — set every frame)
	// ------------------------------------------------------------------
	edict_t *pEnemyFlagCarrier = BotCtfGetFlagCarrier(pEnemyFlag);
	pBot->b_ctf_has_flag = (pEnemyFlagCarrier == pEdict);
	pBot->bot_has_flag   = pBot->b_ctf_has_flag;

	// ------------------------------------------------------------------
	// Role evaluation — every 0.5 seconds to avoid thrashing
	// ------------------------------------------------------------------
	if (pBot->f_ctf_role_eval_time < gpGlobals->time)
	{
		pBot->f_ctf_role_eval_time = gpGlobals->time + 0.5f;

		// Priority 1: Am I carrying the enemy flag?
		if (pBot->b_ctf_has_flag)
		{
			pBot->i_ctf_role = CTF_ROLE_CARRIER;
		}
		// Priority 2: Is our own flag NOT at home? (someone stole it / it's dropped)
		// base pev->iuser4 == TRUE means flag is at home
		else if (!FNullEnt(pOwnFlag) && !FNullEnt(pOwnBase) && !pOwnBase->v.iuser4)
		{
			// Check if the own flag is being carried by an enemy or is on the ground
			edict_t *pOwnFlagCarrier = BotCtfGetFlagCarrier(pOwnFlag);
			if (pOwnFlagCarrier != NULL)
			{
				// Enemy is carrying our flag — chase them to kill and force a drop
				pBot->i_ctf_role = CTF_ROLE_RETRIEVER;
			}
			else
			{
				// Flag is on the ground — rush to touch it and return it
				pBot->i_ctf_role = CTF_ROLE_RETRIEVER;
			}
		}
		// Priority 3: Is a teammate carrying the enemy flag?
		else if (pEnemyFlagCarrier != NULL && pEnemyFlagCarrier != pEdict
			&& UTIL_GetTeam(pEnemyFlagCarrier) == botTeam)
		{
			// Cap escorts at roughly 50% of team bots — others become seekers/defenders
			// Simple approach: alternate based on bot edict index
			int idx = ENTINDEX(pEdict);
			if (idx % 2 == 0)
				pBot->i_ctf_role = CTF_ROLE_ESCORT;
			else
				pBot->i_ctf_role = CTF_ROLE_DEFENDER;
		}
		// Priority 4: Default — go seek enemy flag.
		// DEFENDER is only assigned under Priority 3 (when a teammate
		// carries the flag and we split escort/defense).  Proximity-based
		// auto-defend was removed because with few bots (or right after
		// spawning near the base) it caused the only bot to idle at base.
		else
		{
			pBot->i_ctf_role = CTF_ROLE_SEEKER;
		}
	}

	// Force CARRIER role immediately when flag is held (don't wait for timer)
	if (pBot->b_ctf_has_flag)
		pBot->i_ctf_role = CTF_ROLE_CARRIER;

	// ------------------------------------------------------------------
	// Execute behavior based on role
	// ------------------------------------------------------------------
	switch (pBot->i_ctf_role)
	{
	// =================================================================
	// CARRIER — run to own base to score
	// =================================================================
	case CTF_ROLE_CARRIER:
	{
		pBot->f_pause_time = 0;
		pBot->f_move_speed = pBot->f_max_speed;

		// Suppress item pickup — don't get distracted
		pBot->pBotPickupItem = NULL;
		pBot->item_waypoint  = -1;

		// Set goal to own base
		pBot->v_goal           = pOwnBase->v.origin;
		pBot->f_goal_proximity = 0.0f;  // run through the trigger — don't stop short

		// Multi-jump if base is elevated above us
		BotGoalElevatedJump(pBot, pOwnBase->v.origin);

		return true;
	}

	// =================================================================
	// RETRIEVER — rush to return own flag
	// =================================================================
	case CTF_ROLE_RETRIEVER:
	{
		if (FNullEnt(pOwnFlag))
			return false;

		pBot->f_pause_time = 0;
		pBot->f_move_speed = pBot->f_max_speed;

		edict_t *pOwnFlagCarrier = BotCtfGetFlagCarrier(pOwnFlag);
		Vector vecTarget;

		if (pOwnFlagCarrier != NULL)
		{
			// Enemy is carrying our flag — chase the carrier
			vecTarget = pOwnFlagCarrier->v.origin;
			pBot->f_goal_proximity = 0.0f;
		}
		else
		{
			// Flag is dropped on the ground — rush to touch it
			vecTarget = pOwnFlag->v.origin;
			pBot->f_goal_proximity = 0.0f;  // run through the trigger

			// Suppress combat when very close to prioritize returning flag
			float distToFlag = (pOwnFlag->v.origin - pEdict->v.origin).Length();
			if (distToFlag < 200.0f && pBot->pBotEnemy)
			{
				pBot->pBotEnemy = NULL;
			}
		}

		pBot->v_goal = vecTarget;

		// Multi-jump if the flag is elevated above us
		BotGoalElevatedJump(pBot, vecTarget);

		return true;
	}

	// =================================================================
	// ESCORT — follow and protect the flag carrier teammate
	// =================================================================
	case CTF_ROLE_ESCORT:
	{
		// If no teammate is carrying, fall through to seeker
		if (FNullEnt(pEnemyFlagCarrier) || pEnemyFlagCarrier == pEdict
			|| UTIL_GetTeam(pEnemyFlagCarrier) != botTeam)
		{
			// Teammate lost the flag — become seeker
			pBot->i_ctf_role = CTF_ROLE_SEEKER;
			break;  // will fall through to SEEKER below via re-entry next frame
		}

		pBot->f_move_speed = pBot->f_max_speed;

		// Stay near the carrier with a slight offset
		pBot->v_goal           = pEnemyFlagCarrier->v.origin;
		pBot->f_goal_proximity = 128.0f;

		return true;
	}

	// =================================================================
	// DEFENDER — patrol near own base
	// =================================================================
	case CTF_ROLE_DEFENDER:
	{
		if (FNullEnt(pOwnBase))
			return false;

		pBot->f_move_speed = pBot->f_max_speed;

		// Set goal to own base area
		pBot->v_goal           = pOwnBase->v.origin;
		pBot->f_goal_proximity = 256.0f;



		return true;
	}

	// =================================================================
	// SEEKER — go grab enemy flag (default)
	// =================================================================
	case CTF_ROLE_SEEKER:
	default:
	{
		pBot->f_move_speed = pBot->f_max_speed;

		// Head to enemy flag location
		pBot->v_goal           = pEnemyFlag->v.origin;
		pBot->f_goal_proximity = 0.0f;  // run through the trigger

		// Multi-jump if the flag is elevated above us
		BotGoalElevatedJump(pBot, pEnemyFlag->v.origin);

		return true;
	}
	}

	return false;
}

//=========================================================
// BotArenaPreUpdate — called from BotThink BEFORE
// BotFindEnemy every frame in Arena (1v1) mode.
//
// Finds the single opponent, caches their index, pre-sets
// v_goal toward their position, and clears item pickup so
// the bot never detours to items.
//=========================================================
void BotArenaPreUpdate( bot_t *pBot )
{
	edict_t *pEdict = pBot->pEdict;
	int botTeam = UTIL_GetTeam(pEdict);

	// Find the one alive opponent on a different team
	pBot->i_arena_opponent = 0;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		edict_t *pPlayer = INDEXENT(i);
		if (FNullEnt(pPlayer) || pPlayer == pEdict)
			continue;
		if (!IsAlive(pPlayer))
			continue;
		if (pPlayer->v.flags & FL_SPECTATOR)
			continue;
		int otherTeam = UTIL_GetTeam(pPlayer);
		if (otherTeam != botTeam)
		{
			pBot->i_arena_opponent = i;
			pBot->v_goal           = pPlayer->v.origin;
			pBot->f_goal_proximity = 0.0f;
			break;
		}
	}

	// Suppress item detours — sole objective is the opponent
	pBot->pBotPickupItem = NULL;
	pBot->item_waypoint  = -1;
}

//=========================================================
// BotArenaThink — called from BotThink when in Arena (1v1)
// mode and no combat is active.
//
// Validates the cached Arena opponent and, when valid, keeps
// the bot's goal pointed at that opponent so the normal
// movement/direction code can continue pursuing them.
//
// Returns true when a valid Arena opponent is available and
// v_goal/f_goal_proximity have been refreshed; false to fall
// back to normal navigation behavior.
//=========================================================
bool BotArenaThink( bot_t *pBot )
{
	if (is_gameplay != GAME_ARENA)
		return false;

	edict_t *pEdict = pBot->pEdict;

	// Validate opponent
	edict_t *pOpponent = NULL;
	if (pBot->i_arena_opponent > 0)
		pOpponent = INDEXENT(pBot->i_arena_opponent);

	if (FNullEnt(pOpponent) || !IsAlive(pOpponent))
		return false; // no valid opponent, fallback to wander

	// Keep v_goal pointed at opponent for the movement/direction block.
	// Waypoint goal selection is handled by BotFindWaypointGoal (arena
	// section in bot_navigate.cpp), which finds the nearest waypoint to
	// the opponent every 0.5s.  Floyd routing then advances hop-by-hop.
	pBot->v_goal = pOpponent->v.origin;
	pBot->f_goal_proximity = 0.0f;

	return true;
}

//=========================================================
// BotCtcThink — called from BotThink when in Capture The
// Chumtoad mode and no combat is active.
//
// Priority hierarchy:
//  1. Bot IS holding the chumtoad → evade enemies, keep running
//     to score points.  Strategic drop when health is critical.
//  2. An opponent IS holding the chumtoad → pursue them.
//  3. Chumtoad is loose on the map → run toward it to pick it up.
//  4. No chumtoad in play → fall back to normal nav.
//
// Returns true when movement intent has been set; false to
// fall back to normal nav/combat.
//=========================================================
bool BotCtcThink( bot_t *pBot )
{
	if (is_gameplay != GAME_CTC)
		return false;

	edict_t *pEdict = pBot->pEdict;

	// -----------------------------------------------------------------
	// Case 1: Bot IS holding the chumtoad — evade and score.
	// -----------------------------------------------------------------
	if (pBot->b_ctc_has_chumtoad)
	{
		// Never pause while carrying — velocity must stay > 50 u/s to score.
		pBot->f_pause_time = 0;
		pBot->f_move_speed = pBot->f_max_speed;

		// Strategic drop: if health is critical, drop the toad and flee.
		if (pEdict->v.health <= 30 &&
			pBot->f_ctc_drop_consider_time < gpGlobals->time)
		{
			// Fire the chumtoad weapon to trigger DropCharm server-side
			pEdict->v.button |= IN_ATTACK;
			pBot->f_ctc_drop_consider_time = gpGlobals->time + 5.0f;
			// After this tick the server will call DropCharm, b_ctc_has_chumtoad
			// will clear next frame, and the bot will become a chaser seeking health.
			return true;
		}

		// Evasion: find the nearest enemy and run AWAY from them.
		// Re-evaluate escape direction every 2 seconds to avoid jitter.
		if (pBot->f_ctc_escape_time < gpGlobals->time)
		{
			pBot->f_ctc_escape_time = gpGlobals->time + 2.0f;

			edict_t *pNearest = NULL;
			float flNearestDist = 9e9f;

			for (int i = 1; i <= gpGlobals->maxClients; i++)
			{
				edict_t *pPlayer = INDEXENT(i);
				if (FNullEnt(pPlayer) || pPlayer == pEdict)
					continue;
				if (!IsAlive(pPlayer))
					continue;
				if (pPlayer->v.flags & FL_FAKECLIENT || pPlayer->v.flags & FL_CLIENT)
				{
					float dist = (pPlayer->v.origin - pEdict->v.origin).Length();
					if (dist < flNearestDist)
					{
						flNearestDist = dist;
						pNearest = pPlayer;
					}
				}
			}

			if (pNearest && flNearestDist < 1500.0f)
			{
				// Run in the opposite direction from the nearest threat
				Vector vecAway = pEdict->v.origin - pNearest->v.origin;
				vecAway.z = 0; // keep horizontal
				vecAway = vecAway.Normalize();
				pBot->v_goal = pEdict->v.origin + vecAway * 800.0f;
			}
			else
			{
				// No nearby threat — just keep running forward
				MAKE_VECTORS(pEdict->v.v_angle);
				pBot->v_goal = pEdict->v.origin + gpGlobals->v_forward * 500.0f;
			}
		}

		pBot->f_goal_proximity = 40.0f;

		// --- Evasive jump / duck ---
		// More frequent when taking damage (health < 80), otherwise occasional.
		if (pBot->f_ctc_next_juke_time < gpGlobals->time)
		{
			bool bUnderPressure = (pEdict->v.health < 80);
			float flChance = bUnderPressure ? 0.45f : 0.15f;

			if (RANDOM_FLOAT(0.0f, 1.0f) < flChance)
			{
				if (RANDOM_LONG(0, 1))
					pEdict->v.button |= IN_JUMP;
				else
					pEdict->v.button |= IN_DUCK;
			}

			pBot->f_ctc_next_juke_time = gpGlobals->time +
				(bUnderPressure ? RANDOM_FLOAT(0.8f, 1.5f) : RANDOM_FLOAT(2.0f, 4.0f));
		}

		// --- Special evasive moves (slide, hurricane kick, flip) ---
		if (pBot->f_ctc_next_move_time < gpGlobals->time)
		{
			bool bUnderPressure = (pEdict->v.health < 80);
			float flChance = bUnderPressure ? 0.35f : 0.10f;

			if (RANDOM_FLOAT(0.0f, 1.0f) < flChance)
			{
				switch (RANDOM_LONG(0, 2))
				{
				case 0: // slide or hurricane kick
					pEdict->v.impulse = RANDOM_LONG(0, 1) ? 208 : 214;
					break;
				case 1: // flip (front, back, side)
					pEdict->v.impulse = 210 + RANDOM_LONG(0, 2);
					break;
				case 2: // slide
					pEdict->v.impulse = 208;
					break;
				}
			}

			pBot->f_ctc_next_move_time = gpGlobals->time +
				(bUnderPressure ? RANDOM_FLOAT(1.5f, 3.0f) : RANDOM_FLOAT(3.0f, 6.0f));
		}

		// Face escape direction
		Vector vecDir    = pBot->v_goal - pEdict->v.origin;
		Vector vecAngles = UTIL_VecToAngles(vecDir);
		pEdict->v.ideal_yaw = vecAngles.y;
		BotFixIdealYaw(pEdict);

		return true;
	}

	// -----------------------------------------------------------------
	// Case 2: An opponent IS holding the chumtoad — pursue them.
	// -----------------------------------------------------------------
	{
		edict_t *pHolder = NULL;

		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			edict_t *pPlayer = INDEXENT(i);
			if (FNullEnt(pPlayer) || pPlayer == pEdict)
				continue;
			if (!IsAlive(pPlayer))
				continue;
			if (pPlayer->v.fuser4 > 0)
			{
				pHolder = pPlayer;
				break; // only one holder at a time
			}
		}

		if (pHolder)
		{
			pBot->v_goal           = pHolder->v.origin;
			pBot->f_goal_proximity = 0.0f;
			pBot->f_move_speed     = pBot->f_max_speed;

			// Face toward the holder
			Vector vecDir    = pHolder->v.origin - pEdict->v.origin;
			Vector vecAngles = UTIL_VecToAngles(vecDir);
			pEdict->v.ideal_yaw = vecAngles.y;
			BotFixIdealYaw(pEdict);

			return true;
		}
	}

	// -----------------------------------------------------------------
	// Case 3: Chumtoad is loose on the map — run toward it.
	// -----------------------------------------------------------------
	{
		edict_t *pToad = UTIL_FindEntityByClassname(
			(edict_t *)NULL, "monster_ctctoad");

		if (!FNullEnt(pToad) && !(pToad->v.effects & EF_NODRAW))
		{
			Vector vecTarget = pToad->v.origin;
			pBot->v_goal           = vecTarget;
			pBot->f_goal_proximity = 20.0f;
			pBot->f_move_speed     = pBot->f_max_speed;

			// Face toward the chumtoad
			Vector vecDir    = vecTarget - pEdict->v.origin;
			Vector vecAngles = UTIL_VecToAngles(vecDir);
			pEdict->v.ideal_yaw = vecAngles.y;
			BotFixIdealYaw(pEdict);

			return true;
		}
	}

	// -----------------------------------------------------------------
	// Case 4: No chumtoad in play — fall back to normal nav.
	// -----------------------------------------------------------------
	return false;
}

//=========================================================
// BotKtsThink — called from BotThink when in KTS mode and
// no combat is active.
//
// Priority hierarchy:
//  1. Bot IS the dribbler → navigate to enemy goal and score.
//  2. An opponent IS dribbling → that player becomes the enemy
//     so BotShootAtEnemy will run toward them and tackle.
//  3. Ball is loose → run toward it (waypoint nav already handles
//     this; return false to fall through to normal navigation).
//
// Returns true when this function has fully set the bot's
// movement intent; returns false to fall back to normal nav.
//=========================================================
bool BotKtsThink( bot_t *pBot )
{
	if (is_gameplay != GAME_KTS)
		return false;

	edict_t *pEdict = pBot->pEdict;

	// Locate the snowball
	edict_t *pBall = UTIL_FindEntityByClassname((edict_t *)NULL, "kts_snowball");
	if (FNullEnt(pBall))
		return false;

	Vector ballOrigin = pBall->v.origin;

	// Team mapping: UTIL_GetTeam returns 1=blue(iceman), 2=red(santa)
	// kts_goal pev->body: 0=TEAM_BLUE goal, 1=TEAM_RED goal
	// Blue bot (1) scores in red goal (body==1); red bot (2) in blue goal (body==0)
	int botTeam      = UTIL_GetTeam(pEdict);
	int enemyGoalBody = (botTeam == 1) ? 1 : 0;

	// --- Dribble detection -------------------------------------------
	// b_kts_has_ball is authoritative from the pre-update block in bot.cpp
	// (runs before BotFindEnemy every tick).  Do NOT re-assign it here —
	// a second distance check with a freshly moved ball can flicker the
	// flag and immediately reset v_goal before the movement block reads it.
	bool ballBeingDribbled = (pBall->v.movetype == MOVETYPE_NOCLIP);
	float distToBall = (ballOrigin - pEdict->v.origin).Length();

	// -----------------------------------------------------------------
	// Case 1: THIS bot is dribbling — head straight for the enemy goal.
	// -----------------------------------------------------------------
	if (pBot->b_kts_has_ball)
	{
		// Clear any stale enemy so combat routines don't interfere
		pBot->pBotEnemy = NULL;

		// Find enemy goal
		edict_t *pGoal = NULL;
		edict_t *pEnemyGoal = NULL;
		while ((pGoal = UTIL_FindEntityByClassname(pGoal, "kts_goal")) != NULL)
		{
			if (pGoal->v.body == enemyGoalBody)
			{
				pEnemyGoal = pGoal;
				break;
			}
		}

		if (pEnemyGoal)
		{
			Vector goalTarget = pEnemyGoal->v.origin;
			// Store in v_goal so the movement direction block in bot.cpp can
			// steer directly at the goal when close enough (< 512u).
			pBot->v_goal = goalTarget;
			pBot->f_goal_proximity = 0.0f; // run all the way through the trigger

			Vector dir    = goalTarget - pEdict->v.origin;
			float  dist   = dir.Length();

			// Override ideal_yaw toward the goal when close (< 300u) or
			// visible.  The kts_goal trigger volume origin can sit at floor
			// level, causing FVisible to fail even when the goal is clearly
			// reachable.  The distance fallback must match ktsDirectSteer in
			// the movement direction block so that yaw and direction switch
			// to direct navigation at the same moment — a mismatch would
			// make cos(dgrad) ≈ 0 and stall the bot.
			if (dist < 300.0f || FVisible(goalTarget, pEdict))
			{
				Vector angles = UTIL_VecToAngles(dir);
				pEdict->v.ideal_yaw = angles.y;
				BotFixIdealYaw(pEdict);
			}

			// ---------------------------------------------------------
			// Elevated-goal scoring: if the goal is significantly above
			// the bot, try jumping first.  After a few failed jumps,
			// back up and kick the ball (impulse 206) toward the goal.
			// ---------------------------------------------------------
			float heightDiff = goalTarget.z - pEdict->v.origin.z;
			bool goalElevated = (heightDiff > 36.0f && dist < 300.0f);

			if (goalElevated)
			{
				// Phase 1 — attempt jumps toward the goal
				if (!pBot->b_kts_kick_pending && pBot->i_kts_jump_count < 3)
				{
					if (pBot->f_kts_jump_time < gpGlobals->time)
					{
						pEdict->v.button |= IN_JUMP;
						pBot->i_kts_jump_count++;
						pBot->f_kts_jump_time = gpGlobals->time + 0.6f;
					}
				}
				// Phase 2 — jumps exhausted, back up and kick the ball
				else
				{
					if (!pBot->b_kts_kick_pending)
					{
						// Begin the kick sequence: schedule it 0.8s from now
						// so the bot has time to back away first.
						pBot->b_kts_kick_pending = true;
						pBot->f_kts_kick_time = gpGlobals->time + 0.8f;
					}

					if (pBot->f_kts_kick_time > gpGlobals->time)
					{
						// Backing up: reverse movement and look up at the goal
						pBot->f_move_speed = -(pBot->f_max_speed);

						Vector aimDir = goalTarget - (pEdict->v.origin + pEdict->v.view_ofs);
						Vector aimAng = UTIL_VecToAngles(aimDir);
						pEdict->v.ideal_yaw = aimAng.y;
						BotFixIdealYaw(pEdict);
						pEdict->v.idealpitch = -aimAng.x;
						BotFixIdealPitch(pEdict);
					}
					else
					{
						// Time to kick — aim at the goal and fire impulse 206
						Vector aimDir = goalTarget - (pEdict->v.origin + pEdict->v.view_ofs);
						Vector aimAng = UTIL_VecToAngles(aimDir);
						pEdict->v.ideal_yaw = aimAng.y;
						BotFixIdealYaw(pEdict);
						pEdict->v.idealpitch = -aimAng.x;
						BotFixIdealPitch(pEdict);

						pEdict->v.impulse = 206;

						// Reset state so the bot resumes normal play
						pBot->b_kts_kick_pending = false;
						pBot->i_kts_jump_count   = 0;
						pBot->f_kts_kick_time    = 0.0f;
					}

					return true;  // skip normal movement while in kick sequence
				}
			}
			else
			{
				// Goal is at the same level — reset elevated-goal state
				pBot->i_kts_jump_count   = 0;

				// ---------------------------------------------------------
				// Same-level stall detection: the bot is very close to the
				// goal but the score trigger hasn't fired.  Track how long
				// the bot stays within 100u — if it lingers for 1.5s without
				// scoring it's stuck (fidgeting/oscillating), regardless of
				// instantaneous velocity.
				// ---------------------------------------------------------
				bool nearGoal = (dist < 100.0f);

				if (nearGoal && !pBot->b_kts_kick_pending)
				{
					// Start or maintain the stall timer
					if (pBot->f_kts_stall_time == 0.0f)
						pBot->f_kts_stall_time = gpGlobals->time;

					float stallDuration = gpGlobals->time - pBot->f_kts_stall_time;

					if (stallDuration < 1.5f)
					{
						// Phase 1 — try walking straight into the goal
						pBot->f_move_speed = pBot->f_max_speed;
					}
					else
					{
						// Phase 2 — walking-in failed, begin kick sequence
						pBot->b_kts_kick_pending = true;
						pBot->f_kts_kick_time = gpGlobals->time + 0.8f;
					}
				}
				else if (!nearGoal && !pBot->b_kts_kick_pending)
				{
					// Moving normally — reset stall timer
					pBot->f_kts_stall_time = 0.0f;
				}

				if (pBot->b_kts_kick_pending)
				{
					if (pBot->f_kts_kick_time > gpGlobals->time)
					{
						// Backing up: reverse movement and aim at the goal
						pBot->f_move_speed = -(pBot->f_max_speed);

						Vector aimDir = goalTarget - (pEdict->v.origin + pEdict->v.view_ofs);
						Vector aimAng = UTIL_VecToAngles(aimDir);
						pEdict->v.ideal_yaw = aimAng.y;
						BotFixIdealYaw(pEdict);
						pEdict->v.idealpitch = -aimAng.x;
						BotFixIdealPitch(pEdict);
					}
					else
					{
						// Time to kick — aim at the goal and fire impulse 206
						Vector aimDir = goalTarget - (pEdict->v.origin + pEdict->v.view_ofs);
						Vector aimAng = UTIL_VecToAngles(aimDir);
						pEdict->v.ideal_yaw = aimAng.y;
						BotFixIdealYaw(pEdict);
						pEdict->v.idealpitch = -aimAng.x;
						BotFixIdealPitch(pEdict);

						pEdict->v.impulse = 206;

						// Reset all state so the bot resumes normal play
						pBot->b_kts_kick_pending = false;
						pBot->f_kts_stall_time   = 0.0f;
						pBot->f_kts_kick_time    = 0.0f;
					}

					return true;  // skip normal movement while in kick sequence
				}
			}

			if (b_chat_debug)
			{
				sprintf(pBot->debugchat, "KTS->goal body=%d (%.0f,%.0f,%.0f) dist=%.0f",
					pEnemyGoal->v.body, goalTarget.x, goalTarget.y, goalTarget.z,
					dist);
				UTIL_HostSay(pBot->pEdict, 0, pBot->debugchat);
			}
		}
		else if (b_chat_debug)
		{
			sprintf(pBot->debugchat, "KTS Case1: NO enemy goal (want body=%d)", enemyGoalBody);
			UTIL_HostSay(pBot->pEdict, 0, pBot->debugchat);
		}

		pBot->f_move_speed = pBot->f_max_speed;
		return true;
	}

	// Bot no longer has the ball — clear v_goal so the movement block
	// doesn't keep steering toward the old goal position.
	pBot->v_goal = g_vecZero;

	// Reset elevated-goal kick state in case the bot lost the ball mid-sequence
	pBot->i_kts_jump_count   = 0;
	pBot->b_kts_kick_pending = false;
	pBot->f_kts_kick_time    = 0.0f;
	pBot->f_kts_stall_time   = 0.0f;

	// -----------------------------------------------------------------
	// Case 2: An OPPONENT is dribbling — target them as enemy so the
	// bot runs at them and melee-tackles the ball loose.
	// BotFindEnemy already handles this (it prioritises the ball-carrier)
	// but we also steer yaw toward that player here for responsiveness.
	// -----------------------------------------------------------------
	if (ballBeingDribbled)
	{
		// Use pev->euser1 (set by CaptureCharm) to identify the carrier directly.
		edict_t *pCarrier = !FNullEnt(pBall->v.euser1) ? pBall->v.euser1 : NULL;
		if (pCarrier && pCarrier != pEdict
			&& (pCarrier->v.flags & FL_CLIENT) && IsAlive(pCarrier)
			&& UTIL_GetTeam(pCarrier) != botTeam)
		{
			pBot->v_goal           = pCarrier->v.origin;
			pBot->f_goal_proximity = 48.0f;
			Vector dir    = pCarrier->v.origin - pEdict->v.origin;
			Vector angles = UTIL_VecToAngles(dir);
			pEdict->v.ideal_yaw = angles.y;
			BotFixIdealYaw(pEdict);
			pBot->f_move_speed = pBot->f_max_speed;
			return true;
		}
	}

	// -----------------------------------------------------------------
	// Case 3: Ball is loose and within direct push range — face goal
	// and run into the ball.
	// -----------------------------------------------------------------
	if (!ballBeingDribbled && distToBall <= 120.0f)
	{
		edict_t *pGoal = NULL;
		edict_t *pEnemyGoal = NULL;
		while ((pGoal = UTIL_FindEntityByClassname(pGoal, "kts_goal")) != NULL)
		{
			if (pGoal->v.body == enemyGoalBody)
			{
				pEnemyGoal = pGoal;
				break;
			}
		}

		Vector aimTarget = pEnemyGoal
			? (pEnemyGoal->v.origin + Vector(0, 0, 0))
			: ballOrigin;

		Vector dir    = aimTarget - pEdict->v.origin;
		Vector angles = UTIL_VecToAngles(dir);
		pEdict->v.ideal_yaw = angles.y;
		BotFixIdealYaw(pEdict);

		pBot->f_move_speed = pBot->f_max_speed;
		return true;
	}

	// -----------------------------------------------------------------
	// Case 4: Ball is loose but not yet in kick range.
	// Set v_goal so the movement block can steer directly when the ball
	// is visible, and run at full speed.  Waypoint routing is handled by
	// BotFindWaypointGoal (updates waypoint_goal to the nearest waypoint
	// to the ball every 0.5s).  Do NOT set pBotPickupItem here — a non-NULL
	// pBotPickupItem blocks BotKtsThink in the if/else-if chain on
	// subsequent ticks (the pBotPickupItem block fires, BotKtsThink in
	// the else-if is skipped, and v_goal is never refreshed).
	// -----------------------------------------------------------------
	pBot->v_goal           = ballOrigin;
	pBot->f_goal_proximity = 0.0f;
	pBot->f_move_speed     = pBot->f_max_speed;

	// Face the ball — BotHeadTowardWaypoint runs BEFORE this function and
	// sets ideal_yaw toward the (possibly stale) curr_waypoint_index.
	// Override it here so the bot turns toward the ball immediately.
	Vector ballDir    = ballOrigin - pEdict->v.origin;
	Vector ballAngles = UTIL_VecToAngles(ballDir);
	pEdict->v.ideal_yaw = ballAngles.y;
	BotFixIdealYaw(pEdict);

	return true;
}


//=========================================================
// Cold Spot — zone-hold mode
//
// Gamerules: a single "coldspot" entity spawns on the map
// (classname "coldspot", pev->fuser4 == RADAR_COLD_SPOT).
// Players within 256 units, alive, with clear line of sight
// to the spot center earn team points every 2 seconds.  If
// both teams are present the zone is contested and nobody
// scores.  The spot may relocate during the match.
//=========================================================
#define RADAR_COLD_SPOT        5
#define CSPOT_ZONE_RADIUS      256.0f
#define CSPOT_HOLDER_RADIUS    128.0f
#define CSPOT_DEFEND_RADIUS    200.0f
#define CSPOT_NEARBY_RADIUS    512.0f
#define CSPOT_DIRECT_STEER     300.0f

static float   s_coldspot_cache_time = -1.0f;
static edict_t *s_pColdSpot = NULL;

static void BotColdSpotFindEntity()
{
	if (s_coldspot_cache_time == gpGlobals->time)
		return;

	s_coldspot_cache_time = gpGlobals->time;
	s_pColdSpot = NULL;

	edict_t *pEnt = NULL;
	while ((pEnt = UTIL_FindEntityByClassname(pEnt, "coldspot")) != NULL)
	{
		if ((int)pEnt->v.fuser4 == RADAR_COLD_SPOT)
		{
			s_pColdSpot = pEnt;
			break;
		}
	}
}

static edict_t *BotColdSpotGetEntity()
{
	BotColdSpotFindEntity();
	return s_pColdSpot;
}

// Find the nearest living enemy player inside the scoring zone so HOLDER
// bots can face the most pressing intruder.  Returns NULL when the zone
// is clear of enemies.
static edict_t *BotColdSpotFindZoneIntruder( edict_t *pSpot, int botTeam )
{
	if (FNullEnt(pSpot))
		return NULL;

	edict_t *pBest = NULL;
	float flBestDist = CSPOT_ZONE_RADIUS + 1.0f;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		edict_t *pPlayer = INDEXENT(i);
		if (!pPlayer || pPlayer->free || !(pPlayer->v.flags & FL_CLIENT))
			continue;
		if (!IsAlive(pPlayer))
			continue;
		if (pPlayer->v.iuser1)
			continue;
		if (UTIL_GetTeam(pPlayer) == botTeam)
			continue;
		float d = (pPlayer->v.origin - pSpot->v.origin).Length();
		if (d < flBestDist)
		{
			flBestDist = d;
			pBest = pPlayer;
		}
	}
	return pBest;
}

//=========================================================
// BotColdSpotPreUpdate — called from bot.cpp BEFORE
// BotFindEnemy every frame in Cold Spot mode.
//
// Refreshes the entity cache, detects spot relocation (to
// invalidate stale waypoint routing), evaluates the bot's
// role at ~0.75s intervals, and pre-sets v_goal so the
// movement block always has a current destination even on
// ticks where the enemy branch runs instead of the Think.
//=========================================================
void BotColdSpotPreUpdate( bot_t *pBot )
{
	if (is_gameplay != GAME_COLDSPOT)
		return;

	edict_t *pSpot = BotColdSpotGetEntity();
	if (FNullEnt(pSpot))
		return;

	edict_t *pEdict = pBot->pEdict;
	int botTeam = UTIL_GetTeam(pEdict);

	// Detect spot relocation — wipe stale waypoint routing so the bot
	// re-pathfinds to the new origin on the next BotHeadTowardWaypoint.
	// Clear every field that could otherwise pin the bot to the old
	// spot's waypoint: curr_waypoint_index, the prev-waypoint history,
	// route-tracking timers, and any lingering goal or pause state.
	Vector vecSpot = pSpot->v.origin;
	if (pBot->v_coldspot_last_origin != g_vecZero &&
		(pBot->v_coldspot_last_origin - vecSpot).Length() > 32.0f)
	{
		pBot->waypoint_goal        = -1;
		pBot->old_waypoint_goal    = -1;
		pBot->f_waypoint_goal_time = 0.0f;
		pBot->curr_waypoint_index  = -1;
		pBot->f_waypoint_time      = 0.0f;
		pBot->prev_waypoint_distance = 0.0f;
		for (int p = 0; p < 5; p++)
			pBot->prev_waypoint_index[p] = -1;
		pBot->f_pause_time         = 0.0f;
		pBot->wpt_goal_type        = WPT_GOAL_NONE;
	}
	pBot->v_coldspot_last_origin = vecSpot;

	float botDist = (pEdict->v.origin - vecSpot).Length();
	bool  bInZone = (botDist < CSPOT_ZONE_RADIUS);
	if (bInZone)
		pBot->f_coldspot_last_in_zone = gpGlobals->time;

	// Role evaluation — throttled to ~0.75s.
	if (pBot->f_coldspot_role_eval_time < gpGlobals->time)
	{
		pBot->f_coldspot_role_eval_time = gpGlobals->time + 0.75f;

		int  enemiesInZone = 0;
		int  alliesInZone  = 0;

		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			edict_t *pPlayer = INDEXENT(i);
			if (!pPlayer || pPlayer->free || !(pPlayer->v.flags & FL_CLIENT))
				continue;
			if (pPlayer == pEdict)
				continue;
			if (!IsAlive(pPlayer) || pPlayer->v.iuser1)
				continue;
			if ((pPlayer->v.origin - vecSpot).Length() > CSPOT_ZONE_RADIUS)
				continue;
			if (UTIL_GetTeam(pPlayer) == botTeam)
				alliesInZone++;
			else
				enemiesInZone++;
		}

		if (bInZone && enemiesInZone > 0)
			pBot->i_coldspot_role = CSPOT_ROLE_HOLDER;       // kill intruders, stay
		else if (bInZone)
			pBot->i_coldspot_role = CSPOT_ROLE_HOLDER;       // keep scoring
		else if (!bInZone && enemiesInZone > 0 && alliesInZone == 0)
			pBot->i_coldspot_role = CSPOT_ROLE_HUNTER;       // rush in to clear
		else if (!bInZone && alliesInZone > 0 && enemiesInZone == 0)
			pBot->i_coldspot_role = CSPOT_ROLE_DEFENDER;     // cover perimeter
		else
			pBot->i_coldspot_role = CSPOT_ROLE_SEEKER;       // take the spot
	}

	// Pre-set v_goal so the movement block heads somewhere sane every frame,
	// even on ticks where combat runs instead of BotColdSpotThink.  The
	// target and proximity must match the evaluated role so that combat-tick
	// routing doesn't contradict BotColdSpotThink (e.g. a DEFENDER getting
	// direct-steered into the zone and causing a contest).
	switch (pBot->i_coldspot_role)
	{
	case CSPOT_ROLE_HOLDER:
		pBot->v_goal           = vecSpot;
		pBot->f_goal_proximity = CSPOT_HOLDER_RADIUS;
		break;

	case CSPOT_ROLE_DEFENDER:
	{
		// Perimeter point between the bot and the spot — same offset logic
		// used by BotColdSpotThink for DEFENDER so both paths agree.
		Vector vecToSpot = pEdict->v.origin - vecSpot;
		if (vecToSpot.Length() < 1.0f)
			pBot->v_goal = vecSpot;
		else
			pBot->v_goal = vecSpot + vecToSpot.Normalize() * CSPOT_DEFEND_RADIUS;
		pBot->f_goal_proximity = 64.0f;
		break;
	}

	case CSPOT_ROLE_HUNTER:
	case CSPOT_ROLE_SEEKER:
	default:
		pBot->v_goal           = vecSpot;
		pBot->f_goal_proximity = 0.0f;
		break;
	}

	// Advance the multi-jump sequence toward the spot even on combat ticks
	// (when BotColdSpotThink is skipped).  Without this, a bot engaged with
	// an enemy while standing directly below an elevated coldspot never
	// progresses through the double/triple-jump phases and can never reach
	// the scoring zone.  DEFENDER intentionally stays on the perimeter so
	// don't try to jump up to the spot for that role.
	if (pBot->i_coldspot_role != CSPOT_ROLE_DEFENDER)
		BotGoalElevatedJump(pBot, vecSpot);

	// Suppress random item detours — the spot is the objective.
	pBot->pBotPickupItem = NULL;
	pBot->item_waypoint  = -1;
}

//=========================================================
// BotColdSpotThink — called from BotThink when in Cold Spot
// mode and no combat is active.
//
// Role-based movement:
//  SEEKER   — no bot in zone → rush the spot
//  HUNTER   — enemy in zone → rush the spot to clear them
//  HOLDER   — bot is in zone → stay near center, face nearest threat
//  DEFENDER — ally scoring alone → patrol the perimeter
//
// Returns true when movement intent has been set; false to
// fall back to normal nav (e.g. if no spot exists yet).
//=========================================================
bool BotColdSpotThink( bot_t *pBot )
{
	if (is_gameplay != GAME_COLDSPOT)
		return false;

	edict_t *pSpot = BotColdSpotGetEntity();
	if (FNullEnt(pSpot))
		return false;

	edict_t *pEdict = pBot->pEdict;
	int botTeam = UTIL_GetTeam(pEdict);

	Vector vecSpot = pSpot->v.origin;
	float  botDist = (pEdict->v.origin - vecSpot).Length();

	pBot->f_pause_time = 0;

	switch (pBot->i_coldspot_role)
	{
	// =================================================================
	// HOLDER — stay inside the zone to score
	// =================================================================
	case CSPOT_ROLE_HOLDER:
	{
		// Anchor toward the spot center.  If far enough from center,
		// keep moving in; once inside holder radius, slow to a prowl so
		// the bot can acquire enemies without drifting out of LoS.
		if (botDist > CSPOT_HOLDER_RADIUS)
		{
			pBot->f_move_speed     = pBot->f_max_speed;
			pBot->v_goal           = vecSpot;
			pBot->f_goal_proximity = CSPOT_HOLDER_RADIUS * 0.5f;
		}
		else
		{
			pBot->f_move_speed     = pBot->f_max_speed * 0.35f;
			pBot->v_goal           = vecSpot;
			pBot->f_goal_proximity = CSPOT_HOLDER_RADIUS;
		}

		// Face the nearest zone intruder if any; otherwise face the
		// center so the bot keeps LoS to the scoring point.
		edict_t *pIntruder = BotColdSpotFindZoneIntruder(pSpot, botTeam);
		Vector   vecFace   = pIntruder ? pIntruder->v.origin : vecSpot;
		Vector   vecDir    = vecFace - pEdict->v.origin;
		if (vecDir.Length() > 1.0f)
		{
			Vector angles = UTIL_VecToAngles(vecDir);
			pEdict->v.ideal_yaw = angles.y;
			BotFixIdealYaw(pEdict);
		}

		BotGoalElevatedJump(pBot, vecSpot);
		return true;
	}

	// =================================================================
	// DEFENDER — patrol perimeter so ally in zone can keep scoring
	// =================================================================
	case CSPOT_ROLE_DEFENDER:
	{
		pBot->f_move_speed = pBot->f_max_speed;

		// Aim for a perimeter point between the bot and the spot so the
		// bot stays outside the scoring circle (avoids turning into a
		// contest) but within engagement range.
		Vector vecToSpot = pEdict->v.origin - vecSpot;
		if (vecToSpot.Length() < 1.0f)
		{
			pBot->v_goal = vecSpot;
		}
		else
		{
			vecToSpot = vecToSpot.Normalize();
			pBot->v_goal = vecSpot + vecToSpot * CSPOT_DEFEND_RADIUS;
		}
		pBot->f_goal_proximity = 64.0f;

		BotGoalElevatedJump(pBot, vecSpot);
		return true;
	}

	// =================================================================
	// HUNTER / SEEKER — rush the spot
	// =================================================================
	case CSPOT_ROLE_HUNTER:
	case CSPOT_ROLE_SEEKER:
	default:
	{
		pBot->f_move_speed     = pBot->f_max_speed;
		pBot->v_goal           = vecSpot;
		pBot->f_goal_proximity = 0.0f;

		BotGoalElevatedJump(pBot, vecSpot);
		return true;
	}
	}
}


edict_t *BotFindEnemy( bot_t *pBot )
{
//	ALERT(at_console, "BotFindEnemy\n");
	Vector vecEnd;
	static bool flag=TRUE;
	edict_t *pent = NULL;
	edict_t *pNewEnemy;
	edict_t *pRemember = NULL;
	float nearestdistance;
	int i;
	
	edict_t *pEdict = pBot->pEdict;

	if (is_gameplay == GAME_PROPHUNT)
	{
		if (pBot->f_pause_time >= gpGlobals->time)
		{
			// we're a prop, can't have an enemy
			pBot->pBotEnemy = NULL;
			return NULL;
		}
	}

	// KTS: while this bot has the ball its only job is to run to the goal —
	// no enemies, no combat.
	if (is_gameplay == GAME_KTS && pBot->b_kts_has_ball)
	{
		pBot->pBotEnemy = NULL;
		return NULL;
	}

	// Cold Skulls: when a skull is very close AND the bot is healthy
	// enough to be collecting (not seeking health), suppress enemy
	// targeting so the bot prioritises collection over fighting.
	// Uses BotFindBestSkull to verify a real skull entity exists nearby
	// rather than trusting v_goal which can be stale or set through walls.
	if (is_gameplay == GAME_COLDSKULL && pEdict->v.health > 25)
	{
		float flSkullDist = 0.0f;
		edict_t *pSkull = BotFindBestSkull(pEdict, &flSkullDist);
		if (pSkull && flSkullDist < 300.0f)
		{
			pBot->pBotEnemy = NULL;
			return NULL;
		}
	}

	// Priorize live grenade over everything else
	if (mod_id == VALVE_DLL && pBot->pBotEnemy && FStrEq(STRING(pBot->pBotEnemy->v.classname), "grenade")
		&& ((pBot->pBotEnemy->v.origin - pEdict->v.origin).Length() < 192)
		&& pBot->pBotEnemy->v.dmgtime > gpGlobals->time + 2.0)
		return pBot->pBotEnemy;

	if (pBot->pBotEnemy != NULL)  // does the bot already have an enemy?
	{
		vecEnd = UTIL_GetOrigin(pBot->pBotEnemy) + pBot->pBotEnemy->v.view_ofs;

		// if the enemy is dead?
		if (!IsAlive(pBot->pBotEnemy))  // is the enemy dead?, assume bot killed it
		{
			// the enemy is dead, jump for joy about 10% of the time
			//if (RANDOM_LONG(1, 100) <= 10)
			//	pEdict->v.button |= IN_JUMP;
			
			// don't have an enemy anymore so null out the pointer...
			pBot->pBotEnemy = NULL;
		}
		else if (pBot->pBotEnemy->v.flags & FL_GODMODE)
		{
			pBot->pBotEnemy = NULL;
		// Cannot see transparent player (with rune)
		}
		else if (is_gameplay == GAME_CTC)
		{
			// Void enemy if they dropped the chumtoad
			if (pBot->pEdict->v.health > 25 && pBot->pBotEnemy->v.fuser4 == 0)
			{
				pBot->pBotEnemy = pRemember = NULL;
			}

			// Void enemy if bot has good health and picked up chumtoad
			if (pBot->pEdict->v.health > 25 && pBot->current_weapon.iId == VALVE_WEAPON_CHUMTOAD)
			{
				pBot->pBotEnemy = pRemember = NULL;
			}

			if (pBot->pEdict->v.health <= 25 && pBot->current_weapon.iId != VALVE_WEAPON_CHUMTOAD && pBot->pBotEnemy->v.fuser4 == 0)
			{
				pBot->pBotEnemy = pRemember = NULL;
			}
		}
		else if (is_gameplay == GAME_KTS)
		{
			// In KTS, only keep an enemy while they are actively dribbling.
			// Use pev->euser1 (set in CaptureCharm) — zero-flicker authoritative check.
			edict_t *pBallKts = UTIL_FindEntityByClassname((edict_t *)NULL, "kts_snowball");
			bool stillDribbler = !FNullEnt(pBallKts)
				&& pBallKts->v.movetype == MOVETYPE_NOCLIP
				&& pBallKts->v.euser1 == pBot->pBotEnemy;
			if (!stillDribbler)
				pBot->pBotEnemy = pRemember = NULL;
		}
		else if (pBot->pBotEnemy->v.rendermode == kRenderTransAlpha &&
				 pBot->pBotEnemy->v.renderamt < 60 &&
				 (pBot->pBotEnemy->v.origin - pEdict->v.origin).Length() > 192) {
			pBot->pBotEnemy = NULL;
		}
		else if (FInViewCone( &vecEnd, pEdict ) &&
			FVisible( vecEnd, pEdict ))
		{
			{
				// if enemy is still visible and in field of view, keep it
				// keep track of when we last saw an enemy
				pBot->f_bot_see_enemy_time = gpGlobals->time;
				pBot->f_last_enemy_los_time = gpGlobals->time;
				// remember our current enemy and check for a new one
				pRemember = pBot->pBotEnemy;
			}
		}
		else if ((!FInViewCone( &vecEnd, pEdict ) ||
			!FVisible( vecEnd, pEdict )) && (!pBot->b_engaging_enemy || is_gameplay == GAME_CTF || is_gameplay == GAME_ARENA))
		{	// remember our enemy for 2 seconds even if they're not visible
			// Arena: extend the remember window to 5 seconds for the sole opponent
			float rememberWindow = (is_gameplay == GAME_ARENA) ? 5.0f : 2.0f;
			if (pBot->f_bot_see_enemy_time > (gpGlobals->time - rememberWindow))
				pRemember = pBot->pBotEnemy;
			pBot->pBotEnemy = NULL;
			pBot->f_ignore_wpt_time = 0.0;
		}
	}
	
	pent = NULL;
	pNewEnemy = NULL;
	nearestdistance = 1000;
	
	if (pNewEnemy == NULL)
	{
		edict_t *pMonster = NULL;
		Vector vecEnd;
			
		nearestdistance = 2500;
		
		if (pRemember)
			nearestdistance = (UTIL_GetOrigin(pRemember) - UTIL_GetOrigin(pEdict)).Length();

		// search the world for monsters...
		while (!FNullEnt(pMonster = UTIL_FindEntityInSphere(pMonster, pEdict->v.origin, nearestdistance)))
		{
			// ignore our remembered enemy
			if ((pRemember != NULL) && (pMonster == pRemember))
				continue;

			// not a player are they?
			if (pMonster->v.flags & FL_CLIENT)
				continue;

			// don't attack hornets
			if (mod_id != SI_DLL && FStrEq(STRING(pMonster->v.classname), "hornet"))
				continue;

			if (is_gameplay == GAME_CTC)
			{
				// don't target golden chumtoad
				if (FStrEq(STRING(pMonster->v.classname), "monster_ctctoad"))
					continue;
			}

			if (!FStrEq(STRING(pMonster->v.classname), "func_tech_breakable"))
			{
				if (!(pMonster->v.flags & FL_MONSTER))
					continue; // discard anything that is not a monster
				
				if (!IsAlive(pMonster))
					continue; // discard dead or dying monsters
				
				if ((pMonster->v.rendermode == kRenderTransTexture &&
					pMonster->v.renderamt < renderamt_threshold[pBot->bot_skill]) ||
					pMonster->v.effects & EF_NODRAW)
					continue;
			}
			// skip scientists in S&I if we already are carrying an item and don't have the mindray
			if (mod_id == SI_DLL && FStrEq(STRING(pMonster->v.classname), "monster_scientist") &&
				(pBot->i_carry_type > CARRY_NONE && !(pBot->pEdict->v.weapons & (1<<SI_WEAPON_MINDRAY))))
				continue;

			// is team play enabled?
			if (is_team_play > 0.0)
			{
				int player_team = UTIL_GetTeam(pMonster);
				int bot_team = UTIL_GetTeam(pEdict);
					
				// don't target your teammates as long as they're not a scientist and we don't have the mindray
				if (mod_id == SI_DLL && bot_team == player_team &&
					(!FStrEq(STRING(pMonster->v.classname), "monster_scientist") ||
					!(pBot->pEdict->v.weapons & (1<<SI_WEAPON_MINDRAY))))
					continue;
			}

			if (is_gameplay == GAME_CTC)
			{
				// Bot has good health and chumtoad, skip
				if (pBot->pEdict->v.health > 25 && pBot->pEdict->v.fuser4 > 0)
					continue;

				// Bot doesnt have chumtoad and monster doesnt have chumtoad, skip
				if (pBot->pEdict->v.fuser4 < 1)
					if (pMonster->v.fuser4 < 1)
						continue;
			}

			vecEnd = UTIL_GetOrigin(pMonster) + pMonster->v.view_ofs;
			
			// see if bot can't see the monster...
			if (!FInViewCone( &vecEnd, pEdict ) ||
				!FVisible( vecEnd, pEdict ))
				continue;
			
			float distance = (vecEnd - pEdict->v.origin).Length();

			if (mod_id == SI_DLL)
			{	// only notice scis if they're close and we don't have the mindray
				if (FStrEq(STRING(pMonster->v.classname), "monster_scientist") &&
					((pBot->i_carry_type == CARRY_NONE && distance > 128) ||
					(pBot->i_carry_type >= CARRY_SCI && !(pBot->pEdict->v.weapons & (1<<SI_WEAPON_MINDRAY)))))
					continue; 
			}

			if (distance < nearestdistance)
			{
				nearestdistance = distance;
				pNewEnemy = pMonster;
				
				pBot->pBotUser = NULL;  // don't follow user when enemy found
			}
		}

		// search the world for players...
		for (i = 1; i <= gpGlobals->maxClients; i++)
		{
			edict_t *pPlayer = INDEXENT(i);
			
			// skip invalid players and skip self (i.e. this bot)
			if ((pPlayer) && (!pPlayer->free) && (pPlayer != pEdict) && (pPlayer->v.flags & FL_CLIENT))
			{
				// ignore our remembered enemy
				if ((pRemember != NULL) && (pPlayer == pRemember))
					continue;

				// skip this player if not valid
				if (pPlayer->free)
					continue;

				// skip this player if not alive (i.e. dead or dying)
				if (!IsAlive(pPlayer))
					continue;

				if (pPlayer->v.deadflag == DEAD_FAKING)
				{
					// Allow feign detection a range
					if ((pPlayer->v.origin - pEdict->v.origin).Length() > 128)
						continue;
				}

				if ((b_observer_mode) && !(pPlayer->v.flags & FL_FAKECLIENT))
					continue;
				// can we see them?
				if ((pPlayer->v.rendermode == kRenderTransTexture &&
					pPlayer->v.renderamt < renderamt_threshold[pBot->bot_skill]) ||
					pPlayer->v.effects & EF_NODRAW)
					continue;
				
				// can we hurt them? (stops bots from attacking kicked player's left over edict)
				if (pPlayer->v.takedamage == DAMAGE_NO)
					continue;

				if (!checked_teamplay)  // check for team play...
					BotCheckTeamplay();
				
				// is team play enabled?
				if (is_team_play > 0.0)
				{
					int player_team = UTIL_GetTeam(pPlayer);
					int bot_team = UTIL_GetTeam(pEdict);
					
					// don't target your teammates...
					if (bot_team == player_team)
						continue;
				}

				if (is_gameplay == GAME_CTC)
				{
					// Bot has good health and chumtoad, skip
					if (pBot->pEdict->v.health > 25 && pBot->pEdict->v.fuser4 > 0)
						continue;

					// Bot doesnt have chumtoad and monster doesnt have chumtoad, skip
					if (pBot->pEdict->v.fuser4 < 1)
						if (pPlayer->v.fuser4 < 1)
							continue;
				}

				// KTS: only target the authoritative dribbler (pev->euser1).
				// If the ball is loose or this player is not the owner, skip.
				if (is_gameplay == GAME_KTS)
				{
					edict_t *pBallKts = BotGetKtsSnowballCached();
					if (!FNullEnt(pBallKts)
						&& pBallKts->v.movetype == MOVETYPE_NOCLIP
						&& pBallKts->v.euser1 == pPlayer)
					{
						pNewEnemy = pPlayer;
						pBot->pBotUser = NULL;
						break;
					}
					continue;  // skip non-dribblers entirely
				}

				vecEnd = pPlayer->v.origin + pPlayer->v.view_ofs;
				
				// see if bot can see the player...
				if (FInViewCone( &vecEnd, pEdict ) &&
					FVisible( vecEnd, pEdict ))
				{
					float distance = (pPlayer->v.origin - pEdict->v.origin).Length();

					// Cold Spot: prefer enemies inside or near the scoring zone
					// over random frag targets by shrinking their effective
					// selection distance.  Large bonus for in-zone intruders,
					// smaller bonus for perimeter threats.
					if (is_gameplay == GAME_COLDSPOT)
					{
						edict_t *pSpot = BotColdSpotGetEntity();
						if (!FNullEnt(pSpot))
						{
							float spotDist = (pPlayer->v.origin - pSpot->v.origin).Length();
							if (spotDist < CSPOT_ZONE_RADIUS)
								distance -= 1500.0f;
							else if (spotDist < CSPOT_NEARBY_RADIUS)
								distance -= 500.0f;
						}
					}

					if (distance < nearestdistance)
					{
						nearestdistance = distance;
						pNewEnemy = pPlayer;
						
						pBot->pBotUser = NULL;  // don't follow user when enemy found
					}
				}
			}
		}
	}
	// couldn't find a new enemy so remember the old one we can't see
	if (pNewEnemy == NULL && pRemember != NULL)
		pNewEnemy = pRemember;
	// are we engaging an enemy?  Don't forget about them
	// In CTF / Arena / Cold Spot, let the enemy go so the bot returns to objective play.
	if (pNewEnemy == NULL && pBot->b_engaging_enemy && pBot->pBotEnemy != NULL
		&& is_gameplay != GAME_CTF && is_gameplay != GAME_ARENA && is_gameplay != GAME_COLDSPOT)
		pNewEnemy = pBot->pBotEnemy;

	if (pNewEnemy)
	{
		// face the enemy
		Vector v_enemy = pNewEnemy->v.origin - pEdict->v.origin;
		Vector bot_angles = UTIL_VecToAngles( v_enemy );
		/*	Let shoot at enemy handle this!
		pEdict->v.ideal_yaw = bot_angles.y;
		
		BotFixIdealYaw(pEdict);
		*/
		// we have a reaction time AND our new enemy is not the current enemy AND we're not remembering
		// an old enemy
		if ((bot_reaction_time > 0) && (pNewEnemy != pBot->pBotEnemy) && (!pRemember))
		{
			float react_delay;
			float delay_min = react_time_min[pBot->bot_skill] * bot_reaction_time;
			float delay_max = react_time_max[pBot->bot_skill] * bot_reaction_time;

			float distance_delay = log10(v_enemy.Length()) * 0.8;
			// don't get an advantage if they're too close
			if (distance_delay < 1.0) distance_delay = 1.0;

			react_delay = RANDOM_FLOAT(delay_min, delay_max) * distance_delay;
			
			pBot->f_reaction_target_time = gpGlobals->time + react_delay;

			//SERVER_PRINT( "%s reacting in %f seconds!\n", STRING(pEdict->v.netname), react_delay);
		}
		// Re-acquire penalty: when the bot picks this enemy back up after
		// line-of-sight was broken >= 0.5s ago, apply a shorter hesitation
		// and bump the aim error to the per-skill max.  This removes the
		// "corner-snap" where a bot that saw you 1.9s ago insta-lasers you
		// the moment you reappear.  Arena (1v1) mode is exempt so duels
		// stay snappy.
		else if ((bot_reaction_time > 0) && (pNewEnemy == pBot->pBotEnemy) &&
			(is_gameplay != GAME_ARENA) &&
			(pBot->f_last_enemy_los_time > 0.0f) &&
			((gpGlobals->time - pBot->f_last_enemy_los_time) >= 0.5f))
		{
			float difficulty = bot_aim_difficulty;
			if (difficulty < 0.0f) difficulty = 0.0f;
			if (difficulty > 2.0f) difficulty = 2.0f;

			float delay_min = react_time_min[pBot->bot_skill] * bot_reaction_time * 0.6f;
			float delay_max = react_time_max[pBot->bot_skill] * bot_reaction_time * 0.6f;
			float react_delay = RANDOM_FLOAT(delay_min, delay_max) * difficulty;

			if (gpGlobals->time + react_delay > pBot->f_reaction_target_time)
				pBot->f_reaction_target_time = gpGlobals->time + react_delay;

			// Snap aim off-target so the first shot after re-acquire misses.
			float xscale = aim_tracking_x_scale[pBot->bot_skill] * difficulty;
			float yscale = aim_tracking_y_scale[pBot->bot_skill] * difficulty;
			pBot->f_aim_x_angle_delta = (RANDOM_LONG(0, 1) ? xscale : -xscale);
			pBot->f_aim_y_angle_delta = (RANDOM_LONG(0, 1) ? yscale : -yscale);
			pBot->f_aim_tracking_time = gpGlobals->time + RANDOM_FLOAT(0.2f, 0.5f);

			// Reset burst counter so the bot does a short burst-then-pause
			// rather than dumping a full magazine on re-appearance.
			pBot->i_burst_count = 0;
		}
		// get our origin
		vecEnd = UTIL_GetOrigin(pNewEnemy) + pNewEnemy->v.view_ofs;

		// keep track of when we last saw an enemy
		if (FInViewCone( &vecEnd, pEdict ) &&
			FVisible( vecEnd, pEdict ))
		{
			pBot->f_bot_see_enemy_time = gpGlobals->time;
			pBot->f_last_enemy_los_time = gpGlobals->time;
		}
	}
	
	// has the bot NOT seen an ememy for at least 5 seconds (time to reload)?
	if ((pBot->f_bot_see_enemy_time > 0) &&
		((pBot->f_bot_see_enemy_time + 5.0) <= gpGlobals->time))
	{
		pBot->f_bot_see_enemy_time = -1;  // so we won't keep reloading
		
		pEdict->v.button |= IN_RELOAD;  // press reload button
	}
	
	return (pNewEnemy);
}

int BotGetEnemyWeapon( edict_t *pEnemy )
{
//	ALERT(at_console, "BotGetEnemyWeapon\n");

	bot_weapon_select_t *pSelect = NULL;
	pSelect = WeaponGetSelectPointer();

	if ((pEnemy->v.flags & FL_CLIENT) && (pSelect != NULL))
	{
		int select_index = 0;

		while (pSelect[select_index].iId)
		{	// does our enemy weapon model match this weapon's model?
			// compare!
			if (strstr(strcmp(STRING(pEnemy->v.weaponmodel), "0") != 0 ? STRING(pEnemy->v.weaponmodel) : "", pSelect[select_index].weapon_model) != NULL)
				break;

			select_index++;
		}

		return pSelect[select_index].iId;
	}

	return VALVE_WEAPON_UNKNOWN;
}

bool BotShouldEngageEnemy( bot_t *pBot, edict_t *pEnemy )
{	// this function might need some tweaking?
//	ALERT(at_console, "BotShouldEngageEnemy\n");
	
	bot_weapon_select_t *pSelect = NULL;
	pSelect = WeaponGetSelectPointer();

	// must have enemy and the enemy must be a client
	if ((pSelect == NULL) || (pEnemy == NULL) || 
		(strcmp(STRING(pEnemy->v.classname), "player") != 0))
		return FALSE;

	// never engage the enemy if we have a sci/rsrc
	if (mod_id == SI_DLL && pBot->i_carry_type)
		return FALSE;

	if (is_gameplay == GAME_CTC)
	{
		// Don't engage if I have good health and the chumtoad
		if (pBot->pEdict->v.health > 25 && pBot->pEdict->v.fuser4 > 0)
		{
			return FALSE;
		}
	}

	int our_weapon = WeaponGetSelectIndex(pBot->current_weapon.iId);
	int enemy_weapon = WeaponGetSelectIndex(BotGetEnemyWeapon(pEnemy));

	float primary_ammo = BotAssessPrimaryAmmo(pBot, pBot->current_weapon.iId);
	float secondary_ammo = BotAssessSecondaryAmmo(pBot, pBot->current_weapon.iId);
	// don't engage if our ammo is low!
	if (((primary_ammo == AMMO_CRITICAL) && (secondary_ammo == AMMO_CRITICAL)) ||
		((primary_ammo == AMMO_CRITICAL) && (secondary_ammo == AMMO_NONE)) ||
		((primary_ammo == AMMO_NONE) && (secondary_ammo == AMMO_CRITICAL)))
		return FALSE;

	for (int i = 0; i < 4; i++)
	{
		// is our weapon not good compared to their weapon? AND
		// our health is less than their health plus armor
		/*if ((pSelect[our_weapon].priority > (pSelect[enemy_weapon].priority + i)) &&
			((pBot->pEdict->v.health + pBot->pEdict->v.armorvalue) < (pEnemy->v.health +
			pEnemy->v.armorvalue + (25 * i))))
		{
			if (b_chat_debug)
			{
				sprintf(pBot->debugchat, "I won't engage %s! i = %i (%s - %i > %s - %i) (%.0f < %.0f)\n",
					STRING(pEnemy->v.netname), i, 
					pSelect[our_weapon].weapon_name, pSelect[our_weapon].priority,
					pSelect[enemy_weapon].weapon_name, pSelect[enemy_weapon].priority,
					pBot->pEdict->v.health + pBot->pEdict->v.armorvalue,
					pEnemy->v.health + pEnemy->v.armorvalue + (25 * i));
				UTIL_HostSay(pBot->pEdict, 0, pBot->debugchat);
			}
			return FALSE;
		}*/
	}

	return TRUE;
}

Vector BotBodyTarget( edict_t *pBotEnemy, bot_t *pBot )
{
//	ALERT(at_console, "BotBodyTarget\n");

	if (!pBotEnemy)
		return g_vecZero;

	// get our origin for world brush entities
	if (strncmp(STRING(pBotEnemy->v.classname), "func_", 5) == 0)
		return VecBModelOrigin(pBotEnemy);

	if (strcmp("monster_headcrab", STRING(pBotEnemy->v.classname)) == 0)
		return (pBotEnemy->v.origin + Vector(0,0,8));

	// bots of skill 3 and higher aim at the torso
	if ((pBot->bot_skill + 1) >= 3)
	{	// monsters need special case, origin is at feet, so add half the view offset
		if (strncmp("monster_", STRING(pBotEnemy->v.classname), 5) == 0)
			return (pBotEnemy->v.origin + (pBotEnemy->v.view_ofs / 2));

		return (pBotEnemy->v.origin);
	}

	// aim for the torso with the mindray against scis
	if (mod_id == SI_DLL && FStrEq(STRING(pBotEnemy->v.classname), "monster_scientist") &&
		pBot->current_weapon.iId == SI_WEAPON_MINDRAY)
		return (pBotEnemy->v.origin + (pBotEnemy->v.view_ofs / 2));

	return (pBotEnemy->v.origin + pBotEnemy->v.view_ofs);
}

// specifing a weapon_choice allows you to choose the weapon the bot will
// use (assuming enough ammo exists for that weapon)
// BotFireWeapon will return TRUE if weapon was fired, FALSE otherwise
extern cvar_t sv_botsmelee;

bool BotFireWeapon(Vector v_enemy, bot_t *pBot, int weapon_choice, bool nofire)
{
//ALERT(at_console, "BotFireWeapon\n");
	bot_weapon_select_t *pSelect = NULL;
	bot_fire_delay_t *pDelay = NULL;
	int select_index;
	int iId;
	int primary_percent;
	
	edict_t *pEdict = pBot->pEdict;
	
	float distance = v_enemy.Length();  // how far away is the enemy?
	
	pSelect = WeaponGetSelectPointer();
	pDelay = WeaponGetDelayPointer();
	
	bool use_primary[MAX_WEAPONS];
	bool use_secondary[MAX_WEAPONS];

	// Don't fire weapon if frozen
	if (pBot->pEdict->v.flags & FL_FROZEN) {
		return FALSE;
	}

	if (is_gameplay == GAME_CTC)
	{
		// Don't fire if I have good health and the chumtoad
		if (pBot->pEdict->v.health > 25 && pBot->current_weapon.iId == VALVE_WEAPON_CHUMTOAD)
			return FALSE;
	}

	// Kick or punch this grenade!
	if (pBot->b_hasgrenade) {
		// ALERT(at_aiconsole, "Kick or punch time!");
		pEdict->v.impulse = 206 + RANDOM_LONG(0, 1);
		pBot->b_hasgrenade = FALSE;
	}

	if (pSelect)
	{
		// are we charging the primary fire?
		if (pBot->f_primary_charging > 0)
		{
			iId = pBot->charging_weapon_id;
			
			// is it time to fire the charged weapon?
			if (pBot->f_primary_charging <= gpGlobals->time)
			{
				// we DON'T set pEdict->v.button here to release the
				// fire button which will fire the charged weapon
				
				pBot->f_primary_charging = -1;  // -1 means not charging
				
				// find the correct fire delay for this weapon
				select_index = 0;
				
				while ((pSelect[select_index].iId) &&
					(pSelect[select_index].iId != iId))
					select_index++;
				
				// set next time to shoot
				int skill = pBot->bot_skill;
				float base_delay, min_delay, max_delay;
				
				base_delay = pDelay[select_index].primary_base_delay;
				min_delay = pDelay[select_index].primary_min_delay[skill];
				max_delay = pDelay[select_index].primary_max_delay[skill];
				
				pBot->f_shoot_time = gpGlobals->time + base_delay +
					RANDOM_FLOAT(min_delay, max_delay);
				
				return TRUE;
			}
			else
			{
				pEdict->v.button |= IN_ATTACK;   // charge the weapon
				pBot->f_shoot_time = gpGlobals->time;  // keep charging
				
				return TRUE;
			}
		}
		
		// are we charging the secondary fire?
		if (pBot->f_secondary_charging > 0)
		{
			iId = pBot->charging_weapon_id;
			
			// is it time to fire the charged weapon?
			if (pBot->f_secondary_charging <= gpGlobals->time)
			{
				// we DON'T set pEdict->v.button here to release the
				// fire button which will fire the charged weapon
				
				pBot->f_secondary_charging = -1;  // -1 means not charging
				
				// find the correct fire delay for this weapon
				select_index = 0;
				
				while ((pSelect[select_index].iId) &&
					(pSelect[select_index].iId != iId))
					select_index++;
				
				// set next time to shoot
				int skill = pBot->bot_skill;
				float base_delay, min_delay, max_delay;
				
				base_delay = pDelay[select_index].secondary_base_delay;
				min_delay = pDelay[select_index].secondary_min_delay[skill];
				max_delay = pDelay[select_index].secondary_max_delay[skill];
				
				pBot->f_shoot_time = gpGlobals->time + base_delay +
					RANDOM_FLOAT(min_delay, max_delay);
				
				return TRUE;
			}
			else
			{
				pEdict->v.button |= IN_ATTACK2;  // charge the weapon
				pBot->f_shoot_time = gpGlobals->time;  // keep charging
				
				return TRUE;
			}
		}
		
		select_index = 0;
		int best_priority = MAX_WEAPONS;
		int final_index = 0;
/*
		while (pSelect[select_index].iId)
		{
			// is the bot NOT carrying this weapon?
			if (!(pEdict->v.weapons & (1<<pSelect[select_index].iId)))
			{
				//ALERT( at_console, "Skipping %s, don't have it\n", pSelect[select_index].weapon_name);
				select_index++;  // skip to next weapon
				continue;
			}
			
			if (pSelect[select_index].iId == pBot->current_weapon.iId)
			{
				select_index++;  // skip to next weapon
				continue;
			}
			
			final_index = select_index;
			break;
			//select_index++;
		}*/
	
		// loop through all the weapons until terminator is found...
		while (pSelect[select_index].iId)
		{
			// was a weapon choice specified? (and if so do they NOT match?)
			if (weapon_choice != 0 &&
				weapon_choice != pSelect[select_index].iId)
			{
				//ALERT( at_console, "Skipping %s, not our choice\n", pSelect[select_index].weapon_name);
				select_index++;  // skip to next weapon
				continue;
			}

			// is the bot NOT carrying this weapon?
			if (!UTIL_HasWeaponId(pEdict, pSelect[select_index].iId))
			{
				//ALERT( at_console, "Skipping %s, don't have it\n", pSelect[select_index].weapon_name);
				select_index++;  // skip to next weapon
				continue;
			}
			
			// is the bot NOT skilled enough to use this weapon?
			if ((pBot->bot_skill+1) > pSelect[select_index].skill_level &&
				weapon_choice == 0)
			{
				//ALERT( at_console, "Skipping %s, not skilled enough\n", pSelect[select_index].weapon_name);
				select_index++;  // skip to next weapon
				continue;
			}
			
			// is the bot underwater and does this weapon NOT work under water?
			if ((pEdict->v.waterlevel == 3) &&
				!(pSelect[select_index].can_use_underwater))
			{
				//ALERT( at_console, "Skipping %s, doesn't work underwater\n", pSelect[select_index].weapon_name);
				select_index++;  // skip to next weapon
				continue;
			}
			
			if (mod_id == SI_DLL && pSelect[select_index].iId == SI_WEAPON_MINDRAY && (!pBot->pBotEnemy ||
				!FStrEq(STRING(pBot->pBotEnemy->v.classname), "monster_scientist") ||
				(pBot->pBotEnemy && UTIL_GetTeam(pBot->pBotEnemy) != pBot->bot_team &&
				pBot->i_carry_type == CARRY_NONE)))
			{
				//ALERT( at_console, "Skipping %s, shouldn't mindray\n", pSelect[select_index].weapon_name);
				select_index++;
				continue;
			}
			
			// forget about the GI Distabilizer if they're already vomiting
			if (mod_id == SI_DLL && pSelect[select_index].iId == SI_WEAPON_VOMIT && pBot->pBotEnemy &&
				(g_flVomiting[ENTINDEX(pBot->pBotEnemy)-1] + 0.5) > gpGlobals->time)
			{
				//ALERT( at_console, "Skipping %s, already vomiting\n", pSelect[select_index].weapon_name);
				select_index++;
				continue;
			}

			// forget about the EMP Cannon if they don't have any armor
			if (mod_id == SI_DLL && pSelect[select_index].iId == SI_WEAPON_EMPCANNON && pBot->pBotEnemy &&
				pBot->pBotEnemy->v.armorvalue <= 0)
			{
				//ALERT( at_console, "Skipping %s, target has no armor\n", pSelect[select_index].weapon_name);
				select_index++;
				continue;
			}
			
			// is this weapon worse than our previous choice?
			if (((pSelect[select_index].priority >= best_priority) ||
				(pSelect[select_index].priority < 0)) && (best_priority != VALVE_WEAPON_FISTS ||
				best_priority != SI_WEAPON_BRIEFCASE))
			{
				//ALERT( at_console, "Skipping %s, priority too low\n", pSelect[select_index].weapon_name);
				select_index++;  // skip to next weapon
				continue;
			}

			iId = pSelect[select_index].iId;
			use_primary[select_index] = FALSE;
			use_secondary[select_index] = FALSE;
			primary_percent = RANDOM_LONG(1, 100);
			
			float primary_assess = BotAssessPrimaryAmmo(pBot, pSelect[select_index].iId);
			float secondary_assess = BotAssessSecondaryAmmo(pBot, pSelect[select_index].iId);
			// see if there is enough secondary ammo AND
			// the bot is far enough away to use secondary fire AND
			// the bot is close enough to the enemy to use secondary fire
			if (pSelect[select_index].primary_fire_percent < 100 &&
				primary_percent > pSelect[select_index].primary_fire_percent &&
				(secondary_assess != AMMO_CRITICAL ||
				(pBot->current_weapon.iId == iId &&
				pBot->current_weapon.iClip2 >= pSelect[select_index].min_secondary_ammo) ||
				(pBot->current_weapon.iId == iId &&
				pBot->current_weapon.iClip2 < pSelect[select_index].min_secondary_ammo &&
				pBot->current_weapon.iClip2 != -1 && secondary_assess != AMMO_CRITICAL)) &&
				distance >= pSelect[select_index].secondary_min_distance &&
				distance <= pSelect[select_index].secondary_max_distance)
			{
				use_secondary[select_index] = TRUE;
			}
			
			// NOTE: The EMP Cannon for Science and Industry has a wacky way of ammo handling
			// and does NOT use this if block to determine if it can be fired.
			//
			// is primary percent less than weapon primary percent AND
			// no ammo required for this weapon OR
			// enough ammo available to fire OR
			// we're currently on this weapon and it has a clip OR
			// we're currently on this weapon, clip ran out, but we still have ammo reserve AND
			// the bot is far enough away to use primary fire AND
			// the bot is close enough to the enemy to use primary fire
			if (!use_secondary[select_index] && ((mod_id != SI_DLL) ||
				(mod_id == SI_DLL && iId != SI_WEAPON_EMPCANNON)) &&
				(primary_assess != AMMO_CRITICAL || 
				(pBot->current_weapon.iId == iId &&
				pBot->current_weapon.iClip >= pSelect[select_index].min_primary_ammo) ||
				(pBot->current_weapon.iId == iId &&
				pBot->current_weapon.iClip < pSelect[select_index].min_primary_ammo &&
				pBot->current_weapon.iClip != -1 && primary_assess != AMMO_CRITICAL)) &&
				distance >= pSelect[select_index].primary_min_distance &&
				distance <= pSelect[select_index].primary_max_distance)
			{
				use_primary[select_index] = TRUE;
			}
			
			// EMP Cannon check
			// Our clip has ammo in it OR
			// we have a cannister left AND
			// the bot is far enough away to use primary fire AND
			// the bot is close enough to the enemy to use primary fire
			if (!use_secondary[select_index] && mod_id == SI_DLL && iId == SI_WEAPON_EMPCANNON &&
				((pBot->current_weapon.iId == iId &&
				pBot->current_weapon.iClip >= pSelect[select_index].min_primary_ammo) ||
				(pBot->m_rgAmmo[weapon_defs[iId].iAmmo2] > 0)) &&
				distance >= pSelect[select_index].primary_min_distance &&
				distance <= pSelect[select_index].primary_max_distance)
			{
				use_primary[select_index] = TRUE;
			}

			// see if there wasn't enough ammo to fire the weapon...
			if (use_primary[select_index] == FALSE && use_secondary[select_index] == FALSE)
			{
				//SERVER_PRINT( "Skipping %s, can't use primary or secondary\n", pSelect[select_index].weapon_name);
				select_index++;  // skip to next weapon
				continue;
			}

			final_index = select_index;
			best_priority = pSelect[select_index].priority;

			select_index++;
		}
	
//		ALERT( at_console, "Selected %s\n", pSelect[final_index].weapon_name);

		iId = pSelect[final_index].iId;

		// select this weapon if it isn't already selected
		if (is_gameplay != GAME_CTC && pBot->current_weapon.iId != iId/* && g_flWeaponSwitch <= gpGlobals->time*/)
		{
			//ALERT(at_console, "Switch weapon\n");
			//g_flWeaponSwitch = gpGlobals->time + 1.0;
			//ALERT( at_console, "Switching to %s\n", pSelect[final_index].weapon_name);
			UTIL_SelectItem(pEdict, pSelect[final_index].weapon_name);
			pBot->f_shoot_time = gpGlobals->time + 0.5;
			pBot->f_reload_time = 0;
			return FALSE;
		}
		
		if (nofire) // just select the weapon
			return FALSE;
			
		if (pDelay[final_index].iId != iId)
		{
			char msg[80];
			sprintf(msg, "fire_delay mismatch for weapon id=%d\n",iId);
			SERVER_PRINT(msg);
				
			return FALSE;
		}
			
		if (use_primary[final_index] && pBot->current_weapon.iId == iId && pBot->current_weapon.iClip != -1 &&
			pBot->current_weapon.iClip < pSelect[final_index].min_primary_ammo &&
			pBot->f_reload_time <= gpGlobals->time)
		{	// reload if our clip is running out
			pBot->f_reload_time = gpGlobals->time + pSelect[final_index].reload_delay;
			pEdict->v.button |= IN_RELOAD;
			return FALSE;
		}

		if (is_gameplay == GAME_CTC)
		{
			if (pBot->pEdict->v.health <= 25 && pBot->pEdict->v.fuser4 > 0)
				pBot->pBotEnemy = NULL;
		}

		// zoom in if crossbow
		if ((((mod_id == CRABBED_DLL || mod_id == VALVE_DLL) && iId == VALVE_WEAPON_CROSSBOW) ||
			(mod_id == SI_DLL && iId == SI_WEAPON_CROSSBOW)) &&
			(pEdict->v.fov == 0))
		{
			pEdict->v.button |= IN_ATTACK2;
			return TRUE;
		}

		if (use_primary[final_index])
		{
			// Fire discipline: automatic weapons (primary_fire_hold) at mid/long
			// range shoot in short bursts with a brief pause, instead of holding
			// the trigger.  Makes sustained-fire weapons feel human.
			bool burst_pausing = false;
			if (pSelect[final_index].primary_fire_hold &&
				!pSelect[final_index].primary_fire_charge &&
				distance > 350.0f)
			{
				if (pBot->f_burst_pause_until > gpGlobals->time)
				{
					// Currently in a burst pause — don't fire this frame.
					burst_pausing = true;
				}
				else if (pBot->i_burst_count >= RANDOM_LONG(3, 6))
				{
					// Burst complete — enter a short pause.
					float difficulty = bot_aim_difficulty;
					if (difficulty < 0.5f) difficulty = 0.5f;
					if (difficulty > 2.0f) difficulty = 2.0f;

					// Lower-skill bots pause a touch longer (more human).
					float skill_boost = 1.0f + (pBot->bot_skill * 0.1f);
					float pause = RANDOM_FLOAT(0.25f, 0.60f) * difficulty * skill_boost;

					pBot->f_burst_pause_until = gpGlobals->time + pause;
					pBot->i_burst_count = 0;
					pBot->f_shoot_time = pBot->f_burst_pause_until;
					burst_pausing = true;
				}
			}

			if (burst_pausing)
			{
				// Skip firing this frame; leave f_shoot_time set to the pause
				// deadline so BotShootAtEnemy's gate keeps the trigger up.
				return FALSE;
			}

			if (!UTIL_MutatorEnabled(MUTATOR_DONTSHOOT))
				pEdict->v.button |= IN_ATTACK;  // use primary attack
			else
				if (RANDOM_LONG(0,4) == 0)
					pEdict->v.button |= IN_ATTACK;
			// for dual uzies, we want to fire both guns at the same time
			if (mod_id == SI_DLL && iId == SI_WEAPON_SNUZI)
				pEdict->v.button |= IN_ATTACK2;


			if (sv_botsmelee.value > 0 && is_gameplay != GAME_GUNGAME)
			{
				// Scale melee-impulse frequency with bot_aim_difficulty so
				// softened bots don't become lethal the moment a player closes
				// in.  Linear: 0.0 -> 100% (unchanged), 1.0 -> ~55%, 2.0 -> 10%.
				float melee_difficulty = bot_aim_difficulty;
				if (melee_difficulty < 0.0f) melee_difficulty = 0.0f;
				if (melee_difficulty > 2.0f) melee_difficulty = 2.0f;
				float melee_chance = 1.0f - 0.45f * melee_difficulty;
				if (melee_chance < 0.10f) melee_chance = 0.10f;

				if (RANDOM_FLOAT(0.0f, 1.0f) <= melee_chance)
				{
				if (distance <= 80) {
					// ALERT(at_aiconsole, "Kick or punch time!");
					pEdict->v.impulse = 206 + RANDOM_LONG(0, 1);
				} else if (distance <= 120) {
					// ALERT(at_aiconsole, "Flip!");
					pEdict->v.impulse = 210 + RANDOM_LONG(0, 2);
				} else if (distance <= 250) {
					// ALERT(at_aiconsole, "Slide!");
					pEdict->v.impulse = RANDOM_LONG(0,1) ? 208 : 214;
				} else if (distance <= 450) {
					// ALERT(at_aiconsole, "Throw grenade!");
					pEdict->v.impulse = 209;
				} else {
					// 10%
					if (RANDOM_LONG(1,10) == 10) {
						// ALERT(at_aiconsole, "Force grab it!\n");
						if (RANDOM_LONG(0,1))
							pEdict->v.impulse = 215; // force grab
						else
							pEdict->v.impulse = 216; // drop explosive weapon
					}
				}
				}
			}

			if (pSelect[final_index].primary_fire_charge)
			{
				pBot->charging_weapon_id = iId;
					
				// release primary fire after the appropriate delay...
				pBot->f_primary_charging = gpGlobals->time +
					pSelect[final_index].primary_charge_delay;
					
				pBot->f_shoot_time = gpGlobals->time;  // keep charging
			}
			else
			{
				// set next time to shoot
				if (pSelect[final_index].primary_fire_hold)
				{
					pBot->f_shoot_time = gpGlobals->time;  // don't let button up
					// Count shots toward the burst-pause budget for autos at range.
					if (distance > 350.0f)
						pBot->i_burst_count++;
					else
						pBot->i_burst_count = 0;
				}
				else
				{
					int skill = pBot->bot_skill;
					float base_delay, min_delay, max_delay;
						
					base_delay = pDelay[final_index].primary_base_delay;
					min_delay = pDelay[final_index].primary_min_delay[skill];
					max_delay = pDelay[final_index].primary_max_delay[skill];
						
					pBot->f_shoot_time = gpGlobals->time + base_delay +
						RANDOM_FLOAT(min_delay, max_delay);
				}
			}
		}
		else if (use_secondary[final_index]) // MUST be use_secondary...
		{
			if (!UTIL_MutatorEnabled(MUTATOR_DONTSHOOT))
				pEdict->v.button |= IN_ATTACK2;  // use secondary attack
			else
				if (RANDOM_LONG(0,4) == 0)
					pEdict->v.button |= IN_ATTACK2;
			// for dual uzies, we want to fire both guns at the same time
			if (mod_id == SI_DLL && iId == SI_WEAPON_SNUZI)
				pEdict->v.button |= IN_ATTACK;
			
			if (pSelect[final_index].secondary_fire_charge)
			{
				pBot->charging_weapon_id = iId;
				
				// release secondary fire after the appropriate delay...
				pBot->f_secondary_charging = gpGlobals->time +
					pSelect[final_index].secondary_charge_delay;
					
				pBot->f_shoot_time = gpGlobals->time;  // keep charging
			}
			else
			{
				// set next time to shoot
				if (pSelect[final_index].secondary_fire_hold)
					pBot->f_shoot_time = gpGlobals->time;  // don't let button up
				else
				{
					int skill = pBot->bot_skill;
					float base_delay, min_delay, max_delay;
						
					base_delay = pDelay[final_index].secondary_base_delay;
					min_delay = pDelay[final_index].secondary_min_delay[skill];
					max_delay = pDelay[final_index].secondary_max_delay[skill];
						
					pBot->f_shoot_time = gpGlobals->time + base_delay +
						RANDOM_FLOAT(min_delay, max_delay);
				}
			}
			return TRUE;  // weapon was fired
		}
	
	}
   
	// didn't have any available weapons or ammo, return FALSE
	return FALSE;
}

Vector BotGetLead( bot_t *pBot, edict_t *pEntity, float flProjSpeed )
{
//	ALERT(at_console, "BotGetLead\n");

	if (!pEntity)
		return BotBodyTarget( pBot->pBotEnemy, pBot );
	// get our origin and distance to the entity
	Vector vecOrigin = BotBodyTarget( pBot->pBotEnemy, pBot );
	float flDistance = (vecOrigin - UTIL_GetOrigin(pBot->pEdict)).Length();
	// factor in the entity's velocity multiplied by the percent of distance out of our
	// weapon's projectile speed
	Vector vecNewOrigin = vecOrigin;
	// so we don't divide by 0
	if (flProjSpeed > 0)
	{
		vecNewOrigin.x += pEntity->v.velocity.x * (flDistance/flProjSpeed);
		vecNewOrigin.y += pEntity->v.velocity.y * (flDistance/flProjSpeed);
		vecNewOrigin.z += pEntity->v.velocity.z * (flDistance/flProjSpeed);
	}
	// factor in a small amount of the bot's current velocity
	vecNewOrigin.x += pBot->pEdict->v.velocity.x * -0.005;
	vecNewOrigin.y += pBot->pEdict->v.velocity.y * -0.005;
	vecNewOrigin.z += pBot->pEdict->v.velocity.z * -0.005;

	return vecNewOrigin;
}

void BotShootAtEnemy( bot_t *pBot )
{
//	ALERT(at_console, "BotShootAtEnemy\n");

	if (!pBot->pBotEnemy)
		return;

	int team = UTIL_GetTeam(pBot->pEdict);
	float f_distance;
	float f_velocity;
	TraceResult tr;
	edict_t *pEdict = pBot->pEdict;
	
	Vector v_enemy_origin = BotBodyTarget( pBot->pBotEnemy, pBot );
	Vector v_lead_origin = BotGetLead(pBot, pBot->pBotEnemy, WeaponProjectileSpeed(pBot->current_weapon.iId));
	// aim for the head and/or body
	Vector v_enemy = v_lead_origin - GetGunPosition(pEdict);

	Vector enemy_angle = UTIL_VecToAngles( v_enemy );
	
	if (enemy_angle.x > 180)
		enemy_angle.x -= 360;
	
	if (enemy_angle.y > 180)
		enemy_angle.y -= 360;
	
	// adjust the view angle pitch to aim correctly
	enemy_angle.x = -enemy_angle.x;
	
	float d_x, d_y;
	
	d_x = (enemy_angle.x - pEdict->v.v_angle.x);
	d_y = (enemy_angle.y - pEdict->v.v_angle.y);

	if (pBot->f_aim_tracking_time < gpGlobals->time)
	{
		// Shorter refresh window so aim doesn't "lock" for up to 3 seconds;
		// the old value let bots hold a perfect lead for too long.
		pBot->f_aim_tracking_time = gpGlobals->time + RANDOM_FLOAT(0.25f, 1.25f);

		float difficulty = bot_aim_difficulty;
		if (difficulty < 0.0f) difficulty = 0.0f;
		if (difficulty > 2.0f) difficulty = 2.0f;

		float xscale = aim_tracking_x_scale[pBot->bot_skill] * difficulty;
		float yscale = aim_tracking_y_scale[pBot->bot_skill] * difficulty;

		pBot->f_aim_x_angle_delta = RANDOM_FLOAT(-xscale, xscale);
		pBot->f_aim_y_angle_delta = RANDOM_FLOAT(-yscale, yscale);

//		SERVER_PRINT( "%s x delta is %.2f, y delta is %.2f\n", pBot->name,
//			pBot->f_aim_x_angle_delta, pBot->f_aim_y_angle_delta);
	}
	{	// All skill tiers get aim error applied — no more perfect-aim skill-0
		// bots.  Enemy velocity still amplifies tracking error so fast strafing
		// matters.  Continuous per-frame jitter is layered on top so aim is
		// never pixel-perfect between step refreshes.
		f_velocity = fmax(pBot->pBotEnemy->v.velocity.Length() * 0.01, 1);
		d_x += pBot->f_aim_x_angle_delta * f_velocity;
		d_y += pBot->f_aim_y_angle_delta * f_velocity;

		float difficulty = bot_aim_difficulty;
		if (difficulty < 0.0f) difficulty = 0.0f;
		if (difficulty > 2.0f) difficulty = 2.0f;

		float jitter = aim_jitter_scale[pBot->bot_skill] * difficulty;
		if (jitter > 0.0f)
		{
			d_x += RANDOM_FLOAT(-jitter, jitter);
			d_y += RANDOM_FLOAT(-jitter, jitter);
		}
	}

	if (d_x > 180.0f)
		d_x -= 360.0f;
	if (d_x < -180.0f)
		d_x += 360.0f;

	if (d_y > 180.0f)
		d_y -= 360.0f;
	if (d_y < -180.0f)
		d_y += 360.0f;

	if (!pBot->b_combat_longjump)
	{
		pEdict->v.idealpitch = pEdict->v.v_angle.x + d_x;
		BotFixIdealPitch(pEdict);
		
		pEdict->v.ideal_yaw = pEdict->v.v_angle.y + d_y;
		BotFixIdealYaw(pEdict);
	}

	Vector bot_angle, vecSrc, vecEnd;
	//v_enemy.z = 0;  // ignore z component (up & down)
	
	f_distance = v_enemy.Length();  // how far away is the enemy scum?

	// allow 15 seconds for the mindray to regen it's ammo if it's low
	if (mod_id == SI_DLL && pBot->current_weapon.iId == SI_WEAPON_MINDRAY &&
		BotAssessPrimaryAmmo(pBot, SI_WEAPON_MINDRAY) == AMMO_CRITICAL)
		pBot->f_mindray_regen_time = gpGlobals->time + 15.0;

	// if this is Science and Industry and our enemy is a scientist, run at them
	// and attack with the briefcase!
	if (pBot->pBotEnemy && mod_id == SI_DLL &&
		FStrEq(STRING(pBot->pBotEnemy->v.classname), "monster_scientist"))
	{
		int enemy_team = UTIL_GetTeam(pBot->pBotEnemy);
		// engage if enemy scientist and we don't have anything
		if (enemy_team != team && pBot->i_carry_type == CARRY_NONE)
		{
			pBot->f_move_speed = pBot->f_max_speed;
			pBot->f_ignore_wpt_time = gpGlobals->time + 0.2;
		}

		if (FInViewCone(&v_enemy_origin, pEdict) && FVisible(v_enemy_origin, pEdict))
		{	// switch to the briefcase
			if (pBot->f_shoot_time <= gpGlobals->time && f_distance <= 128.0 && enemy_team != team &&
				pBot->current_weapon.iId != SI_WEAPON_BRIEFCASE && pBot->i_carry_type == CARRY_NONE)
				BotFireWeapon(v_enemy, pBot, SI_WEAPON_BRIEFCASE, true);
			// actually hit them
			if (pBot->f_shoot_time <= gpGlobals->time && f_distance <= 50.0 && enemy_team != team &&
				pBot->i_carry_type == CARRY_NONE)
				BotFireWeapon(v_enemy, pBot, SI_WEAPON_BRIEFCASE);
/*			// use mindray if we have it, it's an enemy sci and we're carrying something, or it's an ally sci
			else if (pBot->f_shoot_time <= gpGlobals->time && pBot->f_mindray_regen_time < gpGlobals->time &&
				((pBot->i_carry_type > CARRY_NONE && enemy_team != team) || (enemy_team == team)) &&
				pBot->pEdict->v.weapons & (1<<SI_WEAPON_MINDRAY))
				BotFireWeapon(v_enemy, pBot, SI_WEAPON_MINDRAY);*/
		}

		return;
	}

	if (pBot->pBotEnemy && mod_id == VALVE_DLL &&
		FStrEq(STRING(pBot->pBotEnemy->v.classname), "grenade"))
	{
		// Remove the need to kick the grenade later, bot will kick on the spot.
		/*
		if (pBot->b_hasgrenade && pBot->f_shoot_time <= gpGlobals->time) {
			pBot->pEdict->v.impulse = 206;
			pBot->b_hasgrenade = FALSE;
			ALERT(at_console, "Kicking grenade. [distance=%.2f]\n", f_distance);
			pBot->f_shoot_time = gpGlobals->time + 0.25;
			return;
		}
		*/

		pBot->f_move_speed = pBot->f_max_speed;
		pBot->f_ignore_wpt_time = gpGlobals->time + 0.2;

		if (FInViewCone(&v_enemy_origin, pEdict) && FVisible(v_enemy_origin, pEdict))
		{
			if (pBot->f_shoot_time <= gpGlobals->time && f_distance <= 192.0) {
#ifdef _DEBUG
				ALERT(at_console, "Kicking grenade. [distance=%.2f]\n", f_distance);
#endif
				// If the bot picks up the grenade first
				//pBot->pEdict->v.button |= IN_USE;
				//pBot->b_hasgrenade = TRUE;
				// Otherwse, just kick it
				pBot->pEdict->v.impulse = 206;
				pBot->f_shoot_time = gpGlobals->time + 0.2;
			}
		}

		return;
	}

	if (pBot->f_engage_enemy_check <= gpGlobals->time)
		pBot->b_last_engage = BotShouldEngageEnemy(pBot, pBot->pBotEnemy);

	bool bShouldEngage = pBot->b_last_engage;

	if ((RANDOM_LONG(1,100) < pBot->i_engage_aggressiveness) && (!pBot->b_engaging_enemy) && 
		(bShouldEngage))
	{
		// remember our current goal so we can go back to it once the enemy is dead (or we flee)
		if (pBot->waypoint_goal != -1)
			pBot->old_waypoint_goal = pBot->waypoint_goal;

		pBot->b_engaging_enemy = TRUE;
		if (b_chat_debug && pBot && pBot->pBotEnemy)
		{
			sprintf(pBot->debugchat, "I am going to engage %s...\n", STRING(pBot->pBotEnemy->v.netname));
			UTIL_HostSay(pBot->pEdict, 0, pBot->debugchat);
		}
	}

	// Arena: always engage immediately — the opponent IS the only objective.
	// Skip the random aggressiveness gate.
	if (is_gameplay == GAME_ARENA && !pBot->b_engaging_enemy && pBot->pBotEnemy != NULL)
	{
		if (pBot->waypoint_goal != -1)
			pBot->old_waypoint_goal = pBot->waypoint_goal;
		pBot->b_engaging_enemy = TRUE;
	}

	// see if we should engage the enemy every half second
	if (pBot->f_engage_enemy_check <= gpGlobals->time)
		pBot->f_engage_enemy_check = gpGlobals->time + 0.5;

	if (pBot->b_engaging_enemy && !bShouldEngage)
	{
		if (b_chat_debug && pBot && pBot->pBotEnemy)
		{
			sprintf(pBot->debugchat, "I gave up engaging %s!\n", STRING(pBot->pBotEnemy->v.netname));
			UTIL_HostSay(pBot->pEdict, 0, pBot->debugchat);
		}
		pBot->f_ignore_wpt_time = 0.0;
		pBot->b_engaging_enemy = FALSE;
		// In KTS there is no valid 'old goal' — the ball is always the objective.
		// Restoring old_waypoint_goal would send the bot away from the dislodged ball.
		if (is_gameplay == GAME_KTS)
		{
			pBot->waypoint_goal     = -1;
			pBot->old_waypoint_goal = -1;
			pBot->f_waypoint_goal_time = 0.0f; // force immediate BotFindWaypointGoal update
		}
		else if (pBot->old_waypoint_goal != -1)
		{
			pBot->waypoint_goal = pBot->old_waypoint_goal;
			pBot->old_waypoint_goal = -1;
		}
	}
	else if (pBot->b_engaging_enemy && bShouldEngage)
	{
		// we're close, forget about waypoints and try to use combat tactics
		if (f_distance < 512 && FInViewCone( &v_enemy_origin, pEdict ) &&
			FHullClear( v_enemy_origin, pEdict ))
		{
			pBot->f_ignore_wpt_time = gpGlobals->time + 0.1;
		}

	}

	// find the difference in the current and ideal angle
	float diff = std::fabs(pEdict->v.v_angle.y - pEdict->v.ideal_yaw);

	if (pBot->curr_waypoint_index == -1 || pBot->f_ignore_wpt_time > gpGlobals->time)
	{
		pBot->f_move_speed = pBot->f_max_speed;
		if (((f_distance < 64 && !pBot->b_longjump) || (f_distance < 128 && pBot->b_longjump)) &&
			pBot->current_weapon.iId != SI_WEAPON_BRIEFCASE && pBot->current_weapon.iId != VALVE_WEAPON_CROWBAR)
			pBot->f_move_speed *= -1;
		// bot skill 5 doesn't even try to strafe
		if ((pBot->bot_skill + 1) < 5)
		{	/*
			// get the direction we're traveling
			bot_angle = pBot->pEdict->v.v_angle;
			bot_angle.x = 0;
			float dgrad = asin(pBot->f_strafe_speed / pBot->f_max_speed) * 180 / PI;
			bot_angle.y += dgrad;

			if (bot_angle.y > 180)
				bot_angle.y -= 360;
			if (bot_angle.y < -180)
				bot_angle.y += 360;

			MAKE_VECTORS(bot_angle);
			*/
			vecSrc = pEdict->v.origin;
			vecEnd = pBot->v_curr_direction.Normalize() * pBot->f_max_speed * pBot->f_frame_time;
			// trace a line to see if anything is in our way
			UTIL_TraceLine(vecSrc, vecSrc + vecEnd, dont_ignore_monsters, pEdict, &tr);

			// switch our strafe direction every so often or if something is in the way
			if (pBot->f_strafe_chng_dir <= gpGlobals->time || tr.flFraction < 1.0)
			{
				pBot->f_strafe_chng_dir = gpGlobals->time + RANDOM_FLOAT(1.0, 3.0);
				pBot->b_strafe_direction = !pBot->b_strafe_direction;//RANDOM_LONG(0,1) ? true : false;
			}
			// go strafe crazy!
			pBot->f_strafe_speed = pBot->b_strafe_direction ? pBot->f_max_speed : -pBot->f_max_speed;

			// try to do a tricky longjump
			if ((pBot->b_longjump) && (pBot->f_combat_longjump < gpGlobals->time) && 
				(!pBot->b_combat_longjump) && (pEdict->v.flags & FL_ONGROUND) &&
				(f_distance > 128) && (f_distance < (LONGJUMP_DISTANCE * 
				(800 / CVAR_GET_FLOAT("sv_gravity")))) && (diff <= 1))
			{
				pBot->b_longjump_dir = RANDOM_LONG(0,1) ? true : false;
				bot_angle = pBot->pEdict->v.v_angle;
				bot_angle.x = -bot_angle.x;
				vecSrc = pEdict->v.origin;
				// get a random angle (-30 or 30)
				int mod = pBot->b_longjump_dir ? -1 : 1;
				for (int i = 0; i < 2; i++)
				{
					Vector target_angle = bot_angle;
					target_angle.y += 30 * mod;

					if (target_angle.y > 180)
						target_angle.y -= 360;
					if (target_angle.y < -180)
						target_angle.y += 360;
							
					MAKE_VECTORS( target_angle );
					vecEnd = gpGlobals->v_forward * (LONGJUMP_DISTANCE * 
						(800 / CVAR_GET_FLOAT("sv_gravity")));

					// are we all clear?
					UTIL_TraceLine(vecSrc, vecSrc + vecEnd, dont_ignore_monsters, pEdict, &tr);

//					if (listenserver_edict)
//						WaypointDrawBeam(listenserver_edict, vecSrc, vecSrc + vecEnd, 20, 0, 255, 32, 32, 200, 10);

					if (tr.flFraction >= 1.0)
					{
						//SERVER_PRINT( "Clear longjump path found\n");
						pBot->b_combat_longjump = TRUE;
						pEdict->v.ideal_yaw += 30 * mod;
						diff = 1;
						BotFixIdealYaw(pEdict);
						break;
					}
					// try other direction;
					mod *= -1;
				}
				pBot->f_combat_longjump = gpGlobals->time + 0.2;
			}
		}

		// stop trying to longjump after half a second
		if ((pBot->f_combat_longjump < gpGlobals->time - 0.5) && (pBot->b_combat_longjump))
			pBot->b_combat_longjump = FALSE;

		// longjump
		if ((pEdict->v.waterlevel == 0) && (pEdict->v.flags & FL_ONGROUND) &&
			(pBot->b_longjump) && (pEdict->v.velocity.Length() > 220) && (pBot->b_combat_longjump) &&
			(diff <= 0.01))
		{
			// don't try to move for 1.0 seconds, otherwise the longjump
			// is fucked up	
			pBot->f_longjump_time = gpGlobals->time + 1.0;
			// we're don trying to do a longjump
			pBot->b_combat_longjump = FALSE;
			// don't do another one for a certain amount of time
			if (RANDOM_LONG(1,100) > 10)
				pBot->f_combat_longjump = gpGlobals->time + 1.0;//RANDOM_FLOAT(0.5, 1.0);
			else
				pBot->f_combat_longjump = gpGlobals->time + RANDOM_LONG(3.0,10.0);
			// actually do the longjump
			if (mod_id != SI_DLL)	// S&I auto longjumps, HLDM/Crabbed don't
				pEdict->v.button |= IN_DUCK;
			else if (mod_id == SI_DLL) // have to be going forward in S&I
				pEdict->v.button |= IN_FORWARD;
			pEdict->v.button |= IN_JUMP;
			//SERVER_PRINT( "%s doing longjump!\n", STRING(pEdict->v.netname));
		}

		if (pBot->f_longjump_time > gpGlobals->time)
			pBot->f_move_speed = pBot->f_strafe_speed = 0;
	}
	// is it time to shoot yet?
	if ((pBot->f_shoot_time <= gpGlobals->time) && !(pBot->pEdict->v.flags & FL_GODMODE) &&
		!(pBot->pBotEnemy->v.flags & FL_GODMODE) && 
		(FInViewCone(&v_enemy_origin, pEdict)) && (FVisible(v_enemy_origin, pEdict)) && 
		(pBot->f_reaction_target_time < gpGlobals->time) && (pBot->f_reload_time < gpGlobals->time)/* && (diff <= 1)*/)
	{
		// select the best weapon to use at this distance and fire...
		BotFireWeapon(v_enemy, pBot, 0);
	}
}

// look around for tripmines, timed grenades, contact grenades, 
// satchels, RPG rockets, and snarks.  Shoot them. 
void BotAssessGrenades( bot_t *pBot )
{
//	ALERT(at_console, "BotAssessGrenades\n");
	edict_t *pEdict = pBot->pEdict;
	edict_t *pGrenade = NULL;
	edict_t *pNewGrenade = NULL;
	Vector vecEnd;
	float nearestdistance = 16384;
	float mindistance = 256;
	// search the world for grenades...
	while (!FNullEnt(pGrenade = UTIL_FindEntityInSphere (pGrenade, pEdict->v.origin, 1000)))
	{
		vecEnd = pGrenade->v.origin;
		
		if (mod_id == CRABBED_DLL)
		{	// crabbed lets players shoot anything
			if ((strcmp("monster_satchel", STRING(pGrenade->v.classname)) != 0) && 
				(strcmp("monster_tripmine", STRING(pGrenade->v.classname)) != 0) && 
				(strcmp("grenade", STRING(pGrenade->v.classname)) != 0) && 
				(strcmp("rpg_rocket", STRING(pGrenade->v.classname)) != 0) && 
				(strcmp("monster_snark", STRING(pGrenade->v.classname)) != 0))
				continue;
		}
		else
		{
			if ((strcmp("monster_tripmine", STRING(pGrenade->v.classname)) != 0) && 
				(strcmp("monster_snark", STRING(pGrenade->v.classname)) != 0) &&
				(strcmp("grenade", STRING(pGrenade->v.classname)) != 0) &&
				(strcmp("monster_chumtoad", STRING(pGrenade->v.classname)) != 0) &&
				(strcmp("monster_propdecoy", STRING(pGrenade->v.classname)) != 0) &&
				(strcmp("loot_crate", STRING(pGrenade->v.classname)) != 0) &&
				(strcmp("kts_snowball", STRING(pGrenade->v.classname)) != 0))
				continue;

		}
		// don't shoot our own grenades!
		if (pGrenade->v.owner == pEdict)
            continue;

		// don't shoot our teams grenades on S&I
		// if (UTIL_GetTeam(pGrenade) == UTIL_GetTeam(pEdict))
        //    continue;

		// see if bot can't see the grenade...
		if (!FInViewCone( &vecEnd, pEdict ) ||
			!FVisible( vecEnd, pEdict ))
            continue;
		
		//mindistance = 256;
		// we don't care how close snarks are, but the others explode
		//if (strcmp("monster_snark", STRING(pGrenade->v.classname)) == 0)
		mindistance = 0;
		
		float distance = (pGrenade->v.origin - pEdict->v.origin).Length();
		// our current enemy is closer, forget the grenade
		if (pBot->pBotEnemy != NULL &&
			(pGrenade->v.origin - UTIL_GetOrigin(pBot->pBotEnemy)).Length() < distance)
			continue;
		// is the grenade the right distance away?
		if (distance < nearestdistance && distance > mindistance)
		{
            nearestdistance = distance;
            pNewGrenade = pGrenade;
			
            pBot->pBotUser = NULL;  // don't follow user when we've found a grenade
		}
	}
	
	if (pNewGrenade)
	{
		// the grenade is our enemy, blast it!
		//SERVER_PRINT( "%s - found %s!\n", STRING(pEdict->v.netname), STRING(pNewGrenade->v.classname));
		pBot->pBotEnemy = pNewGrenade;
	}
}

bool BotWeaponPrimaryDistance( bot_t *pBot, float distance, int weapon_id )
{
//	ALERT(at_console, "BotWeaponPrimaryDistance\n");
	bot_weapon_select_t *pSelect = NULL;
	pSelect = WeaponGetSelectPointer();
	// select pointer not valid?
	if (pSelect == NULL)
		return FALSE;

	int select_index = 0;
	// loop through all the weapons until terminator is found...
	while (pSelect[select_index].iId)
	{
		if (pSelect[select_index].iId != weapon_id)
		{
			select_index++;
			continue;
		}
		else
			break;
	}

	if ((distance < pSelect[select_index].primary_min_distance) &&
		(distance > pSelect[select_index].primary_max_distance))
		return FALSE;

	return TRUE;
}

bool BotWeaponSecondaryDistance( bot_t *pBot, float distance, int weapon_id )
{
//	ALERT(at_console, "BotWeaponSecondaryDistance\n");
	bot_weapon_select_t *pSelect = NULL;
	pSelect = WeaponGetSelectPointer();
	// select pointer not valid?
	if (pSelect == NULL)
		return FALSE;

	int select_index = 0;
	// loop through all the weapons until terminator is found...
	while (pSelect[select_index].iId)
	{
		if (pSelect[select_index].iId != weapon_id)
		{
			select_index++;
			continue;
		}
		else
			break;
	}

	if ((distance < pSelect[select_index].secondary_min_distance) &&
		(distance > pSelect[select_index].secondary_max_distance))
		return FALSE;

	return TRUE;
}

float BotAssessPrimaryAmmo( bot_t *pBot, int weapon_id )
{
//	ALERT(at_console, "BotAssessPrimaryAmmo\n");
	bot_weapon_select_t *pSelect = NULL;
	pSelect = WeaponGetSelectPointer();
	int team = UTIL_GetTeam(pBot->pEdict);
	// select pointer not valid?
	if (pSelect == NULL)
		return AMMO_NONE;
	if (weapon_id - 1 > ARRAYSIZE(weapon_defs))
		return AMMO_NONE;
	// does this weapon even use ammo?	
	if (weapon_defs[weapon_id].iAmmo1 == -1 || weapon_defs[weapon_id].iAmmo1Max <= 0)
		return AMMO_NONE;

	int select_index = WeaponGetSelectIndex(weapon_id);

	int max = weapon_defs[weapon_id].iAmmo1Max;
	if (mod_id == SI_DLL)
	{
		if (team < 0 || team > 1)
			return AMMO_NONE;

		// we don't have either upgrade
		if (!g_Researched[team][RESEARCH_AMMO_REPLICATE].researched &&
			!g_Researched[team][RESEARCH_AMMO_REPLICATE].stolen &&
			!g_Researched[team][RESEARCH_AMMO_REPLICATE2].researched &&
			!g_Researched[team][RESEARCH_AMMO_REPLICATE2].stolen)
			max -= WeaponGetAmmoResearchDiff(weapon_id) * 2;
		// we have replicate 1, but not 2
		else if ((g_Researched[team][RESEARCH_AMMO_REPLICATE].researched ||
			g_Researched[team][RESEARCH_AMMO_REPLICATE].stolen) &&
			(!g_Researched[team][RESEARCH_AMMO_REPLICATE2].researched &&
			!g_Researched[team][RESEARCH_AMMO_REPLICATE2].stolen))
			max -= WeaponGetAmmoResearchDiff(weapon_id);
	}

	float ammo_percent = (float)pBot->m_rgAmmo[weapon_defs[weapon_id].iAmmo1] / (float)max;
	// is our ammo critical (can't attack with this weapon)
	if ((weapon_id != pBot->current_weapon.iId &&
		pBot->m_rgAmmo[weapon_defs[weapon_id].iAmmo1] < pSelect[select_index].min_primary_ammo) ||
		(weapon_id == pBot->current_weapon.iId &&
		pBot->m_rgAmmo[weapon_defs[weapon_id].iAmmo1] < pSelect[select_index].min_primary_ammo &&
		((pBot->current_weapon.iClip != -1 && pBot->current_weapon.iClip < pSelect[select_index].min_primary_ammo) ||
		(pBot->current_weapon.iClip == -1))))
		return AMMO_CRITICAL;

	return ammo_percent;
}

float BotAssessSecondaryAmmo( bot_t *pBot, int weapon_id )
{
//	ALERT(at_console, "BotAssessSecondaryAmmo\n");
	bot_weapon_select_t *pSelect = NULL;
	pSelect = WeaponGetSelectPointer();
	int team = UTIL_GetTeam(pBot->pEdict);
	// select pointer not valid?
	if (pSelect == NULL)
		return AMMO_NONE;
	if (weapon_id - 1 > ARRAYSIZE(weapon_defs))
		return AMMO_NONE;
	// does this weapon even use ammo?	
	if (weapon_defs[weapon_id].iAmmo2 == -1 || weapon_defs[weapon_id].iAmmo2Max <= 0)
		return AMMO_NONE;

	int select_index = WeaponGetSelectIndex(weapon_id);

	int max = weapon_defs[weapon_id].iAmmo2Max;
	if (mod_id == SI_DLL)
	{
		if (team < 0 || team > 1)
			return AMMO_NONE;

		// we don't have either upgrade
		if (!g_Researched[team][RESEARCH_AMMO_REPLICATE].researched &&
			!g_Researched[team][RESEARCH_AMMO_REPLICATE].stolen &&
			!g_Researched[team][RESEARCH_AMMO_REPLICATE2].researched &&
			!g_Researched[team][RESEARCH_AMMO_REPLICATE2].stolen)
			max -= WeaponGetAmmoResearchDiff(weapon_id) * 2;
		// we have replicate 1, but not 2
		else if ((g_Researched[team][RESEARCH_AMMO_REPLICATE].researched ||
			g_Researched[team][RESEARCH_AMMO_REPLICATE].stolen) &&
			(!g_Researched[team][RESEARCH_AMMO_REPLICATE2].researched &&
			!g_Researched[team][RESEARCH_AMMO_REPLICATE2].stolen))
			max -= WeaponGetAmmoResearchDiff(weapon_id);
	}

	float ammo_percent = (float)pBot->m_rgAmmo[weapon_defs[weapon_id].iAmmo2] / (float)max;
	// is our ammo critical (can't attack with this weapon)
	if (pBot->m_rgAmmo[weapon_defs[weapon_id].iAmmo2] < pSelect[select_index].min_secondary_ammo && 
		(weapon_id == pBot->current_weapon.iId && pBot->current_weapon.iClip2 < pSelect[select_index].min_secondary_ammo))
		return AMMO_CRITICAL;
		
	return ammo_percent;
}
