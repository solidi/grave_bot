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

extern bot_t bots[32];

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
// a goal.  Call every frame while pursuing.
//
// Uses a 4-phase jump sequence matching the mod's jump system:
//   phase 0 → detect stall, start 1st jump
//   phase 1 → 2nd jump (double-jump, while airborne)
//   phase 2 → 3rd jump (triple-jump / flip, while airborne)
//   phase 3 → sequence complete, reset after cooldown
//
// bForceTrigger=true bypasses the "close horizontally + goal
// above step height" gate so callers (e.g. combat stuck
// detection) can fire the combo against same-level walls and
// boxes.  Forward-facing + max forward speed are re-asserted
// every frame so the flip's velocity.Length2D() > 100 check
// passes — this is what makes CTF flag pursuit's combo
// reliably clear ledges and is now reused for combat stalls.
//
// Returns true if the bot is currently in a jump sequence
// (caller should keep running forward).
//=========================================================
static bool BotGoalElevatedJump( bot_t *pBot, Vector vecGoal, bool bForceTrigger = false )
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
	// Forced triggers (combat stall) skip the gate entirely — the caller
	// has already validated that we're stuck and pursuing a real target.
	if (pBot->i_goal_jump_phase == 0 && !bForceTrigger
		&& (horzDist > 300.0f || heightDiff < 20.0f))
	{
		pBot->f_goal_jump_stall_time = 0.0f;
		return false;
	}

	// Track how long we've been stuck below the goal
	if (pBot->f_goal_jump_stall_time == 0.0f)
		pBot->f_goal_jump_stall_time = gpGlobals->time;

	// Wait 0.5s before starting jump sequence (gives waypoint nav a chance).
	// Forced triggers skip the wait — the caller's own stall detector has
	// already burned ~1s of stuck time before invoking us.
	if (!bForceTrigger
		&& gpGlobals->time - pBot->f_goal_jump_stall_time < 0.5f)
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
// Loot mode — 4-team round-based objective AI
//
// Gamerules: many `loot_crate` entities spawn each round
// (one randomly marked as containing the loot).  Breaking
// the loot crate spawns a `loot_entity` (touchable) that a
// player picks up; `CHalfLifeLoot::CaptureCharm` then sets
// `pPlayer->m_bHoldingLoot = TRUE` and writes
// `pev->fuser4 = RADAR_LOOT` (16) on the holder.  All other
// players carry their team index (0–3) in fuser4.  When the
// holder reaches `loot_goal`, their team scores.
//
// Bot strategy (priority order, evaluated every 0.75 s):
//   CARRIER   — self has loot → run to loot_goal, suppress
//                 engagement above 25 HP.
//   RECOVERER — enemy holds loot → force-target the carrier.
//   ESCORT    — teammate holds loot → trail and protect.
//   GRABBER   — loot_entity loose → always run to grab it.
//   BREAKER   — default — seek the nearest loot_crate and
//                 shoot it open; switch crates after broken
//                 or after a stuck timeout.
//=========================================================
#define RADAR_LOOT_VAL  16   // mirrors RADAR_LOOT in src/common/const.h

#define LOOT_ROLE_EVAL_CADENCE   0.75f
#define LOOT_CRATE_PICK_CADENCE  0.75f
#define LOOT_CARRIER_PACIFY_HP   25.0f
#define LOOT_ESCORT_PROXIMITY    128.0f
#define LOOT_BREAKER_FIRE_DIST   1024.0f

#define MAX_LOOT_CRATES 32

static float    s_loot_cache_time = -1.0f;
static edict_t *s_pLootEntity = NULL;
static edict_t *s_pLootGoal   = NULL;
static int      s_loot_crate_count = 0;
static edict_t *s_loot_crates[MAX_LOOT_CRATES];

//=========================================================
// BotLootFindEntities — per-frame cached scan for the loot
// entity, goal, and live crate list.
//=========================================================
static void BotLootFindEntities( void )
{
	if (s_loot_cache_time == gpGlobals->time)
		return;

	s_loot_cache_time = gpGlobals->time;
	s_pLootEntity     = NULL;
	s_pLootGoal       = NULL;
	s_loot_crate_count = 0;

	edict_t *pEnt = NULL;
	while ((pEnt = UTIL_FindEntityByClassname(pEnt, "loot_entity")) != NULL)
	{
		if (FNullEnt(pEnt) || pEnt->free) continue;
		if (pEnt->v.effects & EF_NODRAW)  continue; // held → invisible
		s_pLootEntity = pEnt;
		break;
	}

	pEnt = NULL;
	while ((pEnt = UTIL_FindEntityByClassname(pEnt, "loot_goal")) != NULL)
	{
		if (FNullEnt(pEnt) || pEnt->free) continue;
		s_pLootGoal = pEnt;
		break;
	}

	pEnt = NULL;
	while ((pEnt = UTIL_FindEntityByClassname(pEnt, "loot_crate")) != NULL
		&& s_loot_crate_count < MAX_LOOT_CRATES)
	{
		if (FNullEnt(pEnt) || pEnt->free) continue;
		if (pEnt->v.solid == SOLID_NOT)   continue; // mid-Break, ignore
		if (pEnt->v.health <= 0)          continue;
		s_loot_crates[s_loot_crate_count++] = pEnt;
	}
}

// Return the player edict currently carrying the loot
// (fuser4 == RADAR_LOOT) or NULL if no carrier.  We cannot
// rely on `fuser4 > 0` here because all players in Loot
// have a non-zero team-index fuser4 (0–3) outside of carry.
static edict_t *BotLootGetHolder( void )
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		edict_t *pPlayer = INDEXENT(i);
		if (FNullEnt(pPlayer) || pPlayer->free)
			continue;
		if (!(pPlayer->v.flags & FL_CLIENT))
			continue;
		if (!IsAlive(pPlayer))
			continue;
		if ((int)pPlayer->v.fuser4 == RADAR_LOOT_VAL)
			return pPlayer;
	}
	return NULL;
}

// True when bot pSelf is currently standing on top of pCrate.
// Use the crate's absolute bbox instead of a fixed radius —
// loot_crate brushes commonly have horizontal half-widths
// well over 48u, so a fixed-radius check missed bots planted
// on the crate's corners.  Test:
//   1. groundentity == crate (most reliable when it fires), OR
//   2. on-ground AND bot origin is within the crate's XY
//      bbox (with a small slop) AND the bot's feet are at or
//      above the crate's top minus a tolerance.
static bool BotLootIsOnTopOfCrate( edict_t *pBotEdict, edict_t *pCrate )
{
	if (FNullEnt(pBotEdict) || FNullEnt(pCrate)) return false;
	if (!(pBotEdict->v.flags & FL_ONGROUND)) return false;

	if (!FNullEnt(pBotEdict->v.groundentity)
		&& pBotEdict->v.groundentity == ENT(pCrate))
		return true;

	Vector vMins = pCrate->v.absmin;
	Vector vMaxs = pCrate->v.absmax;
	const Vector &vOrg = pBotEdict->v.origin;

	const float xySlop  = 16.0f;  // small over-hang allowance
	const float topSlop = 12.0f;  // ~half a step

	if (vOrg.x < vMins.x - xySlop || vOrg.x > vMaxs.x + xySlop) return false;
	if (vOrg.y < vMins.y - xySlop || vOrg.y > vMaxs.y + xySlop) return false;

	// Feet roughly at or above crate top (origin is mid-torso, so
	// origin.z - 36 ≈ feet for a standing player).
	float feetZ = vOrg.z - 36.0f;
	return (feetZ >= vMaxs.z - topSlop);
}

// True when SOME OTHER live bot is currently on top of pCrate.
// Lets BREAKER target-picking skip already-occupied crates so
// two bots don't pile onto the same one and block each other.
static bool BotLootCrateOccupiedByOtherBot( edict_t *pCrate, bot_t *pSelf )
{
	if (FNullEnt(pCrate)) return false;
	for (int i = 0; i < 32; i++)
	{
		bot_t *pOther = &bots[i];
		if (pOther == pSelf) continue;
		if (FNullEnt(pOther->pEdict) || pOther->pEdict->free) continue;
		if (!(pOther->pEdict->v.flags & FL_CLIENT)) continue;
		if (!IsAlive(pOther->pEdict)) continue;
		if (BotLootIsOnTopOfCrate(pOther->pEdict, pCrate))
			return true;
	}
	return false;
}

// On-top-of-crate handler.  Called every frame from BotThink.
//
// Attacking from on top is unreliable: an eye-trace straight
// down to the crate origin clips the crate brush itself and
// reads as occluded — BotShootAtEnemy's FVisible gate then
// blocks IN_ATTACK, the synthetic-enemy injection skips the
// crate (same FVisible failure), and forcing the button
// without aim resolution produces wild swings at empty air.
//
// So when we detect we're on top, we DISMOUNT immediately:
// pick a horizontal away-from-crate direction, override v_goal
// + suppress waypoint nav for ~1s so the bot actually walks
// away (a bare strafe wasn't enough — the bot kept landing
// back on top), and add this crate to a per-bot ignore list
// so we don't immediately re-target it.  After ~2s the ignore
// expires and BREAKER picking can re-select it from the side.
//
// Returns true when it took control this frame.
bool BotLootHandleOnTopOfCrate( bot_t *pBot )
{
	if (is_gameplay != GAME_LOOT) return false;
	if (pBot->b_loot_has_loot)    return false;

	edict_t *pEdict = pBot->pEdict;

	// Locate the crate we're standing on.  Prefer the cached BREAKER
	// pick (cheap check), but fall back to scanning ALL live crates
	// so non-BREAKER roles (RECOVERER / ESCORT / GRABBER) that bump
	// onto a crate while waypoint-traveling also get dismounted.
	edict_t *pCrate = NULL;
	if (pBot->i_loot_crate_target_index > 0)
	{
		edict_t *pCached = INDEXENT(pBot->i_loot_crate_target_index);
		if (!FNullEnt(pCached) && !pCached->free
			&& FStrEq(STRING(pCached->v.classname), "loot_crate")
			&& pCached->v.solid != SOLID_NOT && pCached->v.health > 0
			&& BotLootIsOnTopOfCrate(pEdict, pCached))
			pCrate = pCached;
	}
	if (FNullEnt(pCrate))
	{
		BotLootFindEntities();
		for (int i = 0; i < s_loot_crate_count; i++)
		{
			edict_t *pC = s_loot_crates[i];
			if (FNullEnt(pC) || pC->free) continue;
			if (pC->v.solid == SOLID_NOT) continue;
			if (pC->v.health <= 0) continue;
			if (BotLootIsOnTopOfCrate(pEdict, pC)) { pCrate = pC; break; }
		}
	}
	if (FNullEnt(pCrate)) return false;

	// Pick a horizontal away-from-crate vector.  Use the bot's
	// own offset from the crate origin (or fall back to a small
	// nudge based on entindex parity if we're sitting exactly on
	// top).  Project to XY and extend ~256u out so v_goal is well
	// off the crate's bbox.
	Vector vAway = pEdict->v.origin - pCrate->v.origin;
	vAway.z = 0;
	if (vAway.Length() < 4.0f)
	{
		vAway = Vector((ENTINDEX(pEdict) & 1) ? 1.0f : -1.0f, 0.0f, 0.0f);
	}
	else
	{
		vAway = vAway.Normalize();
	}
	Vector vAwayPoint = pEdict->v.origin + vAway * 256.0f;
	vAwayPoint.z = pEdict->v.origin.z;

	// Override navigation so the bot WALKS off the crate instead
	// of just hopping in place.
	pBot->v_goal           = vAwayPoint;
	pBot->f_goal_proximity = 0.0f;
	pBot->f_move_speed     = pBot->f_max_speed;
	pBot->f_strafe_speed   = 0.0f;

	// Face the away-direction so f_move_speed actually translates
	// to motion in that direction.
	Vector vAng = UTIL_VecToAngles(vAway);
	pEdict->v.ideal_yaw = vAng.y;
	BotFixIdealYaw(pEdict);

	// Press IN_JUMP only on the first frame on top — the very
	// next frames we want to RUN, not jump-spam (which produced
	// the bunny-hop on top).  We detect first-frame via the
	// ignore-list state: it's only updated below when we transition.
	if (pBot->i_loot_crate_ignore_index != ENTINDEX(pCrate))
	{
		pEdict->v.button |= IN_JUMP;
	}

	// Suppress waypoint detours and pickup steering for 1.5s so
	// the bot actually clears the crate's bbox before nav can
	// re-route it back into a re-mount.
	pBot->f_ignore_wpt_time = gpGlobals->time + 1.5f;
	pBot->pBotPickupItem    = NULL;
	pBot->item_waypoint     = -1;

	// Add the crate to the per-bot ignore list for 2s and
	// invalidate the cached pick so BREAKER chooses something else.
	pBot->i_loot_crate_ignore_index = ENTINDEX(pCrate);
	pBot->f_loot_crate_ignore_until = gpGlobals->time + 2.0f;
	pBot->i_loot_crate_target_index = -1;
	pBot->f_loot_crate_target_time  = gpGlobals->time + 1.0f;
	pBot->f_loot_ontop_until        = 0;
	return true;
}

// Scan all live crates and return the closest one that is
// currently visible (FInViewCone + FVisible) to the bot,
// within maxDist.  Used by the BREAKER engagement hook so
// bots opportunistically shoot crates they walk past — the
// cached "nearest" pick may be a different crate.
edict_t *BotLootFindBestVisibleCrate( bot_t *pBot, float maxDist )
{
	BotLootFindEntities();
	if (s_loot_crate_count == 0)
		return NULL;

	edict_t *pEdict = pBot->pEdict;
	edict_t *pBest  = NULL;
	float   bestD   = maxDist;

	for (int i = 0; i < s_loot_crate_count; i++)
	{
		edict_t *pC = s_loot_crates[i];
		if (FNullEnt(pC) || pC->free) continue;
		if (pC->v.solid == SOLID_NOT) continue;
		if (pC->v.health <= 0)        continue;

		float d = (pC->v.origin - pEdict->v.origin).Length();
		if (d >= bestD) continue;

		// Skip per-bot ignore (recently dismounted from this one).
		if (pBot->f_loot_crate_ignore_until > gpGlobals->time
			&& pBot->i_loot_crate_ignore_index == ENTINDEX(pC))
			continue;

		// Skip crates another bot is already camping on top of —
		// prevents the two-bots-stuck-on-same-crate deadlock.
		// (Doesn't apply when *I* am the one on top.)
		if (BotLootCrateOccupiedByOtherBot(pC, pBot)
			&& !BotLootIsOnTopOfCrate(pEdict, pC))
			continue;

		// NOTE: no FInViewCone gate — when the bot reaches v_goal
		// proximity (64u) it stops moving (f_move_speed = 0) and
		// retains stale yaw.  If we required the cone here the bot
		// would never assign the crate as enemy, never call
		// BotShootAtEnemy, and never rotate to face it → permanent
		// stall.  FVisible alone is enough; the shoot block will
		// yaw the bot onto the crate and fire next frame.
		if (!FVisible(pC->v.origin, pEdict)) continue;

		bestD = d;
		pBest = pC;
	}
	return pBest;
}

// Pick the nearest live crate.  Caches the choice on pBot
// for LOOT_CRATE_PICK_CADENCE seconds to avoid thrashing.
static edict_t *BotLootPickNearestCrate( bot_t *pBot )
{
	BotLootFindEntities();
	if (s_loot_crate_count == 0)
	{
		pBot->i_loot_crate_target_index = -1;
		return NULL;
	}

	edict_t *pEdict = pBot->pEdict;

	// Honor cached pick while still valid + crate alive + not
	// being camped by another bot (unless I'm the one camping it).
	if (pBot->i_loot_crate_target_index > 0
		&& pBot->f_loot_crate_target_time > gpGlobals->time)
	{
		edict_t *pCached = INDEXENT(pBot->i_loot_crate_target_index);
		if (!FNullEnt(pCached) && !pCached->free
			&& FStrEq(STRING(pCached->v.classname), "loot_crate")
			&& pCached->v.solid != SOLID_NOT && pCached->v.health > 0
			&& (!BotLootCrateOccupiedByOtherBot(pCached, pBot)
				|| BotLootIsOnTopOfCrate(pEdict, pCached)))
			return pCached;
	}

	pBot->f_loot_crate_target_time  = gpGlobals->time + LOOT_CRATE_PICK_CADENCE;
	pBot->i_loot_crate_target_index = -1;

	float bestDist = 1e9f;
	edict_t *pBest = NULL;
	for (int i = 0; i < s_loot_crate_count; i++)
	{
		edict_t *pC = s_loot_crates[i];
		// Per-bot ignore: skip a crate we just dismounted from.
		if (pBot->f_loot_crate_ignore_until > gpGlobals->time
			&& pBot->i_loot_crate_ignore_index == ENTINDEX(pC))
			continue;
		// De-conflict: skip crates another bot is camping on top of.
		if (BotLootCrateOccupiedByOtherBot(pC, pBot)
			&& !BotLootIsOnTopOfCrate(pEdict, pC))
			continue;
		float d = (pC->v.origin - pEdict->v.origin).Length();
		if (d < bestDist) { bestDist = d; pBest = pC; }
	}

	// Fallback: if every crate is camped by another bot, pick the
	// nearest one anyway so the bot doesn't idle forever.
	if (!pBest)
	{
		for (int i = 0; i < s_loot_crate_count; i++)
		{
			edict_t *pC = s_loot_crates[i];
			float d = (pC->v.origin - pEdict->v.origin).Length();
			if (d < bestDist) { bestDist = d; pBest = pC; }
		}
	}

	if (pBest)
		pBot->i_loot_crate_target_index = ENTINDEX(pBest);
	return pBest;
}

//=========================================================
// LOOT — weapon drop/swap (mirrors rune-drop bot pattern)
//
// In Loot, players are capped at 1 non-fists weapon (or 3
// while their team controls the loot via "loot advantage").
// The server enforces the cap via CHalfLifeLoot::
// CanHavePlayerItem; the player.cpp +use handler drops the
// active weapon when the player presses +use near a weapon
// they can't carry.  Humans also have the "drop" client cmd.
//
// Bot equivalent: scan a radius for pickupable weapon ents,
// compare priority vs what we already hold, and either walk
// over the upgrade (under the cap) or drop our worst-held
// weapon when within touch range (at the cap) so the next
// touch frame picks the upgrade up via standard Touch().
//=========================================================
#define LOOT_WEAPON_SCAN_RADIUS    768.0f
#define LOOT_WEAPON_EVAL_CADENCE   1.0f
#define LOOT_WEAPON_SWAP_RANGE     80.0f
#define LOOT_WEAPON_DROP_COOLDOWN  1.5f

static int BotLootWeaponClassToId(const char *classname)
{
	bot_weapon_select_t *pSel = WeaponGetSelectPointer();
	if (pSel == NULL || classname == NULL) return 0;
	int i = 0;
	while (pSel[i].iId)
	{
		if (strcmp(pSel[i].weapon_name, classname) == 0)
			return pSel[i].iId;
		i++;
	}
	return 0;
}

static int BotLootWeaponPriority(int iId)
{
	if (iId <= 0) return 999;
	int idx = WeaponGetSelectIndex(iId);
	if (idx < 0) return 999;
	return WeaponGetSelectPointer()[idx].priority;
}

// True when the held weapon has a primary-ammo type and the bot's
// reserves are empty (and the clip is too, if it happens to be the
// active weapon).  Melee / exhaustible weapons with no ammo index
// (iAmmo1 == -1) are never considered out.
static bool BotLootIsHeldOutOfAmmo(bot_t *pBot, int iId)
{
	if (iId <= 0 || iId >= MAX_WEAPONS) return false;
	int ammoIdx = weapon_defs[iId].iAmmo1;
	if (ammoIdx < 0) return false;
	if (pBot->m_rgAmmo[ammoIdx] > 0) return false;
	if (pBot->current_weapon.iId == iId && pBot->current_weapon.iClip > 0)
		return false;
	return true;
}

// Worst (highest priority value) non-fists weapon currently held —
// this is the slot we'd be giving up to swap.
static int BotLootWorstHeldPriority(bot_t *pBot)
{
	bot_weapon_select_t *pSel = WeaponGetSelectPointer();
	int worst = -1;
	int i = 0;
	while (pSel[i].iId)
	{
		if (pSel[i].iId != VALVE_WEAPON_FISTS
			&& UTIL_HasWeaponId(pBot->pEdict, pSel[i].iId))
		{
			// Out-of-ammo weapons rank as worst-possible so any
			// candidate beats them in the at-cap swap gate.
			int p = BotLootIsHeldOutOfAmmo(pBot, pSel[i].iId)
				? 999 : pSel[i].priority;
			if (p > worst) worst = p;
		}
		i++;
	}
	return worst;
}

// Classname of the worst-priority non-fists weapon currently held.
// Used to drop a specific weapon by name so the drop succeeds even
// when the bot's active item is fists (fists has ITEM_FLAG_NODROP).
// Out-of-ammo weapons are preferred targets regardless of priority:
// a dry shotgun is more disposable than a loaded MAC-10 even if the
// shotgun has the better table priority.
static const char *BotLootGetWorstHeldName(bot_t *pBot)
{
	bot_weapon_select_t *pSel = WeaponGetSelectPointer();

	// First pass: any out-of-ammo non-fists weapon wins outright.
	int i = 0;
	while (pSel[i].iId)
	{
		if (pSel[i].iId != VALVE_WEAPON_FISTS
			&& UTIL_HasWeaponId(pBot->pEdict, pSel[i].iId)
			&& BotLootIsHeldOutOfAmmo(pBot, pSel[i].iId))
			return pSel[i].weapon_name;
		i++;
	}

	// Fallback: highest-priority-value (worst by table priority).
	const char *name = NULL;
	int worst = -1;
	i = 0;
	while (pSel[i].iId)
	{
		if (pSel[i].iId != VALVE_WEAPON_FISTS
			&& UTIL_HasWeaponId(pBot->pEdict, pSel[i].iId))
		{
			if (pSel[i].priority > worst)
			{
				worst = pSel[i].priority;
				name  = pSel[i].weapon_name;
			}
		}
		i++;
	}
	return name;
}

// Best (lowest priority value) non-fists weapon currently held.
// Returns classname or NULL when bot only has fists.
static const char *BotLootGetBestHeldName(bot_t *pBot)
{
	bot_weapon_select_t *pSel = WeaponGetSelectPointer();
	const char *name = NULL;
	int best = 999;
	int i = 0;
	while (pSel[i].iId)
	{
		if (pSel[i].iId != VALVE_WEAPON_FISTS
			&& UTIL_HasWeaponId(pBot->pEdict, pSel[i].iId))
		{
			if (pSel[i].priority < best)
			{
				best = pSel[i].priority;
				name = pSel[i].weapon_name;
			}
		}
		i++;
	}
	return name;
}

static int BotLootCountNonFistsHeld(bot_t *pBot)
{
	bot_weapon_select_t *pSel = WeaponGetSelectPointer();
	int count = 0;
	int i = 0;
	while (pSel[i].iId)
	{
		if (pSel[i].iId != VALVE_WEAPON_FISTS
			&& UTIL_HasWeaponId(pBot->pEdict, pSel[i].iId))
			count++;
		i++;
	}
	return count;
}

// Mirrors CHalfLifeLoot::CanHavePlayerItem's loot-advantage check.
static bool BotLootHasTeamAdvantage(bot_t *pBot)
{
	edict_t *pEdict = pBot->pEdict;
	if (pBot->b_loot_has_loot) return true;
	int botTeam = UTIL_GetTeam(pEdict);
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		edict_t *pPl = INDEXENT(i);
		if (FNullEnt(pPl) || pPl == pEdict) continue;
		if (!IsAlive(pPl)) continue;
		if (UTIL_GetTeam(pPl) != botTeam) continue;
		if ((int)pPl->v.fuser4 == RADAR_LOOT_VAL) return true;
	}
	return false;
}

// Sentinel priority used for weaponbox entities (dropped weapons whose
// contents the bot DLL can't introspect from server private state).
// Pegged to 50 so weapon_* entries (priorities ~1-10) always rank
// better when both are in range; weaponboxes only get picked when no
// real weapon is around.
#define LOOT_WEAPONBOX_PRIORITY 50

// True when the entity is a weapon on the ground (no owner) we could
// pick up.  Outputs the bot-recognized iId and the weapon's priority.
// For weaponbox (dropped guns), iId is 0 (unknown contents).
static bool BotLootIsPickupableWeapon(edict_t *pWeapon, int *outId, int *outPriority)
{
	if (FNullEnt(pWeapon) || pWeapon->free) return false;
	const char *cn = STRING(pWeapon->v.classname);

	// Dropped weapons become weaponbox entities (CWeaponBox).  Bot DLL
	// can't see the packed weapon's iId without server-private types,
	// so treat as unknown contents — still worth detouring when we
	// have a free slot.
	if (strcmp(cn, "weaponbox") == 0)
	{
		if (pWeapon->v.effects & EF_NODRAW) return false;
		if (outId)       *outId = 0;
		if (outPriority) *outPriority = LOOT_WEAPONBOX_PRIORITY;
		return true;
	}

	if (strncmp(cn, "weapon_", 7) != 0) return false;
	if (strcmp(cn, "weapon_fists") == 0) return false;
	// Currently-held weapons have a non-null owner.
	if (!FNullEnt(pWeapon->v.owner)) return false;
	if (pWeapon->v.effects & EF_NODRAW) return false;
	int iId = BotLootWeaponClassToId(cn);
	if (iId <= 0) return false;
	int prio = BotLootWeaponPriority(iId);
	if (prio >= 999) return false;
	if (outId)       *outId = iId;
	if (outPriority) *outPriority = prio;
	return true;
}

// Find the best (lowest-priority value) nearby weapon worth detouring
// for.  Worthwhile means: (a) the bot has no weapon, (b) under the cap,
// or (c) at the cap but the candidate's priority strictly beats the
// worst weapon already held (strict to avoid swap thrashing on ties).
static edict_t *BotLootFindUpgradeWeapon(bot_t *pBot)
{
	edict_t *pEdict = pBot->pEdict;
	int held_count = BotLootCountNonFistsHeld(pBot);
	int held_worst = BotLootWorstHeldPriority(pBot);
	int max_held   = BotLootHasTeamAdvantage(pBot) ? 3 : 1;

	int      best_prio = 999;
	edict_t *pBest     = NULL;

	edict_t *pE = NULL;
	while ((pE = UTIL_FindEntityInSphere(pE, pEdict->v.origin,
	                                     LOOT_WEAPON_SCAN_RADIUS)) != NULL)
	{
		int iId, prio;
		if (!BotLootIsPickupableWeapon(pE, &iId, &prio)) continue;
		// Skip duplicates we already hold (iId==0 means weaponbox of
		// unknown contents — can't dedupe, just try it).
		if (iId != 0 && UTIL_HasWeaponId(pEdict, iId)) continue;

		bool worthwhile;
		if (iId == 0)
		{
			// Weaponbox contents unknown — only chase when we have a
			// free slot.  Never swap (could be worse than what we hold).
			worthwhile = (held_count < max_held);
		}
		else if (held_count == 0)              worthwhile = true;
		else if (held_count < max_held)   worthwhile = true;
		else                              worthwhile = (prio < held_worst);
		if (!worthwhile) continue;

		if (prio < best_prio)
		{
			best_prio = prio;
			pBest     = pE;
		}
	}
	return pBest;
}

// At-cap swap: when within touch range of the upgrade target, issue
// the "drop" client command — server drops the active weapon and the
// upgrade picks up on the next Touch frame via standard pickup logic.
static void BotLootMaybeDropForSwap(bot_t *pBot, edict_t *pTarget)
{
	if (FNullEnt(pTarget) || pTarget->free) return;
	if (gpGlobals->time < pBot->f_loot_weapon_drop_cooldown) return;

	edict_t *pEdict = pBot->pEdict;
	int held_count = BotLootCountNonFistsHeld(pBot);
	int max_held   = BotLootHasTeamAdvantage(pBot) ? 3 : 1;
	if (held_count < max_held) return;  // touch-pickup handles under-cap

	float dist = (pTarget->v.origin - pEdict->v.origin).Length();
	if (dist > LOOT_WEAPON_SWAP_RANGE) return;

	// Drop by name — DropPlayerItem matches the arg against weapon
	// classnames.  We pass the WORST-priority non-fists weapon so the
	// drop succeeds even when the bot's active item is fists (fists
	// has ITEM_FLAG_NODROP and would silently fail an empty-arg drop).
	const char *pszDrop = BotLootGetWorstHeldName(pBot);
	if (pszDrop == NULL) return;
	FakeClientCommand(pEdict, "drop", (char *)pszDrop, NULL);
	pBot->f_loot_weapon_drop_cooldown = gpGlobals->time + LOOT_WEAPON_DROP_COOLDOWN;
}

// When the bot holds a non-fists weapon but is wielding fists, force
// a switch to the best non-fists weapon.  HL's auto-switch on pickup
// doesn't always engage for bots, leaving them punching enemies past
// a perfectly good MAC-10 in their inventory.
static void BotLootMaybeWieldNonFists(bot_t *pBot)
{
	edict_t *pEdict = pBot->pEdict;
	if (FNullEnt(pEdict)) return;
	if (pBot->current_weapon.iId != VALVE_WEAPON_FISTS) return;
	const char *pszBest = BotLootGetBestHeldName(pBot);
	if (pszBest == NULL) return;
	UTIL_SelectItem(pEdict, (char *)pszBest);
}

// Proactively drop any held non-fists weapon whose reserves are
// exhausted.  Frees the inventory slot so the next weapon pickup
// succeeds even at the cap, and lets the bot fall back to fists
// (better than carrying dead weight).  Cooldowned to avoid spam.
static void BotLootDropEmptyWeapons(bot_t *pBot)
{
	if (gpGlobals->time < pBot->f_loot_weapon_drop_cooldown) return;

	edict_t *pEdict = pBot->pEdict;
	bot_weapon_select_t *pSel = WeaponGetSelectPointer();
	int i = 0;
	while (pSel[i].iId)
	{
		if (pSel[i].iId != VALVE_WEAPON_FISTS
			&& UTIL_HasWeaponId(pEdict, pSel[i].iId)
			&& BotLootIsHeldOutOfAmmo(pBot, pSel[i].iId))
		{
			FakeClientCommand(pEdict, "drop",
			                  (char *)pSel[i].weapon_name, NULL);
			pBot->f_loot_weapon_drop_cooldown =
				gpGlobals->time + LOOT_WEAPON_DROP_COOLDOWN;
			return;
		}
		i++;
	}
}

// Public entry: returns true and fills outGoal when the bot should
// detour to grab a weapon.  Never deviates while carrying the loot.
static bool BotLootHandleWeaponSwap(bot_t *pBot, Vector *outGoal)
{
	if (pBot == NULL || pBot->pEdict == NULL) return false;
	if (pBot->b_loot_has_loot) return false;

	// Validate cached target first; rescan on cadence if invalid.
	edict_t *pTarget = NULL;
	if (pBot->i_loot_weapon_target_index > 0)
	{
		edict_t *pC = INDEXENT(pBot->i_loot_weapon_target_index);
		int id, pr;
		if (!FNullEnt(pC) && !pC->free
			&& BotLootIsPickupableWeapon(pC, &id, &pr)
			&& (id == 0 || !UTIL_HasWeaponId(pBot->pEdict, id)))
			pTarget = pC;
		else
			pBot->i_loot_weapon_target_index = 0;
	}
	if (pTarget == NULL && pBot->f_loot_weapon_eval_time < gpGlobals->time)
	{
		pBot->f_loot_weapon_eval_time = gpGlobals->time + LOOT_WEAPON_EVAL_CADENCE;
		pTarget = BotLootFindUpgradeWeapon(pBot);
		pBot->i_loot_weapon_target_index = pTarget ? ENTINDEX(pTarget) : 0;
	}
	if (pTarget == NULL) return false;

	BotLootMaybeDropForSwap(pBot, pTarget);

	if (outGoal) *outGoal = pTarget->v.origin;
	return true;
}

//=========================================================
// BotLootPreUpdate — called BEFORE BotFindEnemy every frame.
// Sets b_loot_has_loot and pre-seeds v_goal so the movement
// block always has a target on ticks where the enemy branch
// runs instead of BotLootThink.
//=========================================================
void BotLootPreUpdate( bot_t *pBot )
{
	edict_t *pEdict = pBot->pEdict;

	BotLootFindEntities();

	pBot->b_loot_has_loot = ((int)pEdict->v.fuser4 == RADAR_LOOT_VAL);

	// Get off fists whenever we hold a real weapon -- prevents bots
	// from running around punching when they're carrying an MP5.
	BotLootMaybeWieldNonFists(pBot);

	// Dump any empty non-fists weapons so we're not holding dead
	// weight that blocks future pickups.
	BotLootDropEmptyWeapons(pBot);

	if (pBot->b_loot_has_loot)
	{
		pBot->pBotPickupItem = NULL;
		pBot->item_waypoint  = -1;
		if (!FNullEnt(s_pLootGoal))
		{
			pBot->v_goal           = s_pLootGoal->v.origin;
			pBot->f_goal_proximity = 0.0f;
		}
		return;
	}

	// Loose loot_entity always wins — a freshly-broken crate's loot
	// is the single most valuable pickup on the map.  Forcing role +
	// goal here (instead of waiting for the 0.75s role-eval cadence)
	// stops bots from running past exposed loot while still cached as
	// BREAKER/RECOVERER/ESCORT from the previous tick.
	if (!FNullEnt(s_pLootEntity))
	{
		pBot->i_loot_role      = LOOT_ROLE_GRABBER;
		pBot->pBotPickupItem   = NULL;
		pBot->item_waypoint    = -1;
		pBot->v_goal           = s_pLootEntity->v.origin;
		pBot->f_goal_proximity = 0.0f;
		return;
	}

	// Weapon-swap detour overrides any role-driven goal: a clearly
	// better gun on the floor is worth a brief pivot even mid-escort.
	Vector vWeaponGoal;
	if (BotLootHandleWeaponSwap(pBot, &vWeaponGoal))
	{
		pBot->pBotPickupItem   = NULL;
		pBot->item_waypoint    = -1;
		pBot->v_goal           = vWeaponGoal;
		pBot->f_goal_proximity = 32.0f;
		return;
	}

	int botTeam = UTIL_GetTeam(pEdict);
	edict_t *pHolder = BotLootGetHolder();

	switch (pBot->i_loot_role)
	{
	case LOOT_ROLE_RECOVERER:
		if (pHolder && UTIL_GetTeam(pHolder) != botTeam)
		{
			pBot->v_goal           = pHolder->v.origin;
			pBot->f_goal_proximity = 0.0f;
		}
		break;
	case LOOT_ROLE_ESCORT:
		if (pHolder && UTIL_GetTeam(pHolder) == botTeam && pHolder != pEdict)
		{
			// Flank offset — match BotLootThink ESCORT.  Avoids escort
			// stacking on the carrier and blocking forward motion.
			Vector vForward;
			if (!FNullEnt(s_pLootGoal))
				vForward = s_pLootGoal->v.origin - pHolder->v.origin;
			else
			{
				MAKE_VECTORS(pHolder->v.v_angle);
				vForward = gpGlobals->v_forward;
			}
			vForward.z = 0;
			if (vForward.Length() < 1.0f) vForward = Vector(1, 0, 0);
			else vForward = vForward.Normalize();
			Vector vSide((ENTINDEX(pEdict) & 1) ? -vForward.y :  vForward.y,
			             (ENTINDEX(pEdict) & 1) ?  vForward.x : -vForward.x,
			             0);
			pBot->v_goal           = pHolder->v.origin + vSide * 96.0f - vForward * 32.0f;
			pBot->f_goal_proximity = LOOT_ESCORT_PROXIMITY;
		}
		break;
	case LOOT_ROLE_GRABBER:
		if (!FNullEnt(s_pLootEntity))
		{
			pBot->v_goal           = s_pLootEntity->v.origin;
			pBot->f_goal_proximity = 0.0f;
		}
		break;
	case LOOT_ROLE_BREAKER:
	default:
	{
		edict_t *pCrate = BotLootPickNearestCrate(pBot);
		if (pCrate)
		{
			pBot->v_goal           = pCrate->v.origin;
			pBot->f_goal_proximity = 64.0f;
		}
		break;
	}
	}
}

//=========================================================
// BotLootThink — main loot AI dispatch.  Returns true when
// movement intent has been set.
//=========================================================
bool BotLootThink( bot_t *pBot )
{
	if (is_gameplay != GAME_LOOT)
		return false;

	edict_t *pEdict = pBot->pEdict;
	int botTeam = UTIL_GetTeam(pEdict);

	BotLootFindEntities();

	pBot->b_loot_has_loot = ((int)pEdict->v.fuser4 == RADAR_LOOT_VAL);
	edict_t *pHolder = BotLootGetHolder();

	// Weapon-swap detour: short-circuit the role switch when an
	// upgrade weapon is nearby.  Suppressed while carrying loot
	// (handled by BotLootHandleWeaponSwap itself).
	{
		Vector vWeaponGoal;
		if (BotLootHandleWeaponSwap(pBot, &vWeaponGoal))
		{
			pBot->pBotPickupItem   = NULL;
			pBot->item_waypoint    = -1;
			pBot->v_goal           = vWeaponGoal;
			pBot->f_goal_proximity = 32.0f;
			pBot->f_move_speed     = pBot->f_max_speed;
			return true;
		}
	}

	// Role evaluation
	if (pBot->b_loot_has_loot)
	{
		pBot->i_loot_role = LOOT_ROLE_CARRIER;
	}
	else if (!FNullEnt(s_pLootEntity))
	{
		// Loose loot ALWAYS forces GRABBER — don't wait for the
		// 0.75s eval cadence (bot would run past exposed loot in
		// the meantime while still cached as BREAKER/ESCORT/etc).
		pBot->i_loot_role = LOOT_ROLE_GRABBER;
	}
	else if (pBot->f_loot_role_eval_time < gpGlobals->time
	         || pBot->i_loot_role == LOOT_ROLE_CARRIER)
	{
		pBot->f_loot_role_eval_time = gpGlobals->time + LOOT_ROLE_EVAL_CADENCE;

		// Count living teammates other than self.  When the bot is
		// the sole survivor on its team, defensive roles (ESCORT)
		// degenerate into back-and-forth pacing — there's nobody
		// to defend or trail.  Force a forward role instead.
		int aliveMates = 0;
		for (int pi = 1; pi <= gpGlobals->maxClients; pi++)
		{
			edict_t *pPl = INDEXENT(pi);
			if (FNullEnt(pPl) || pPl == pEdict) continue;
			if (!IsAlive(pPl)) continue;
			if (UTIL_GetTeam(pPl) != botTeam) continue;
			aliveMates++;
		}
		bool bSoleSurvivor = (aliveMates == 0);

		if (pHolder && UTIL_GetTeam(pHolder) != botTeam)
			pBot->i_loot_role = LOOT_ROLE_RECOVERER;
		else if (pHolder && UTIL_GetTeam(pHolder) == botTeam && pHolder != pEdict
		         && !bSoleSurvivor)
			pBot->i_loot_role = LOOT_ROLE_ESCORT;
		else if (!FNullEnt(s_pLootEntity))
			pBot->i_loot_role = LOOT_ROLE_GRABBER;
		else
			pBot->i_loot_role = LOOT_ROLE_BREAKER;
	}

	switch (pBot->i_loot_role)
	{
	// CARRIER — run to goal; pacified above 25 HP
	case LOOT_ROLE_CARRIER:
	{
		if (FNullEnt(s_pLootGoal))
			return false;

		pBot->f_pause_time = 0;
		pBot->f_move_speed = pBot->f_max_speed;
		pBot->pBotPickupItem = NULL;
		pBot->item_waypoint  = -1;

		pBot->v_goal           = s_pLootGoal->v.origin;
		pBot->f_goal_proximity = 0.0f;

		// Goal-priority: when the loot_goal is visible within ~1500u
		// (call it the carrier's "deposit radius"), drop ALL combat
		// engagement and sprint for the score regardless of HP.
		// Teammates handle escort.  Outside that radius we fall back
		// to the standard pacify-above-25-HP rule so a low-HP carrier
		// still defends itself when the goal is far / occluded.
		float goalDist = (s_pLootGoal->v.origin - pEdict->v.origin).Length();
		bool goalVisible = (goalDist < 1500.0f
			&& FVisible(s_pLootGoal->v.origin, pEdict));

		if (goalVisible)
		{
			pBot->pBotEnemy = NULL;
		}
		else if (pEdict->v.health > LOOT_CARRIER_PACIFY_HP && pBot->pBotEnemy)
		{
			pBot->pBotEnemy = NULL;
		}

		// Carrier anti-stuck: if a teammate (or any other live
		// player) parks themselves between us and the goal we'd
		// otherwise just stand still pushing into them.  Sample
		// distance-to-goal every 0.4s; if it hasn't decreased by
		// at least 8u we're stalled.  After 0.6s of stall, force
		// IN_JUMP and alternate strafe direction so the carrier
		// hops over or sidesteps the blocker.  After 1.2s of stall,
		// hand off to BotGoalElevatedJump's multi-jump combo so we
		// triple-jump clean over them.
		if (pBot->f_loot_carrier_check_time < gpGlobals->time)
		{
			if (pBot->f_loot_carrier_check_time == 0.0f
				|| pBot->f_loot_carrier_last_dist - goalDist >= 8.0f)
			{
				pBot->f_loot_carrier_stuck_since = 0.0f;
			}
			else if (pBot->f_loot_carrier_stuck_since == 0.0f)
			{
				pBot->f_loot_carrier_stuck_since = gpGlobals->time;
			}
			pBot->f_loot_carrier_last_dist  = goalDist;
			pBot->f_loot_carrier_check_time = gpGlobals->time + 0.4f;
		}

		if (pBot->f_loot_carrier_stuck_since > 0.0f)
		{
			float stuckDur = gpGlobals->time - pBot->f_loot_carrier_stuck_since;

			// Detect a player blocker within 80u in our forward arc.
			edict_t *pBlocker = NULL;
			for (int i = 1; i <= gpGlobals->maxClients; i++)
			{
				edict_t *pPl = INDEXENT(i);
				if (FNullEnt(pPl) || pPl->free) continue;
				if (pPl == pEdict) continue;
				if (!(pPl->v.flags & FL_CLIENT)) continue;
				if (!IsAlive(pPl)) continue;
				Vector vDelta = pPl->v.origin - pEdict->v.origin;
				if (vDelta.Length() > 80.0f) continue;
				if (!FInViewCone(&pPl->v.origin, pEdict)) continue;
				pBlocker = pPl;
				break;
			}

			if (stuckDur >= 0.6f)
			{
				pEdict->v.button |= IN_JUMP;
				// Alternate strafe direction by entindex parity so two
				// adjacent stuck bots peel apart instead of mirroring.
				pBot->f_strafe_speed = (ENTINDEX(pEdict) & 1)
					? pBot->f_max_speed : -pBot->f_max_speed;
				pBot->f_move_speed   = pBot->f_max_speed;
			}

			// Heavier escalation: if we've been stuck > 1.2s and a
			// blocker is in front, force the multi-jump combo so we
			// clear them via double/triple-jump.
			if (stuckDur >= 1.2f && pBlocker && (pEdict->v.flags & FL_ONGROUND))
			{
				BotGoalElevatedJump(pBot, s_pLootGoal->v.origin, true /*force*/);
				// Reset the stall timer so the combo gets a fair window
				// to deliver progress before we re-trigger.
				pBot->f_loot_carrier_stuck_since = 0.0f;
				pBot->f_loot_carrier_check_time  = gpGlobals->time + 0.6f;
			}
		}

		BotGoalElevatedJump(pBot, s_pLootGoal->v.origin);
		return true;
	}

	// RECOVERER — force-target the enemy carrier
	case LOOT_ROLE_RECOVERER:
	{
		if (!pHolder || UTIL_GetTeam(pHolder) == botTeam)
		{
			pBot->i_loot_role = LOOT_ROLE_NONE;
			return false;
		}

		pBot->f_pause_time = 0;
		pBot->f_move_speed = pBot->f_max_speed;

		// Override default frag target with the carrier when in LoS
		if (FInViewCone(&pHolder->v.origin, pEdict) && FVisible(pHolder->v.origin, pEdict))
			pBot->pBotEnemy = pHolder;

		pBot->v_goal           = pHolder->v.origin;
		pBot->f_goal_proximity = 0.0f;

		BotGoalElevatedJump(pBot, pHolder->v.origin);
		return true;
	}

	// ESCORT — trail teammate carrier on a flank, NOT directly on top
	// of them.  Walking straight to the carrier's origin causes pile-ups
	// where the carrier ends up jumping in place against a teammate
	// blocker.  Compute a flank offset perpendicular to the carrier's
	// path toward the loot_goal (or the carrier's facing as fallback)
	// and split escorts left/right by entindex parity.  Escort proximity
	// stays loose so they don't crowd the carrier.
	case LOOT_ROLE_ESCORT:
	{
		if (!pHolder || UTIL_GetTeam(pHolder) != botTeam || pHolder == pEdict)
		{
			pBot->i_loot_role = LOOT_ROLE_NONE;
			return false;
		}

		Vector vForward;
		if (!FNullEnt(s_pLootGoal))
			vForward = s_pLootGoal->v.origin - pHolder->v.origin;
		else
		{
			Vector vCarrierAngles = pHolder->v.v_angle;
			if ((vCarrierAngles.x == 0) && (vCarrierAngles.y == 0) && (vCarrierAngles.z == 0))
				vCarrierAngles = pHolder->v.angles;

			MAKE_VECTORS(vCarrierAngles);
			vForward = gpGlobals->v_forward;
		}
		vForward.z = 0;
		if (vForward.Length() < 1.0f)
			vForward = Vector(1, 0, 0);
		else
			vForward = vForward.Normalize();

		// Perpendicular flank vector.  Parity flips left/right.
		Vector vSide((ENTINDEX(pEdict) & 1) ? -vForward.y :  vForward.y,
		             (ENTINDEX(pEdict) & 1) ?  vForward.x : -vForward.x,
		             0);
		Vector vEscortPos = pHolder->v.origin + vSide * 96.0f - vForward * 32.0f;

		pBot->f_move_speed     = pBot->f_max_speed;
		pBot->v_goal           = vEscortPos;
		pBot->f_goal_proximity = LOOT_ESCORT_PROXIMITY;
		return true;
	}

	// GRABBER — loose loot, override waypoints
	case LOOT_ROLE_GRABBER:
	{
		if (FNullEnt(s_pLootEntity))
		{
			pBot->i_loot_role = LOOT_ROLE_NONE;
			return false;
		}

		pBot->f_pause_time = 0;
		pBot->f_move_speed = pBot->f_max_speed;

		pBot->v_goal           = s_pLootEntity->v.origin;
		pBot->f_goal_proximity = 0.0f;

		// Bypass waypoint detours while we can see the loose loot
		if (FInViewCone(&s_pLootEntity->v.origin, pEdict)
			&& FVisible(s_pLootEntity->v.origin, pEdict))
		{
			pBot->f_ignore_wpt_time = gpGlobals->time + 0.5f;
		}

		BotGoalElevatedJump(pBot, s_pLootEntity->v.origin);
		return true;
	}

	// BREAKER — seek and shoot the nearest crate
	case LOOT_ROLE_BREAKER:
	default:
	{
		edict_t *pCrate = BotLootPickNearestCrate(pBot);
		if (FNullEnt(pCrate))
			return false;

		pBot->f_move_speed     = pBot->f_max_speed;
		pBot->v_goal           = pCrate->v.origin;
		pBot->f_goal_proximity = 64.0f;

		float dist = (pCrate->v.origin - pEdict->v.origin).Length();
		if (dist < LOOT_BREAKER_FIRE_DIST
			&& FInViewCone(&pCrate->v.origin, pEdict)
			&& FVisible(pCrate->v.origin, pEdict)
			&& pBot->pBotEnemy == NULL)
		{
			pBot->pBotEnemy = pCrate;
		}

		// On top of the crate — the always-runs handler in
		// BotLootHandleOnTopOfCrate (called from bot.cpp) is now
		// authoritative.  Do nothing here; the handler will have
		// already taken control before we reached this branch.

		// Elevated crate: when the cached crate is significantly above
		// the bot and we're horizontally close, hand off to the multi-
		// jump helper so the bot double/triple-jumps up to it (mirrors
		// CTF flag pursuit / loot goal).  Ground-level crates do NOT
		// trigger this branch — the helper's 20u heightDiff gate plus
		// our explicit 32u guard keep the bot from leaping onto floor
		// crates and entering the weapon-thrash loop fixed earlier.
		float heightDiff = pCrate->v.origin.z - pEdict->v.origin.z;
		Vector vecFlat = pCrate->v.origin - pEdict->v.origin;
		vecFlat.z = 0;
		float horzDist = vecFlat.Length();
		if (heightDiff > 32.0f && horzDist < 200.0f)
		{
			BotGoalElevatedJump(pBot, pCrate->v.origin);
		}
		return true;
	}
	}
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
// Busters — per-frame cached entity scan.
//
// Caches the live Buster (the one player on team "busters",
// signalled by pev->fuser4 > 0) and the nearest dropped
// weaponbox on the map.  When the Buster is dead, the egon
// drops as a weaponbox (gamerules cannot re-grant to raw
// weapon_egon entities).  Ghost bots race the nearest
// weaponbox during that window — whoever touches the egon
// weaponbox becomes the new Buster via PlayerGotWeapon.
//=========================================================
static float    s_busters_cache_time = -1.0f;
static edict_t *s_pBuster = NULL;
static edict_t *s_pBusterWeaponbox = NULL;

static void BotBustersFindEntities()
{
	if (s_busters_cache_time == gpGlobals->time)
		return;
	s_busters_cache_time = gpGlobals->time;

	s_pBuster = NULL;
	s_pBusterWeaponbox = NULL;

	// Find the live Buster (fuser4 > 0 and on a client edict)
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		edict_t *pPlayer = INDEXENT(i);
		if (FNullEnt(pPlayer) || !IsAlive(pPlayer))
			continue;
		if (!(pPlayer->v.flags & (FL_CLIENT | FL_FAKECLIENT)))
			continue;
		if (pPlayer->v.fuser4 > 0)
		{
			s_pBuster = pPlayer;
			break;
		}
	}

	// When the Buster is missing, the egon is loose.  Capture the
	// first weaponbox on the map — gamerules guarantees there is at
	// most one egon in play, and ghost-dropped weaponboxes are also
	// useful pickups for hunters.  Prefer the freshest one by
	// choosing the one with the earliest next-think (most recently
	// dropped) when multiple exist.
	if (!s_pBuster)
	{
		edict_t *pScan = NULL;
		float flBestAge = 9e9f;
		while ((pScan = UTIL_FindEntityByClassname(pScan, "weaponbox")) != NULL)
		{
			if (FNullEnt(pScan) || pScan->free)
				continue;
			if (pScan->v.effects & EF_NODRAW)
				continue;
			// nextthink holds the dissolve timer; earlier = fresher drop
			float age = pScan->v.nextthink;
			if (age < flBestAge)
			{
				flBestAge = age;
				s_pBusterWeaponbox = pScan;
			}
		}
	}
}

//=========================================================
// BotBustersPreUpdate — called from BotThink BEFORE
// BotFindEnemy every frame.  Refreshes entity cache and
// pre-sets v_goal so the movement block has a target even
// on ticks where the enemy branch runs instead of
// BotBustersThink.
//=========================================================
void BotBustersPreUpdate( bot_t *pBot )
{
	edict_t *pEdict = pBot->pEdict;

	BotBustersFindEntities();

	// Reset egon-grab stuck detection whenever the weaponbox isn't the
	// active goal so stale samples don't trigger spurious jumps later.
	if (!s_pBusterWeaponbox)
	{
		pBot->f_busters_stuck_check_time = 0.0f;
		pBot->f_busters_stuck_since      = 0.0f;
	}

	bool botIsBuster = (pEdict->v.fuser4 > 0);

	if (botIsBuster)
	{
		// Buster can't pick anything up — clear pickup pointer every frame.
		pBot->pBotPickupItem = NULL;
		pBot->item_waypoint  = -1;
		pBot->i_engage_aggressiveness = 100;

		// Pre-set v_goal to nearest ghost so the movement block has a
		// target even when BotFindEnemy claims this frame.
		edict_t *pTarget = NULL;
		float flBest = 9e9f;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			edict_t *p = INDEXENT(i);
			if (FNullEnt(p) || p == pEdict)
				continue;
			if (!IsAlive(p))
				continue;
			if (!(p->v.flags & (FL_CLIENT | FL_FAKECLIENT)))
				continue;
			if (p->v.fuser4 > 0)
				continue; // skip other busters (shouldn't happen, safety)
			float d = (p->v.origin - pEdict->v.origin).Length();
			if (d < flBest) { flBest = d; pTarget = p; }
		}
		if (pTarget)
		{
			pBot->v_goal           = pTarget->v.origin;
			pBot->f_goal_proximity = 64.0f;
		}
		return;
	}

	// Ghost: pursue buster if alive, else race for dropped weaponbox.
	if (s_pBuster)
	{
		pBot->v_busters_last_seen      = s_pBuster->v.origin;
		pBot->f_busters_last_seen_time = gpGlobals->time;
		pBot->v_goal                   = s_pBuster->v.origin;
		pBot->f_goal_proximity         = 96.0f;
	}
	else if (s_pBusterWeaponbox)
	{
		pBot->v_goal           = s_pBusterWeaponbox->v.origin;
		pBot->f_goal_proximity = 20.0f;
		// Suppress generic item detours only when the grab target exists.
		pBot->pBotPickupItem = NULL;
		pBot->item_waypoint  = -1;
	}
	else if (pBot->f_busters_last_seen_time > 0
		&& gpGlobals->time - pBot->f_busters_last_seen_time < 6.0f)
	{
		// Fall back to the Buster's last-known origin briefly.
		pBot->v_goal           = pBot->v_busters_last_seen;
		pBot->f_goal_proximity = 96.0f;
	}
}

//=========================================================
// BotBustersThink — called from BotThink when in Busters
// mode and no combat is active.
//
// Priority hierarchy:
//  1. Bot IS the Buster → hunt nearest ghost with the egon.
//  2. Buster is alive (opponent) → pursue them; juke while
//     en-route so two looping bots break their stalemate.
//  3. Buster is missing & weaponbox is loose → race to it
//     (use the elevated-jump helper for pedestals).
//  4. Nothing in play → fall back to normal nav.
//
// Returns true when movement intent has been set; false to
// fall back to normal nav/combat.
//=========================================================
bool BotBustersThink( bot_t *pBot )
{
	if (is_gameplay != GAME_BUSTERS)
		return false;

	edict_t *pEdict = pBot->pEdict;

	BotBustersFindEntities();

	// Per-bot pace oscillation — re-roll every 1.5-4s so two bots
	// running the same waypoint loop don't orbit at identical speed.
	if (pBot->f_busters_pace_time < gpGlobals->time)
	{
		pBot->f_busters_pace_scale = RANDOM_FLOAT(0.65f, 1.0f);
		pBot->f_busters_pace_time  = gpGlobals->time + RANDOM_FLOAT(1.5f, 4.0f);
	}

	bool botIsBuster = (pEdict->v.fuser4 > 0);

	// -----------------------------------------------------------------
	// Case 1: Bot IS the Buster — hunt nearest ghost with the egon.
	// -----------------------------------------------------------------
	if (botIsBuster)
	{
		pBot->i_busters_role          = BUSTERS_ROLE_BUSTER;
		pBot->f_pause_time            = 0;
		pBot->pBotPickupItem          = NULL;
		pBot->item_waypoint           = -1;
		pBot->i_engage_aggressiveness = 100;

		edict_t *pTarget = NULL;
		float flBest = 9e9f;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			edict_t *p = INDEXENT(i);
			if (FNullEnt(p) || p == pEdict)
				continue;
			if (!IsAlive(p))
				continue;
			if (!(p->v.flags & (FL_CLIENT | FL_FAKECLIENT)))
				continue;
			if (p->v.fuser4 > 0)
				continue;
			float d = (p->v.origin - pEdict->v.origin).Length();
			if (d < flBest) { flBest = d; pTarget = p; }
		}

		if (pTarget)
		{
			pBot->v_goal           = pTarget->v.origin;
			pBot->f_goal_proximity = 64.0f;
			pBot->f_move_speed     = pBot->f_max_speed;

			Vector vecDir    = pTarget->v.origin - pEdict->v.origin;
			Vector vecAngles = UTIL_VecToAngles(vecDir);
			pEdict->v.ideal_yaw = vecAngles.y;
			BotFixIdealYaw(pEdict);
			return true;
		}

		// No ghosts visible yet — keep moving, waypoint nav will route.
		pBot->f_move_speed = pBot->f_max_speed;
		return false;
	}

	// -----------------------------------------------------------------
	// Case 2: Buster is alive — pursue them.
	// -----------------------------------------------------------------
	if (s_pBuster)
	{
		pBot->i_busters_role = BUSTERS_ROLE_GHOST_HUNTER;
		if (pBot->i_engage_aggressiveness < 85)
			pBot->i_engage_aggressiveness = 85;

		pBot->v_busters_last_seen      = s_pBuster->v.origin;
		pBot->f_busters_last_seen_time = gpGlobals->time;

		pBot->v_goal           = s_pBuster->v.origin;
		pBot->f_goal_proximity = 96.0f;
		pBot->f_move_speed     = pBot->f_max_speed * pBot->f_busters_pace_scale;

		// Anti-stalemate jukes while hunting (no enemy currently engaged)
		if (!pBot->pBotEnemy && pBot->f_busters_juke_time < gpGlobals->time)
		{
			float r = RANDOM_FLOAT(0.0f, 1.0f);
			if (r < 0.25f)
				pEdict->v.button |= IN_JUMP;
			else if (r < 0.45f)
				pEdict->v.button |= IN_DUCK;
			else if (r < 0.60f)
				pEdict->v.button |= (RANDOM_LONG(0, 1) ? IN_MOVELEFT : IN_MOVERIGHT);
			pBot->f_busters_juke_time = gpGlobals->time + RANDOM_FLOAT(0.8f, 2.0f);
		}

		Vector vecDir    = s_pBuster->v.origin - pEdict->v.origin;
		Vector vecAngles = UTIL_VecToAngles(vecDir);
		pEdict->v.ideal_yaw = vecAngles.y;
		BotFixIdealYaw(pEdict);
		return true;
	}

	// -----------------------------------------------------------------
	// Case 3: Buster missing — race to nearest dropped weaponbox.
	// -----------------------------------------------------------------
	if (s_pBusterWeaponbox)
	{
		pBot->i_busters_role = BUSTERS_ROLE_GHOST_GRABBER;
		pBot->pBotPickupItem = NULL;
		pBot->item_waypoint  = -1;

		Vector vecTarget = s_pBusterWeaponbox->v.origin;
		pBot->v_goal           = vecTarget;
		pBot->f_goal_proximity = 20.0f;
		pBot->f_move_speed     = pBot->f_max_speed; // sprint, no pace scale

		// Use the elevated-jump helper for pedestal/ledge placements.
		BotGoalElevatedJump(pBot, vecTarget);

		// Stuck detection: when the elevated-jump helper isn't engaged
		// (already gates on horzDist < 300 && heightDiff > 20) but the
		// bot still isn't reaching the egon — pinned on a step, blocked
		// by a railing, low-headroom doorway, etc — force the same
		// 3-jump combo so a double/triple-jump can clear the obstacle.
		// Sample horizontal distance every 0.4s; if it hasn't dropped
		// by at least 16u for 1.0s, we're stuck → trigger phase 0.
		if (pBot->i_goal_jump_phase == 0)
		{
			Vector vecFlat = vecTarget - pEdict->v.origin;
			vecFlat.z = 0;
			float horzDist = vecFlat.Length();

			if (pBot->f_busters_stuck_check_time < gpGlobals->time)
			{
				if (pBot->f_busters_stuck_check_time == 0.0f
					|| pBot->f_busters_stuck_last_dist - horzDist >= 16.0f)
				{
					// Healthy progress (or first sample) — reset stall.
					pBot->f_busters_stuck_since = 0.0f;
				}
				else if (pBot->f_busters_stuck_since == 0.0f)
				{
					// First time we noticed no progress — start the clock.
					pBot->f_busters_stuck_since = gpGlobals->time;
				}

				pBot->f_busters_stuck_last_dist  = horzDist;
				pBot->f_busters_stuck_check_time = gpGlobals->time + 0.4f;
			}

			// Stalled for 1.0s within reach? Kick off phase 0 of the
			// jump combo.  Bypass the elevated-jump guard by seeding
			// f_goal_jump_stall_time so the 0.5s pre-wait is already
			// satisfied — ghosts have already been trying for 1s+.
			if (pBot->f_busters_stuck_since > 0.0f
				&& gpGlobals->time - pBot->f_busters_stuck_since >= 1.0f
				&& horzDist < 256.0f)
			{
				pBot->f_goal_jump_stall_time = gpGlobals->time - 1.0f;
				pEdict->v.button |= IN_JUMP;
				pBot->i_goal_jump_phase = 1;
				pBot->f_goal_jump_time  = gpGlobals->time + 0.15f;
				pBot->f_busters_stuck_since = 0.0f; // reset; phase machinery takes over
			}
		}

		Vector vecDir    = vecTarget - pEdict->v.origin;
		Vector vecAngles = UTIL_VecToAngles(vecDir);
		pEdict->v.ideal_yaw = vecAngles.y;
		BotFixIdealYaw(pEdict);
		return true;
	}

	// -----------------------------------------------------------------
	// Case 4: Nothing in play — fall back to normal nav.
	// -----------------------------------------------------------------
	pBot->i_busters_role = BUSTERS_ROLE_NONE;
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


//=========================================================
// Horde — survivors-vs-monsters mode
//
// Gamerules: each wave spawns N monsters tagged with
// pev->message == "horde" and the Horde radar/team marker
// stored in pev->fuser3 at runtime.
// All survivor players are on team "survivors"; FF is
// blocked at gamerules.  Between waves there is a short
// breather window (typically ~3-10s) during which surviving
// players can run the map collecting health/armor/ammo
// pickups; the gamerules also auto-replenish HP to 100 when
// the next wave begins.
//
// Bot strategy:
//   HUNTER   — wave active, pursue highest-threat-by-score
//              monster (gargantua > assassin > grunt > ...).
//              Sticky for 4s to avoid target thrashing.
//   RESUPPLY — no monsters alive, fall back to default
//              pickup/wander logic so the bot grabs items
//              during the inter-wave breather.
//   RETREAT  — HP <= 25, drop the chase and let default
//              pickup logic find a healthkit.
//=========================================================
#define HORDE_STICKY_TIME       4.0f
#define HORDE_RETREAT_HP        25.0f
#define HORDE_TARGET_SWITCH_DIST 256.0f

#define MAX_HORDE_CACHE 32
static float    s_horde_cache_time = -1.0f;
static int      s_horde_count = 0;
static edict_t *s_horde_monsters[MAX_HORDE_CACHE];

static int BotHordeMonsterScore( edict_t *pMonster )
{
	const char *cls = STRING(pMonster->v.classname);
	if (FStrEq(cls, "monster_gargantua"))      return 100;
	if (FStrEq(cls, "monster_human_assassin")) return  60;
	if (FStrEq(cls, "monster_panther"))        return  50;
	if (FStrEq(cls, "monster_human_grunt"))    return  40;
	if (FStrEq(cls, "monster_houndeye"))       return  25;
	if (FStrEq(cls, "monster_zombie"))         return  15;
	if (FStrEq(cls, "monster_headcrab"))       return   8;
	return 10;
}

static void BotHordeFindEntities( void )
{
	if (s_horde_cache_time == gpGlobals->time)
		return;

	s_horde_cache_time = gpGlobals->time;
	s_horde_count = 0;

	edict_t *pEnt = NULL;
	while ((pEnt = UTIL_FindEntityByString(pEnt, "message", "horde")) != NULL
		&& s_horde_count < MAX_HORDE_CACHE)
	{
		if (FNullEnt(pEnt) || pEnt->free)
			continue;
		if (!(pEnt->v.flags & FL_MONSTER))
			continue;
		if (!IsAlive(pEnt))
			continue;
		if (pEnt->v.effects & EF_NODRAW)
			continue;
		s_horde_monsters[s_horde_count++] = pEnt;
	}
}

edict_t *BotHordePickTarget( bot_t *pBot )
{
	BotHordeFindEntities();
	if (s_horde_count == 0)
		return NULL;

	edict_t *pEdict = pBot->pEdict;

	// Sticky preference: keep current target while it lives and the
	// 4-second lock is unexpired.  Prevents thrashing between two
	// equally-weighted threats every 0.75s role tick.
	if (!FNullEnt(pBot->p_horde_target) && IsAlive(pBot->p_horde_target)
		&& (pBot->p_horde_target->v.flags & FL_MONSTER)
		&& pBot->f_horde_target_time > gpGlobals->time)
	{
		return pBot->p_horde_target;
	}

	// Score = base_threat * 1000 / (distance + 200).  Closer + higher
	// base_threat wins; constants tuned so a gargantua at 1500u still
	// outweighs a headcrab at 200u, but a headcrab at 100u beats a
	// gargantua at 4000u.
	float bestScore = -1.0f;
	edict_t *pBest = NULL;
	for (int i = 0; i < s_horde_count; i++)
	{
		edict_t *pM = s_horde_monsters[i];
		float dist = (pM->v.origin - pEdict->v.origin).Length();
		float score = (float)BotHordeMonsterScore(pM) * (1000.0f / (dist + 200.0f));
		if (score > bestScore)
		{
			bestScore = score;
			pBest = pM;
		}
	}
	return pBest;
}

//=========================================================
// BotHordePreUpdate — called from BotThink BEFORE
// BotFindEnemy every frame in Horde mode.
//
// Refreshes the monster cache, evaluates the bot's role at
// ~0.75s intervals, picks the best monster target with a
// 4-second sticky lock, and pre-sets v_goal so the movement
// block has a destination on every tick (even ticks where
// combat runs instead of BotHordeThink).
//=========================================================
void BotHordePreUpdate( bot_t *pBot )
{
	if (is_gameplay != GAME_HORDE)
		return;

	edict_t *pEdict = pBot->pEdict;
	BotHordeFindEntities();

	// Role evaluation throttled to 0.75s (matches Cold Spot cadence).
	if (pBot->f_horde_role_eval_time < gpGlobals->time)
	{
		pBot->f_horde_role_eval_time = gpGlobals->time + 0.75f;

		if (pEdict->v.health > 0 && pEdict->v.health <= HORDE_RETREAT_HP)
			pBot->i_horde_role = HORDE_ROLE_RETREAT;
		else if (s_horde_count == 0)
			pBot->i_horde_role = HORDE_ROLE_RESUPPLY;
		else
			pBot->i_horde_role = HORDE_ROLE_HUNTER;
	}

	if (pBot->i_horde_role == HORDE_ROLE_HUNTER)
	{
		edict_t *pTarget = BotHordePickTarget(pBot);
		if (!FNullEnt(pTarget))
		{
			// Refresh sticky lock when target changes.
			if (pBot->p_horde_target != pTarget)
			{
				pBot->p_horde_target      = pTarget;
				pBot->f_horde_target_time = gpGlobals->time + HORDE_STICKY_TIME;
			}

			Vector vecMonster = pTarget->v.origin;

			// Detect a meaningful target relocation — wipe stale waypoint
			// routing so the bot re-pathfinds on the next nav tick.
			if (pBot->v_horde_last_target_org != g_vecZero
				&& (pBot->v_horde_last_target_org - vecMonster).Length() > HORDE_TARGET_SWITCH_DIST)
			{
				pBot->waypoint_goal        = -1;
				pBot->old_waypoint_goal    = -1;
				pBot->f_waypoint_goal_time = 0.0f;
			}
			pBot->v_horde_last_target_org = vecMonster;

			pBot->v_goal           = vecMonster;
			pBot->f_goal_proximity = 64.0f;

			// Suppress generic item detours while hunting.
			pBot->pBotPickupItem = NULL;
			pBot->item_waypoint  = -1;
			return;
		}
		// Fall through if no target found this frame.
	}

	// RESUPPLY / RETREAT (or HUNTER with no target this frame): clear
	// v_goal so the default pickup/wander logic runs and the bot grabs
	// items during the inter-wave breather or retreat phase.
	pBot->v_goal                  = g_vecZero;
	pBot->f_goal_proximity        = 0.0f;
	pBot->p_horde_target          = NULL;
	pBot->f_horde_target_time     = 0.0f;
	pBot->v_horde_last_target_org = g_vecZero;
}

//=========================================================
// BotHordeThink — called from BotThink when no combat is
// active.
//
// HUNTER   — drive toward picked monster origin (set by
//            PreUpdate); face the target.
// RESUPPLY — return false to let default nav/pickup run.
// RETREAT  — return false; the default pickup logic
//            naturally prioritizes health when low.
//=========================================================
bool BotHordeThink( bot_t *pBot )
{
	if (is_gameplay != GAME_HORDE)
		return false;

	edict_t *pEdict = pBot->pEdict;
	pBot->f_pause_time = 0;

	if (pBot->i_horde_role == HORDE_ROLE_HUNTER && pBot->v_goal != g_vecZero)
	{
		pBot->f_move_speed     = pBot->f_max_speed;
		pBot->f_goal_proximity = 64.0f;

		// Face the target so the bot can shoot the moment it acquires.
		Vector vecDir = pBot->v_goal - pEdict->v.origin;
		if (vecDir.Length() > 1.0f)
		{
			Vector angles = UTIL_VecToAngles(vecDir);
			pEdict->v.ideal_yaw = angles.y;
			BotFixIdealYaw(pEdict);
		}
		return true;
	}

	// RESUPPLY / RETREAT: fall back to default nav so BotFindItem and
	// BotFindWaypointGoal route to pickups (the bot pickup heuristics
	// already weight health/armor/ammo by current need).
	return false;
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

	// Capture the previous frame's enemy up-front so the re-acquire penalty
	// below can detect it even after the function has nulled pBotEnemy on
	// LOS loss.
	edict_t *pPrevEnemy = pBot->pBotEnemy;

	if (is_gameplay == GAME_PROPHUNT)
	{
		if (pBot->f_pause_time >= gpGlobals->time)
		{
			// we're a prop, can't have an enemy
			pBot->pBotEnemy = NULL;
			return NULL;
		}
	}

	// Horde: when the bot is in RETREAT, refuse to acquire any enemy so
	// it commits to falling back / grabbing pickups instead of wading
	// back into combat at low HP.  Sticky preference for the picked
	// monster target is applied via pPrevEnemy below — if the picked
	// target is alive and visible, prefer it over closer targets.
	if (is_gameplay == GAME_HORDE)
	{
		if (pBot->i_horde_role == HORDE_ROLE_RETREAT)
		{
			pBot->pBotEnemy = NULL;
			return NULL;
		}
		// If the sticky horde target is still valid and currently visible,
		// select it immediately instead of falling through to normal target
		// acquisition.
		if (!FNullEnt(pBot->p_horde_target) && IsAlive(pBot->p_horde_target)
			&& pBot->f_horde_target_time > gpGlobals->time)
		{
			Vector vecTOrg = pBot->p_horde_target->v.origin + pBot->p_horde_target->v.view_ofs;
			if (FInViewCone(&vecTOrg, pEdict) && FVisible(vecTOrg, pEdict))
			{
				pBot->pBotEnemy = pBot->p_horde_target;
				pBot->f_bot_see_enemy_time = gpGlobals->time;
				return pBot->p_horde_target;
			}
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
		else if (is_gameplay == GAME_LOOT)
		{
			// Carrier pacifism — when this bot is the loot holder and
			// healthy, drop the enemy so the bot keeps running to the
			// goal instead of dueling on the way.  Mirrors CTC.
			if (pBot->pEdict->v.health > LOOT_CARRIER_PACIFY_HP
				&& (int)pBot->pEdict->v.fuser4 == RADAR_LOOT_VAL)
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
			// if enemy is still visible and in field of view, keep it
			// keep track of when we last saw an enemy
			pBot->f_bot_see_enemy_time = gpGlobals->time;
			pBot->f_last_enemy_los_time = gpGlobals->time;
			// remember our current enemy and check for a new one
			pRemember = pBot->pBotEnemy;
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

			// Busters has constant new-enemy churn (cluster fights, role
			// swaps on egon pickup, ghosts vs the lone buster).  The full
			// react formula (~1.1s base * ~2.0 distance multiplier) makes
			// every first contact look like a 2-3s frozen stand-off.
			// Skip the distance multiplier and cap to a snappy floor so
			// engagements start within ~0.3-0.5s.
			if (is_gameplay == GAME_BUSTERS)
			{
				react_delay = RANDOM_FLOAT(delay_min, delay_max) * 0.4f;
				if (react_delay > 0.5f) react_delay = 0.5f;
			}

			pBot->f_reaction_target_time = gpGlobals->time + react_delay;

			//SERVER_PRINT( "%s reacting in %f seconds!\n", STRING(pEdict->v.netname), react_delay);
		}
		// Re-acquire penalty: when the bot picks this enemy back up after
		// line-of-sight was broken >= 0.5s ago, apply a shorter hesitation
		// and bump the aim error to the per-skill max.  This removes the
		// "corner-snap" where a bot that saw you 1.9s ago insta-lasers you
		// the moment you reappear.  Arena (1v1) mode is exempt so duels
		// stay snappy.
		//
		// Keyed off pPrevEnemy (captured before pBotEnemy is nulled on LOS
		// loss) so it also fires in the common "remembered enemy reappears"
		// case, not just the rare keep-through-LOS-break branch.
		else if ((bot_reaction_time > 0) && (pPrevEnemy != NULL) &&
			(pNewEnemy == pPrevEnemy) &&
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
	{
		// Horde: monsters are the *only* legitimate targets and they
		// aren't class "player".  Treat any live FL_MONSTER as engage-
		// worthy so the engagement state machine commits, the combat
		// movement block runs (strafing, longjump, ignore-wpt), and
		// the bot doesn't immediately fall into the "give up engaging"
		// branch the next frame.
		if (is_gameplay == GAME_HORDE && pEnemy != NULL
			&& (pEnemy->v.flags & FL_MONSTER) && IsAlive(pEnemy))
			return TRUE;
		return FALSE;
	}

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

		// Combat weapon-switch hysteresis: when the bot recently switched
		// weapons in combat (cooldown active), and the current weapon is
		// still usable at this distance for either fire mode, stick with
		// it instead of switching again.  Without this, an enemy whose
		// distance is straddling a weapon's primary_min/max boundary
		// (e.g. cannon @ 300u min vs. a headcrab leaping in/out) causes
		// constant switch-and-cancel-fire churn that prevents the bot
		// from ever firing.
		if (pBot->f_combat_switch_cooldown > gpGlobals->time &&
			pBot->current_weapon.iId > 0 &&
			pBot->current_weapon.iId != iId &&
			UTIL_HasWeaponId(pEdict, pBot->current_weapon.iId))
		{
			int cur_idx = WeaponGetSelectIndex(pBot->current_weapon.iId);
			if (cur_idx >= 0 && pSelect[cur_idx].iId == pBot->current_weapon.iId)
			{
				bool cur_prim_ok =
					(distance >= pSelect[cur_idx].primary_min_distance) &&
					(distance <= pSelect[cur_idx].primary_max_distance) &&
					(BotAssessPrimaryAmmo(pBot, pBot->current_weapon.iId) != AMMO_CRITICAL);
				bool cur_sec_ok =
					(distance >= pSelect[cur_idx].secondary_min_distance) &&
					(distance <= pSelect[cur_idx].secondary_max_distance) &&
					(BotAssessSecondaryAmmo(pBot, pBot->current_weapon.iId) != AMMO_CRITICAL);

				if (cur_prim_ok || cur_sec_ok)
				{
					final_index = cur_idx;
					iId = pBot->current_weapon.iId;
					use_primary[cur_idx] = cur_prim_ok;
					use_secondary[cur_idx] = (!cur_prim_ok) && cur_sec_ok;
				}
			}
		}

		// select this weapon if it isn't already selected
		if (is_gameplay != GAME_CTC && pBot->current_weapon.iId != iId/* && g_flWeaponSwitch <= gpGlobals->time*/)
		{
			//ALERT(at_console, "Switch weapon\n");
			//g_flWeaponSwitch = gpGlobals->time + 1.0;
			//ALERT( at_console, "Switching to %s\n", pSelect[final_index].weapon_name);
			UTIL_SelectItem(pEdict, pSelect[final_index].weapon_name);
			pBot->f_shoot_time = gpGlobals->time + 0.5;
			// Lock out further combat switches for ~1.5s so we don't
			// oscillate when the enemy moves across a distance threshold.
			pBot->f_combat_switch_cooldown = gpGlobals->time + 1.5;
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
			//
			// NOTE: i_burst_count increments once per call to BotFireWeapon (which
			// runs once per think frame while the trigger is held for a
			// primary_fire_hold auto at range > 350).  It is NOT a per-bullet
			// counter; the engine's weapon-fire timing decides how many rounds
			// actually leave the barrel per tick.  The threshold of 3-6 is
			// tuned for feel, not ballistic accuracy.
			bool burst_active = pSelect[final_index].primary_fire_hold &&
				!pSelect[final_index].primary_fire_charge &&
				distance > 350.0f;

			// Weapon / range switch → drop any leftover burst state so stale
			// counters from a previous weapon can't cause an immediate pause
			// on the first frame of the next auto burst.
			if (!burst_active || pBot->i_burst_last_weapon != iId)
			{
				pBot->i_burst_count = 0;
				pBot->f_burst_pause_until = 0.0f;
				pBot->i_burst_last_weapon = burst_active ? iId : 0;
			}

			bool burst_pausing = false;
			if (burst_active)
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
					// Count this frame's trigger-hold tick toward the burst
					// budget for autos at range.  Reset outside the burst
					// window so state cannot leak into close-range fights.
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
		//
		// BUT: when LOS to the enemy was lost recently, the bot is aiming
		// at a remembered position.  Adding velocity-scaled tracking error
		// AND per-frame jitter on top of that produces visible pitch/yaw
		// wobble (the gun bobs while the monster is occluded).  In that
		// state, hold a smooth lock on the remembered point — the
		// remembered position itself is already "error" enough.
		bool has_recent_los = (pBot->f_bot_see_enemy_time > 0.0f) &&
			((gpGlobals->time - pBot->f_bot_see_enemy_time) < 0.3f);

		float difficulty = bot_aim_difficulty;
		if (difficulty < 0.0f) difficulty = 0.0f;
		if (difficulty > 2.0f) difficulty = 2.0f;

		if (has_recent_los)
		{
			f_velocity = fmax(pBot->pBotEnemy->v.velocity.Length() * 0.01, 1);
			d_x += pBot->f_aim_x_angle_delta * f_velocity;
			d_y += pBot->f_aim_y_angle_delta * f_velocity;

			float jitter = aim_jitter_scale[pBot->bot_skill] * difficulty;
			if (jitter > 0.0f)
			{
				// Smooth aim: use a persistent jitter offset refreshed at a low
				// frequency (every 0.15-0.30s) instead of fresh per-frame
				// randomization, which reads as visible "vibration" on the
				// bot's view.
				if (pBot->f_aim_jitter_refresh_time < gpGlobals->time)
				{
					pBot->f_aim_jitter_refresh_time = gpGlobals->time +
						RANDOM_FLOAT(0.15f, 0.30f);
					pBot->f_aim_jitter_x = RANDOM_FLOAT(-jitter, jitter);
					pBot->f_aim_jitter_y = RANDOM_FLOAT(-jitter, jitter);
				}
				d_x += pBot->f_aim_jitter_x;
				d_y += pBot->f_aim_jitter_y;
			}
		}
		else
		{
			// Occluded: zero out persistent jitter so it doesn't snap back
			// in when LOS is reacquired mid-refresh-window.
			pBot->f_aim_jitter_x = 0.0f;
			pBot->f_aim_jitter_y = 0.0f;
			pBot->f_aim_jitter_refresh_time = 0.0f;
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

	// -----------------------------------------------------------------
	// Busters: prevent wall-mashing.  When the bot has spotted an enemy
	// through a gap/sliver but cannot physically reach them (FHullClear
	// false), the yaw lock above would point the bot at the wall; the
	// movement system's cos(yaw - waypoint_dir) factor then collapses
	// forward speed and the bot just grinds into the brush.  Restore
	// the waypoint-driven facing so the bot keeps routing around toward
	// the enemy.  Once FHullClear becomes TRUE (LOS opens / corner
	// turned), the next frame's yaw lock takes over and the bot fires
	// normally.  Busters-only so other modes' tactics aren't disturbed.
	// -----------------------------------------------------------------
	if (is_gameplay == GAME_BUSTERS && pBot->pBotEnemy &&
		pBot->curr_waypoint_index != -1 && !pBot->b_combat_longjump &&
		pBot->i_goal_jump_phase == 0 &&
		!FHullClear(v_enemy_origin, pEdict))
	{
		Vector vecWpDir = pBot->v_curr_direction;
		vecWpDir.z = 0;
		if (vecWpDir.Length() > 1.0f)
		{
			Vector vecWpAngles = UTIL_VecToAngles(vecWpDir);
			pEdict->v.ideal_yaw = vecWpAngles.y;
			BotFixIdealYaw(pEdict);
		}
	}

	// -----------------------------------------------------------------
	// Combat stuck-jump: if the bot has a visible enemy but can't close
	// the distance (crate/box/railing/elevated platform/wall between
	// them), reuse the same multi-jump helper that drives CTF flag
	// pursuit.  Sample horizontal distance every 0.4s; if it hasn't
	// dropped 16u in 1.0s while the enemy is visible and within 512u,
	// invoke BotGoalElevatedJump with bForceTrigger=true so the helper
	// runs the full ground → double → triple-jump (flip) sequence,
	// re-asserting forward facing + max forward speed each frame so
	// velocity.Length2D() stays > 100 and the flip on phase 3 lands.
	// -----------------------------------------------------------------
	if (pBot->pBotEnemy &&
		(pBot->pBotEnemy->v.flags & (FL_CLIENT | FL_MONSTER)) &&
		f_distance > 80.0f && f_distance < 512.0f &&
		FVisible(v_enemy_origin, pEdict))
	{
		Vector vecFlat = pBot->pBotEnemy->v.origin - pEdict->v.origin;
		vecFlat.z = 0;
		float horzDist = vecFlat.Length();

		if (pBot->f_combat_stuck_check_time < gpGlobals->time)
		{
			if (pBot->f_combat_stuck_check_time == 0.0f
				|| pBot->f_combat_stuck_last_dist - horzDist >= 16.0f)
			{
				pBot->f_combat_stuck_since = 0.0f;
			}
			else if (pBot->f_combat_stuck_since == 0.0f)
			{
				pBot->f_combat_stuck_since = gpGlobals->time;
			}

			pBot->f_combat_stuck_last_dist  = horzDist;
			pBot->f_combat_stuck_check_time = gpGlobals->time + 0.4f;
		}

		// Stalled for 1.0s + on the ground? Hand off to the multi-jump
		// helper.  It owns the phase machine from here until phase 3 →
		// reset (handled below by re-entering this branch).
		if (pBot->i_goal_jump_phase == 0
			&& pBot->f_combat_stuck_since > 0.0f
			&& gpGlobals->time - pBot->f_combat_stuck_since >= 1.0f
			&& (pEdict->v.flags & FL_ONGROUND))
		{
			pBot->f_combat_stuck_since = 0.0f;
			BotGoalElevatedJump(pBot, pBot->pBotEnemy->v.origin, true /*force*/);
		}
		// Already mid-combo: keep advancing it every frame so phases
		// 1 → 2 → 3 fire on schedule with f_move_speed re-asserted.
		else if (pBot->i_goal_jump_phase > 0)
		{
			BotGoalElevatedJump(pBot, pBot->pBotEnemy->v.origin, true /*force*/);
		}
	}
	else if (!pBot->pBotEnemy)
	{
		// No enemy → drop stale samples so a future encounter starts clean.
		pBot->f_combat_stuck_check_time = 0.0f;
		pBot->f_combat_stuck_since      = 0.0f;
	}

	// -----------------------------------------------------------------
	// Horde elevated-monster pursuit: if a monster is above the bot
	// (e.g. headcrab on a platform/ledge), the bot would otherwise keep
	// running its waypoint underneath while only aim-locking, never
	// firing because FVisible to the body keeps clipping the platform
	// edge and never closing because the monster's Z is unreachable
	// from the current path.
	//
	// When we have a live monster enemy that is meaningfully above us
	// (heightDiff > 32) and within engagement range (≤ 600u horiz),
	// break off the waypoint, lock on, and hand off to the multi-jump
	// helper with bForceTrigger=true.  The helper runs ground →
	// double → triple-jump (flip) and re-asserts forward facing + max
	// forward speed every frame — matching the CTF/Busters flag-pursuit
	// behavior the user referenced.  No horizontal-distance "stall"
	// gate here: bots running waypoints past a ledge mob never trip
	// the stall detector, so they'd never engage without this branch.
	// -----------------------------------------------------------------
	if (is_gameplay == GAME_HORDE && pBot->pBotEnemy &&
		(pBot->pBotEnemy->v.flags & FL_MONSTER) && IsAlive(pBot->pBotEnemy))
	{
		float heightDiff = pBot->pBotEnemy->v.origin.z - pEdict->v.origin.z;
		Vector vecFlat = pBot->pBotEnemy->v.origin - pEdict->v.origin;
		vecFlat.z = 0;
		float horzDist = vecFlat.Length();

		if (heightDiff > 32.0f && horzDist <= 600.0f)
		{
			// Suspend waypoint following so the multi-jump can re-aim
			// the bot's yaw at the monster instead of the next node.
			pBot->f_ignore_wpt_time = gpGlobals->time + 0.5f;
			pBot->f_move_speed      = pBot->f_max_speed;

			// Kick off (or continue) the ground → double → triple-jump
			// sequence toward the monster.  bForceTrigger=true bypasses
			// the helper's "wait 0.5s + close horizontally" gate so we
			// commit immediately when the elevation is detected.
			if (pBot->i_goal_jump_phase == 0
				&& (pEdict->v.flags & FL_ONGROUND))
			{
				BotGoalElevatedJump(pBot, pBot->pBotEnemy->v.origin, true /*force*/);
			}
			else if (pBot->i_goal_jump_phase > 0)
			{
				BotGoalElevatedJump(pBot, pBot->pBotEnemy->v.origin, true /*force*/);
			}
		}
	}

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

	// Horde: same as Arena — monsters are the only objective.  Skip the
	// random aggressiveness gate so bots commit to combat as soon as a
	// monster is acquired, and so the engagement-driven movement block
	// (strafing, ignore-wpt, longjump) actually runs.
	if (is_gameplay == GAME_HORDE && !pBot->b_engaging_enemy && pBot->pBotEnemy != NULL
		&& (pBot->pBotEnemy->v.flags & FL_MONSTER))
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
	// Independent close-range melee: kick or punch when an enemy is within
	// 96u, on a per-skill cooldown.  Runs regardless of weapon-fire state
	// so the bot still throws hands during a weapon switch / shoot-time
	// hold-off — without this, a bot oscillating between weapons (cannon
	// out of min range, pistol in range) would stand passively while a
	// headcrab clawed it.  Cooldown intervals (s):
	//   skill 0 (best)   -> 0.6 + 0.3 jitter
	//   skill 4 (worst)  -> 1.5 + 0.3 jitter
	// not overpowered: still capped at one impulse per cooldown window.
	if (sv_botsmelee.value > 0 && is_gameplay != GAME_GUNGAME &&
		pBot->pBotEnemy && f_distance <= 96.0f &&
		pBot->f_next_melee_time < gpGlobals->time &&
		FInViewCone(&v_enemy_origin, pEdict) &&
		FVisible(v_enemy_origin, pEdict))
	{
		static const float melee_cooldown[5] = { 0.6f, 0.8f, 1.0f, 1.25f, 1.5f };
		int skill = pBot->bot_skill;
		if (skill < 0) skill = 0;
		if (skill > 4) skill = 4;
		pBot->f_next_melee_time = gpGlobals->time +
			melee_cooldown[skill] + RANDOM_FLOAT(0.0f, 0.3f);
		pEdict->v.impulse = 206 + RANDOM_LONG(0, 1);  // 206 kick / 207 punch
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
				// loot_crate is not allowlisted here, so it is excluded from
				// grenade/entity scanning in this path unless re-added below.
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


//=========================================================
// Rune handling
//
// The bot DLL does not have access to CBasePlayer::m_fHasRune,
// so it tracks its own rune type via the pickup callback. These
// helpers let the bot decide whether a rune on the ground is
// worth swapping for, and (if so) drop the held rune just before
// touching the new one.
//
// See workspace/ai/gravebot.md "Rune handling" and
// workspace/ai/runes.md for the design and rune effects.
//=========================================================

int BotRuneClassToType(const char *classname)
{
	if (classname == NULL)
		return 0;
	if      (strcmp(classname, "rune_frag")     == 0) return BOT_RUNE_FRAG;
	else if (strcmp(classname, "rune_vampire")  == 0) return BOT_RUNE_VAMPIRE;
	else if (strcmp(classname, "rune_protect")  == 0) return BOT_RUNE_PROTECT;
	else if (strcmp(classname, "rune_regen")    == 0) return BOT_RUNE_REGEN;
	else if (strcmp(classname, "rune_haste")    == 0) return BOT_RUNE_HASTE;
	else if (strcmp(classname, "rune_gravity")  == 0) return BOT_RUNE_GRAVITY;
	else if (strcmp(classname, "rune_strength") == 0) return BOT_RUNE_STRENGTH;
	else if (strcmp(classname, "rune_cloak")    == 0) return BOT_RUNE_CLOAK;
	else if (strcmp(classname, "rune_ammo")     == 0) return BOT_RUNE_AMMO;
	return 0;
}

float BotEvaluateRuneScore(bot_t *pBot, int rune_type)
{
	if (rune_type <= 0 || pBot == NULL || pBot->pEdict == NULL)
		return 0.0f;

	// Base weights — direct combat power-ups score highest.
	float score = 0.0f;
	switch (rune_type)
	{
	case BOT_RUNE_STRENGTH: score = 90.0f; break;
	case BOT_RUNE_HASTE:    score = 85.0f; break;
	case BOT_RUNE_VAMPIRE:  score = 80.0f; break;
	case BOT_RUNE_PROTECT:  score = 80.0f; break;
	case BOT_RUNE_REGEN:    score = 70.0f; break;
	case BOT_RUNE_FRAG:     score = 65.0f; break;
	case BOT_RUNE_CLOAK:    score = 60.0f; break;
	case BOT_RUNE_AMMO:     score = 55.0f; break;
	case BOT_RUNE_GRAVITY:  score = 50.0f; break;
	default:                score =  0.0f; break;
	}

	float health = pBot->pEdict->v.health;

	// REGEN — strongly preferred when low HP.
	if (rune_type == BOT_RUNE_REGEN)
	{
		if (health < 30.0f)      score += 50.0f;
		else if (health < 60.0f) score += 25.0f;
	}

	// VAMPIRE — also a heal source when low.
	if (rune_type == BOT_RUNE_VAMPIRE && health < 60.0f)
		score += 20.0f;

	// PROTECT — most valuable while taking fire.
	if (rune_type == BOT_RUNE_PROTECT && pBot->b_engaging_enemy)
		score += 15.0f;

	// CLOAK — disengagement / escape rune.
	if (rune_type == BOT_RUNE_CLOAK && health < 40.0f && !pBot->b_engaging_enemy)
		score += 15.0f;

	// AMMO — boost when current weapon's ammo is critical.
	if (rune_type == BOT_RUNE_AMMO && pBot->current_weapon.iId > 0)
	{
		float prim_ammo = BotAssessPrimaryAmmo(pBot, pBot->current_weapon.iId);
		float sec_ammo  = BotAssessSecondaryAmmo(pBot, pBot->current_weapon.iId);
		if (prim_ammo == AMMO_CRITICAL || sec_ammo == AMMO_CRITICAL)
			score += 30.0f;
	}

	// HASTE — flag carrier wants to move + shoot fast.
	if (rune_type == BOT_RUNE_HASTE && pBot->bot_has_flag)
		score += 20.0f;

	// FRAG — only meaningful in DM (no team scoring uplift).
	if (rune_type == BOT_RUNE_FRAG && is_team_play <= 0.0f)
		score += 15.0f;

	return score;
}

void BotMaybeDropRuneForSwap(bot_t *pBot)
{
	if (pBot == NULL || pBot->pEdict == NULL)
		return;
	if (!pBot->b_rune || pBot->i_rune_type == 0)
		return;
	if (gpGlobals->time < pBot->f_rune_drop_cooldown)
		return;
	if (pBot->pBotPickupItem == NULL || FNullEnt(pBot->pBotPickupItem) ||
		pBot->pBotPickupItem->free)
		return;

	// Only drop when our queued pickup is itself a rune (BotFindItem only
	// queues a rune over an existing rune when the new one scores better).
	const char *pickup_class = STRING(pBot->pBotPickupItem->v.classname);
	if (BotRuneClassToType(pickup_class) == 0)
		return;

	// Within touch range — drop now so the next step picks up the new rune.
	float dist = (pBot->pBotPickupItem->v.origin - pBot->pEdict->v.origin).Length();
	if (dist > 120.0f)
		return;

	// Issue the same client command a human would.
	FakeClientCommand(pBot->pEdict, "drop_rune", NULL, NULL);

	// Mirror the cleared state immediately and lock out rune pickups
	// briefly so we don't re-grab the rune we just discarded
	// (CWorldRunes::DropRune launches it ~400 u/s forward).
	pBot->b_rune = FALSE;
	pBot->i_rune_type = 0;
	pBot->f_rune_drop_cooldown = gpGlobals->time + 1.5f;
}
