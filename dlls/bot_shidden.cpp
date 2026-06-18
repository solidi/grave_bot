//
// bot_shidden.cpp — grave-bot AI for the Shidden gameplay (GAME_SHIDDEN).
//
// Roles
// -----
// Dealters (pev->fuser4 == SHIDDEN_DEALTER, invisible, fists + knife only)
//   HUNTER      — stalk a smelter, hold fists, fart at close range to
//                 trigger the 5-second freeze
//   FINISHER    — a smelter is frozen (iuser4 > 0); switch to knife,
//                 close to point-blank and primary-stab for one-shot kill
//
// Smelters (pev->fuser4 == SHIDDEN_SMELTER, visible, normal loadout)
//   FLOCK       — clump with teammates (centroid + jitter) so dealter
//                 farts are forced to hit multiple targets, easier to spot
//   DEFENDER    — a teammate is frozen (iuser4 > 0); break flock to scan
//                 the frozen teammate's surroundings, force-target any
//                 visible dealter to deter the knife-finish
//   SCOUT       — no frozen teammate, no visible dealter; roam waypoints
//
// Cross-DLL freeze signal
//   pev->iuser4   freeze counter mirror (set by ice.dll player.cpp and
//                 shidden_gamerules.cpp; > 0 means frozen for iuser4 ticks)
//
// Pattern mirrors BotLoot* / BotProphunt* (PreUpdate + Think + role eval).
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
#include "waypoint.h"
#include "bot_weapons.h"

extern int is_gameplay;
extern edict_t *clients[32];
extern bot_t bots[32];

// Mode-team values for pev->fuser4 in Shidden — keep in lockstep with
// ice.dll shidden_gamerules.cpp.
#define SHIDDEN_SMELTER  0
#define SHIDDEN_DEALTER  1

// Tunables
static const float SHID_ROLE_EVAL_PERIOD   = 0.5f;
static const float SHID_FART_RANGE         = 240.0f;  // < gameplay 256u radius
static const float SHID_FART_COOLDOWN      = 0.85f;   // > weapon 0.75s cooldown
static const float SHID_KNIFE_RANGE        = 64.0f;
static const float SHID_FINISHER_LOCK_TIME = 2.5f;
static const float SHID_FLOCK_RADIUS       = 384.0f;
static const float SHID_DEFEND_SCAN_RADIUS = 640.0f;
static const float SHID_DEALTER_SPOT_RANGE = 768.0f;
static const float SHID_SWITCH_COOLDOWN    = 0.25f;
static const float SHID_UNSEEN_BOOST_TIME  = 2.0f;

// Per-frame cache of all live Shidden players.  Tiny (<= 32 entries) so
// a single linear walk per Think tick is fine — no need for a persistent
// data structure.  Indices are 1..maxClients; entries may be NULL.
typedef struct
{
	float    f_built_time;
	int      n_dealters;
	int      n_smelters;
	int      n_frozen_smelters;
	edict_t *pDealters[32];
	edict_t *pSmelters[32];
	edict_t *pFrozenSmelters[32];   // subset of pSmelters with iuser4 > 0
} shidden_cache_t;

static shidden_cache_t s_cache;

// Walk clients[] once per server frame and rebuild the role caches.
// Skips spectators, dead players, and the calling bot itself.
static void BotShiddenBuildCache(void)
{
	if (s_cache.f_built_time == gpGlobals->time)
		return;

	s_cache.f_built_time      = gpGlobals->time;
	s_cache.n_dealters        = 0;
	s_cache.n_smelters        = 0;
	s_cache.n_frozen_smelters = 0;

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		edict_t *p = clients[i];
		if (FNullEnt(p) || !IsAlive(p)) continue;
		if (p->v.flags & FL_NOTARGET) continue;

		// Spectators have empty model strings; ignore.
		if (p->v.deadflag != DEAD_NO) continue;

		if (p->v.fuser4 == SHIDDEN_DEALTER)
		{
			if (s_cache.n_dealters < 32)
				s_cache.pDealters[s_cache.n_dealters++] = p;
		}
		else if (p->v.fuser4 == SHIDDEN_SMELTER)
		{
			if (s_cache.n_smelters < 32)
				s_cache.pSmelters[s_cache.n_smelters++] = p;
			if (p->v.iuser4 > 0 && s_cache.n_frozen_smelters < 32)
				s_cache.pFrozenSmelters[s_cache.n_frozen_smelters++] = p;
		}
	}
}

// Closest entry from a cache list, optionally requiring LoS.
static edict_t *BotShiddenClosest(bot_t *pBot, edict_t **list, int count,
                                  bool requireLoS, float *outDist)
{
	edict_t *pBest = NULL;
	float    best  = 999999.0f;
	edict_t *pE    = pBot->pEdict;

	for (int i = 0; i < count; i++)
	{
		edict_t *p = list[i];
		if (FNullEnt(p) || p == pE) continue;
		if (requireLoS && !FVisible(p->v.origin, pE)) continue;

		float d = (p->v.origin - pE->v.origin).Length();
		if (d < best) { best = d; pBest = p; }
	}

	if (outDist) *outDist = pBest ? best : 0.0f;
	return pBest;
}

// Mean position of a cache list — used for smelter flock centroid.
static Vector BotShiddenCentroid(edict_t **list, int count, edict_t *pSelf)
{
	Vector sum = g_vecZero;
	int    n   = 0;
	for (int i = 0; i < count; i++)
	{
		edict_t *p = list[i];
		if (FNullEnt(p) || p == pSelf) continue;
		sum = sum + p->v.origin;
		n++;
	}
	if (n == 0) return g_vecZero;
	return sum * (1.0f / (float)n);
}

// Force the bot to hold weapon_fists / weapon_knife.  Honours a small
// cooldown so we don't spam SelectItem every frame.
static void BotShiddenForceWeapon(bot_t *pBot, int wantId, const char *classname)
{
	if (pBot->current_weapon.iId == wantId) return;
	if (pBot->f_switch_weapon_time > gpGlobals->time) return;

	UTIL_SelectItem(pBot->pEdict, (char *)classname);
	pBot->f_switch_weapon_time = gpGlobals->time + SHID_SWITCH_COOLDOWN;
}

// ----------------------------------------------------------------------
// PreUpdate — runs BEFORE BotFindEnemy so v_goal is valid on the very
// first frame after spawn (same pattern CTF / Loot use).
// ----------------------------------------------------------------------
void BotShiddenPreUpdate(bot_t *pBot)
{
	if (FNullEnt(pBot->pEdict)) return;
	if (!IsAlive(pBot->pEdict))  return;

	BotShiddenBuildCache();

	edict_t *pE = pBot->pEdict;
	const int team = (int)pE->v.fuser4;

	if (team == SHIDDEN_DEALTER)
	{
		// Hunting fallback: whenever no smelter is frozen anywhere on the
		// map, we are by definition in HUNTER mode and must hold fists so
		// the next IN_ATTACK farts (knife is excluded from the fart path
		// in weapons.cpp).  Done here in PreUpdate because BotShiddenThink
		// is skipped once BotFindEnemy sets pBotEnemy, leaving the combat
		// gate as the only weapon-selector — and the combat gate may stay
		// on knife for a tick after the frag if any state is stale.  Also
		// covers the round-start case where the engine auto-switched to
		// the knife (lower priority number) when GiveNamedItem ran.
		if (s_cache.n_frozen_smelters == 0)
		{
			pBot->i_shidden_role = SHIDDEN_ROLE_HUNTER;
			BotShiddenForceWeapon(pBot, VALVE_WEAPON_FISTS, "weapon_fists");
		}

		// Prefer a frozen smelter (finisher takes priority); otherwise
		// the nearest visible smelter, then nearest known smelter.
		edict_t *pTarget = BotShiddenClosest(pBot,
			s_cache.pFrozenSmelters, s_cache.n_frozen_smelters,
			false /*LoS*/, NULL);
		if (!pTarget)
			pTarget = BotShiddenClosest(pBot,
				s_cache.pSmelters, s_cache.n_smelters,
				true /*LoS*/, NULL);
		if (!pTarget)
			pTarget = BotShiddenClosest(pBot,
				s_cache.pSmelters, s_cache.n_smelters,
				false, NULL);

		if (pTarget)
		{
			pBot->v_goal           = pTarget->v.origin;
			pBot->f_goal_proximity = 0.0f;
		}
	}
	else if (team == SHIDDEN_SMELTER)
	{
		// No live dealter exists on the map: clear any stale alert state
		// the Valve damage hook armed on the killing-fart frame.  That
		// hook extends f_dmg_time to 3.0s for unfrozen smelters so they
		// face the (invisible) attacker long enough to spot the dealter,
		// but once the dealter is dead or in spectator there is nothing
		// left to spot — without this sweep the killer freezes facing the
		// stale dmg_origin for the full 3 seconds (pitch wobbles as the
		// bot bobs / steps) AND the f_dmg_time < now gates in
		// bot_navigate.cpp block waypoint following + pickup yaw, so the
		// bot also "stops and does nothing" for that window.
		if (s_cache.n_dealters == 0)
		{
			pBot->f_dmg_time             = 0.0f;
			pBot->dmg_origin             = g_vecZero;
			pBot->f_shidden_unseen_until = 0.0f;
		}

		// If a teammate is frozen, swing toward them (defender pre-set).
		edict_t *pFrozen = BotShiddenClosest(pBot,
			s_cache.pFrozenSmelters, s_cache.n_frozen_smelters,
			false, NULL);
		if (pFrozen && pFrozen != pE)
		{
			pBot->v_goal           = pFrozen->v.origin;
			pBot->f_goal_proximity = 64.0f;
		}
		else
		{
			// Flock toward the team centroid.  Jitter by entindex so
			// bots don't pile on the same pixel.
			Vector c = BotShiddenCentroid(s_cache.pSmelters,
			                              s_cache.n_smelters, pE);
			if (c != g_vecZero)
			{
				const int jitter = (ENTINDEX(pE) * 71) % 96;
				c.x += (float)(jitter - 48);
				c.y += (float)((jitter * 13) % 96 - 48);
				pBot->v_goal           = c;
				pBot->f_goal_proximity = 96.0f;
			}
		}
	}
}

// ----------------------------------------------------------------------
// Think — returns true to short-circuit the rest of the BotThink chain
// when Shidden role logic owns this tick (mirrors BotLootThink / etc.).
// ----------------------------------------------------------------------
bool BotShiddenThink(bot_t *pBot)
{
	edict_t *pE = pBot->pEdict;
	if (FNullEnt(pE)) return false;
	if (!IsAlive(pE)) return false;

	BotShiddenBuildCache();

	const int team = (int)pE->v.fuser4;

	// --- DEALTER ----------------------------------------------------------
	if (team == SHIDDEN_DEALTER)
	{
		// FINISHER lock takes priority — once we commit to a frozen
		// target we stay on it until it dies, thaws, or the lock expires.
		edict_t *pFinishTarget = NULL;
		if (pBot->p_shidden_target
		    && !FNullEnt(pBot->p_shidden_target)
		    && IsAlive(pBot->p_shidden_target)
		    && pBot->p_shidden_target->v.iuser4 > 0
		    && pBot->f_shidden_target_time > gpGlobals->time)
		{
			pFinishTarget = pBot->p_shidden_target;
		}
		else
		{
			pBot->p_shidden_target = NULL;
			edict_t *pNewFrozen = BotShiddenClosest(pBot,
				s_cache.pFrozenSmelters, s_cache.n_frozen_smelters,
				false, NULL);
			if (pNewFrozen)
			{
				pBot->p_shidden_target      = pNewFrozen;
				pBot->f_shidden_target_time = gpGlobals->time
				                              + SHID_FINISHER_LOCK_TIME;
				pFinishTarget = pNewFrozen;
			}
		}

		// Periodic role eval (cheap; cache is reused).
		if (pBot->f_shidden_role_eval_time <= gpGlobals->time)
		{
			pBot->f_shidden_role_eval_time = gpGlobals->time
			                                  + SHID_ROLE_EVAL_PERIOD;
			pBot->i_shidden_role = pFinishTarget
				? SHIDDEN_ROLE_FINISHER
				: SHIDDEN_ROLE_HUNTER;
		}

		// Immediate transition into FINISHER the moment a freeze lands,
		// without waiting for the next role-eval tick.
		if (pFinishTarget && pBot->i_shidden_role != SHIDDEN_ROLE_FINISHER)
			pBot->i_shidden_role = SHIDDEN_ROLE_FINISHER;

		// ---- FINISHER --------------------------------------------------
		if (pBot->i_shidden_role == SHIDDEN_ROLE_FINISHER && pFinishTarget)
		{
			BotShiddenForceWeapon(pBot, VALVE_WEAPON_KNIFE, "weapon_knife");

			pBot->pBotEnemy        = pFinishTarget;
			pBot->v_goal           = pFinishTarget->v.origin;
			pBot->f_goal_proximity = 0.0f;
			pBot->f_pause_time     = 0.0f;
			pBot->f_move_speed     = pBot->f_max_speed;

			float dist = (pFinishTarget->v.origin - pE->v.origin).Length();
			if (dist <= SHID_KNIFE_RANGE
			    && pBot->current_weapon.iId == VALVE_WEAPON_KNIFE
			    && FInViewCone(&pFinishTarget->v.origin, pE))
			{
				// Primary stab — gamerules verify knife + !isOffhand
				// + frozen victim and force the one-shot kill.
				pE->v.button |= IN_ATTACK;
			}

			return true;
		}

		// ---- HUNTER ----------------------------------------------------
		// Ensure we hold fists so the IN_ATTACK press farts (knife is
		// excluded from the fart code path in weapons.cpp).
		BotShiddenForceWeapon(pBot, VALVE_WEAPON_FISTS, "weapon_fists");

		edict_t *pPrey = BotShiddenClosest(pBot,
			s_cache.pSmelters, s_cache.n_smelters, true, NULL);
		if (!pPrey)
			pPrey = BotShiddenClosest(pBot,
				s_cache.pSmelters, s_cache.n_smelters, false, NULL);

		if (!pPrey) return false;   // no prey — fall back to default nav

		pBot->v_goal           = pPrey->v.origin;
		pBot->f_goal_proximity = 0.0f;
		pBot->f_pause_time     = 0.0f;
		pBot->f_move_speed     = pBot->f_max_speed;

		// Fart when we're inside the radius AND have LoS AND the local
		// cooldown has expired (gamerules also enforces a 0.75s weapon
		// cooldown — ours is slightly larger to avoid wasted presses).
		float dist = (pPrey->v.origin - pE->v.origin).Length();
		if (dist <= SHID_FART_RANGE
		    && FVisible(pPrey->v.origin, pE)
		    && pBot->f_shidden_fart_cooldown <= gpGlobals->time
		    && pBot->current_weapon.iId != VALVE_WEAPON_KNIFE)
		{
			pE->v.button |= IN_ATTACK;
			pBot->f_shidden_fart_cooldown = gpGlobals->time
			                                + SHID_FART_COOLDOWN;
		}

		return true;
	}

	// --- SMELTER ----------------------------------------------------------
	if (team == SHIDDEN_SMELTER)
	{
		// We're frozen — engine FL_FROZEN already locks input, but
		// strip buttons + clear pickup so nothing leaks through.
		if (pE->v.iuser4 > 0)
		{
			pBot->pBotEnemy   = NULL;
			pBot->f_move_speed = 0.0f;
			pBot->f_pause_time = gpGlobals->time + 0.1f;
			pE->v.button     &= ~(IN_ATTACK | IN_ATTACK2 | IN_JUMP);
			return true;
		}

		// Periodic role eval.
		if (pBot->f_shidden_role_eval_time <= gpGlobals->time)
		{
			pBot->f_shidden_role_eval_time = gpGlobals->time
			                                  + SHID_ROLE_EVAL_PERIOD;

			edict_t *pFrozen = BotShiddenClosest(pBot,
				s_cache.pFrozenSmelters, s_cache.n_frozen_smelters,
				false, NULL);

			if (pFrozen)
				pBot->i_shidden_role = SHIDDEN_ROLE_DEFENDER;
			else if (s_cache.n_smelters >= 2)
				pBot->i_shidden_role = SHIDDEN_ROLE_FLOCK;
			else
				pBot->i_shidden_role = SHIDDEN_ROLE_SCOUT;
		}

		// ---- DEFENDER --------------------------------------------------
		// A teammate is frozen.  Try to spot the dealter (invisible to
		// players, but FVisible passes through render-fx) near the
		// frozen teammate.  Force-target any visible dealter we find
		// within scan radius so BotShootAtEnemy actually fires.
		if (pBot->i_shidden_role == SHIDDEN_ROLE_DEFENDER)
		{
			edict_t *pFrozen = BotShiddenClosest(pBot,
				s_cache.pFrozenSmelters, s_cache.n_frozen_smelters,
				false, NULL);

			if (!pFrozen)
			{
				// State changed mid-tick; demote and fall through.
				pBot->i_shidden_role = SHIDDEN_ROLE_FLOCK;
			}
			else
			{
				pBot->v_goal           = pFrozen->v.origin;
				pBot->f_goal_proximity = 96.0f;
				pBot->f_pause_time     = 0.0f;
				pBot->f_move_speed     = pBot->f_max_speed;

				edict_t *pBestDealter = NULL;
				float    bestDealterDist = SHID_DEFEND_SCAN_RADIUS;
				for (int i = 0; i < s_cache.n_dealters; i++)
				{
					edict_t *pD = s_cache.pDealters[i];
					if (FNullEnt(pD)) continue;

					float fromFrozen =
						(pD->v.origin - pFrozen->v.origin).Length();
					if (fromFrozen > bestDealterDist) continue;
					if (!FVisible(pD->v.origin, pE)) continue;

					pBestDealter   = pD;
					bestDealterDist = fromFrozen;
				}

				if (pBestDealter) pBot->pBotEnemy = pBestDealter;

				return true;
			}
		}

		// ---- FLOCK -----------------------------------------------------
		if (pBot->i_shidden_role == SHIDDEN_ROLE_FLOCK)
		{
			Vector c = BotShiddenCentroid(s_cache.pSmelters,
			                              s_cache.n_smelters, pE);
			if (c == g_vecZero) return false;

			float distToCentroid = (c - pE->v.origin).Length();

			// Close enough to the pack — slow to roam pace so we don't
			// overshoot.  Outside the pack — sprint in.
			if (distToCentroid < SHID_FLOCK_RADIUS)
				pBot->f_move_speed = pBot->f_max_speed * 0.65f;
			else
				pBot->f_move_speed = pBot->f_max_speed;

			const int jitter = (ENTINDEX(pE) * 71) % 96;
			c.x += (float)(jitter - 48);
			c.y += (float)((jitter * 13) % 96 - 48);

			pBot->v_goal           = c;
			pBot->f_goal_proximity = 96.0f;
			pBot->f_pause_time     = 0.0f;

			// Look around: if any dealter is visible within spot range,
			// force-target so the bot returns fire as soon as it sees one.
			// While the "unseen attacker" alert is active (just took fart
			// damage), widen the range and drop the view-cone gate so we
			// swing around to engage even outside our current facing.
			const bool bAlerted =
				pBot->f_shidden_unseen_until > gpGlobals->time;
			const float spotRange = bAlerted
				? SHID_DEALTER_SPOT_RANGE * 1.5f
				: SHID_DEALTER_SPOT_RANGE;
			for (int i = 0; i < s_cache.n_dealters; i++)
			{
				edict_t *pD = s_cache.pDealters[i];
				if (FNullEnt(pD)) continue;
				float d = (pD->v.origin - pE->v.origin).Length();
				if (d > spotRange) continue;
				if (!bAlerted
				    && !FInViewCone(&pD->v.origin, pE)) continue;
				if (!FVisible(pD->v.origin, pE))       continue;
				pBot->pBotEnemy = pD;
				break;
			}

			return true;
		}

		// ---- SCOUT -----------------------------------------------------
		// Solo — fall through to default waypoint roam.  We still
		// opportunistically force-target a visible dealter.
		{
			const bool bAlerted =
				pBot->f_shidden_unseen_until > gpGlobals->time;
			const float spotRange = bAlerted
				? SHID_DEALTER_SPOT_RANGE * 1.5f
				: SHID_DEALTER_SPOT_RANGE;
			for (int i = 0; i < s_cache.n_dealters; i++)
			{
				edict_t *pD = s_cache.pDealters[i];
				if (FNullEnt(pD)) continue;
				float d = (pD->v.origin - pE->v.origin).Length();
				if (d > spotRange) continue;
				if (!bAlerted
				    && !FInViewCone(&pD->v.origin, pE)) continue;
				if (!FVisible(pD->v.origin, pE))       continue;
				pBot->pBotEnemy = pD;
				break;
			}
		}
		return false;
	}

	return false;
}
