//
// bot_prophunt.cpp — grave-bot AI for the Prop Hunt gameplay (GAME_PROPHUNT).
//
// Roles
// -----
// Props  (pev->fuser4 >= 1, value 1..30 doubles as decoy body index)
//   FREEZE      — game start; walk to chosen hide spot, do not attack
//   HIDE        — at hide spot; stand still, morph to match a neighbouring
//                 world-item, optionally drop a decoy
//   PANIC       — hunter within ~512u with LOS; flee to a fresh hide spot,
//                 may drop a decoy (IN_RELOAD), may attack with fists
//   DESPERATE   — last prop standing buff active (glow shell + extra HP +
//                 grenade resupply); plays aggressively with grenades
//
// Hunters (pev->fuser4 == 0)
//   HUNT_SEARCH — sweep cluster centroids of nearby world-items / decoys,
//                 micro-yaw look-around at each waypoint
//   HUNT_PURSUE — confirmed prop / suspect target → engage
//   HUNT_HELP   — teammate engaged → flank toward the same suspect
//
// Cross-DLL signals consumed
//   pev->fuser4   prop body slot (1..30) / 0 for hunter
//   pev->fuser3   freeze-timer signal (>1 = frozen until time, 1 = unstick)
//   pev->renderfx kRenderFxGlowShell on the last-prop-standing buff
//
// Implementation mirrors the BotLoot* / BotHorde* pre-update + think pattern.
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

// Tunables
static const float PP_HIDE_ARRIVE_DIST    = 96.0f;
static const float PP_PANIC_HUNTER_DIST   = 512.0f;
static const float PP_HUNTER_SEARCH_DIST  = 1024.0f;
static const float PP_MORPH_COOLDOWN      = 0.6f;
static const float PP_ROLE_EVAL_PERIOD    = 0.75f;
static const float PP_DECOY_COOLDOWN      = 6.0f;
static const float PP_HIDE_REPICK_DELAY   = 8.0f;
static const int   PP_MAX_MORPH_PRESSES   = 4;   // per role-eval tick
static const float PP_HUNTER_LOOKAROUND   = 1.2f;
static const float PP_HUNTER_SEARCH_PERIOD= 3.0f;
// Minimum distance any hide-spot anchor (and any chosen hide spot) must
// keep from EVERY hunter — frozen or not.  During the prophunt freeze
// window hunters are stationary but the prop should still treat them as
// poison: clustering on a pickup next to a paused hunter is a death
// sentence the moment the freeze ends.
static const float PP_HUNTER_ANCHOR_AVOID = 384.0f;
// Prop panic radius — any hunter (frozen or not, LOS or not) inside this
// range scares the prop into a fresh hide spot.  Larger than the LOS
// panic gate because frozen hunters around corners are still dangerous.
static const float PP_HUNTER_FEAR_DIST    = 640.0f;
// If a hunter is closer than this and roughly along the prop's heading,
// the prop double-/triple-jumps and tries to bound past instead of
// pathing straight into them.
static const float PP_HUNTER_BLOCK_DIST   = 160.0f;

//=========================================================
// Helpers
//=========================================================

static inline bool PP_IsProp(edict_t *p)
{
	return p && p->v.fuser4 >= 1;
}

static inline bool PP_IsHunter(edict_t *p)
{
	return p && p->v.fuser4 == 0;
}

static inline bool PP_IsDesperate(edict_t *p)
{
	return p && p->v.renderfx == kRenderFxGlowShell;
}

static inline bool PP_HunterIsFrozen(edict_t *p)
{
	// Server freezes hunters with EnableControl(FALSE) which sets
	// pev->flags |= FL_FROZEN.  fuser3 is only stamped on PROPS at round
	// start (it's the round freeze-end time the prop bot uses for its own
	// bFrozen gate) so it's NOT a reliable hunter-frozen signal \u2014 stale
	// values from a player's previous-round prop role would lie here.
	return p && (p->v.flags & FL_FROZEN) != 0;
}

// Dangerous / volatile entities a prop must NEVER use as a hide anchor
// and must NEVER attack.  These either explode, chase, or are mid-flight
// projectiles — hugging them is suicide.
static bool PP_IsHazardous(const char *cls)
{
	if (!cls || !*cls) return true;
	// Grenades / satchels / projectiles
	if (FStrEq(cls, "grenade") || FStrEq(cls, "rpg_rocket")
		|| FStrEq(cls, "crossbow_bolt") || FStrEq(cls, "bolt")
		|| FStrEq(cls, "hornet") || FStrEq(cls, "laser_spot"))
		return true;
	// Live monsters — NPCs, satchels, snarks, propdecoys.
	if (!strncmp(cls, "monster_", 8))
		return true;
	// Tripmines & beams.
	if (FStrEq(cls, "weapon_tripmine") || FStrEq(cls, "env_laser"))
		return true;
	return false;
}

// True if this classname is a safe, prop-like world entity — a pickup,
// ammo box, or static map prop.  We bias the hide-spot picker toward
// these so bots cluster on things that already look like props.
static bool PP_IsPickup(const char *cls)
{
	if (!cls || !*cls) return false;
	return !strncmp(cls, "weapon_", 7)
		|| !strncmp(cls, "ammo_", 5)
		|| !strncmp(cls, "item_", 5)
		|| !strncmp(cls, "prop_", 5)
		|| FStrEq(cls, "func_breakable")
		|| FStrEq(cls, "cycler") || FStrEq(cls, "cycler_sprite");
}

// Map a classname to a target prop body index.
//   1..51  → rendered via models/w_weapons.mdl  (weapon-shaped)
//   52..70 → rendered via models/w_ammo.mdl     (ammo/item-shaped)
// We use a stable djb2-style hash on the classname so the same world
// entity class always lands on the same body — deterministic disguises.
static int PP_BodyForClassname(const char *cls)
{
	if (!cls || !*cls) return 1;
	unsigned h = 5381;
	for (const char *p = cls; *p; ++p)
		h = ((h << 5) + h) + (unsigned char)*p;

	// Weapons stay in the weapon range, everything else (ammo / item /
	// generic pickup) lands in the ammo range.
	if (!strncmp(cls, "weapon_", 7))
	{
		int span = 51;       // 1..51
		return 1 + (int)(h % (unsigned)span);
	}
	int span = 70 - 52 + 1; // 52..70
	return 52 + (int)(h % (unsigned)span);
}

// Find the closest world-item the prop can use as a hide-spot anchor.
// Prefers `weapon_*` / `ammo_*` / `item_*` pickups; never returns a
// hazardous entity (grenade, monster_*, tripmine, projectile).
static bool PP_AnyHunterNear(const Vector &pos, float radius);  // fwd decl
static edict_t *PP_FindHideAnchor(edict_t *pEdict, float maxDist, edict_t *pAvoid)
{
	edict_t *pBest         = NULL;
	edict_t *pBestFallback = NULL;
	float    bestDist      = maxDist;
	float    fallbackDist  = maxDist;
	edict_t *pEnt          = NULL;

	while ((pEnt = UTIL_FindEntityInSphere(pEnt, pEdict->v.origin, maxDist)) != NULL)
	{
		if (pEnt == pEdict || pEnt == pAvoid)
			continue;
		if ((pEnt->v.flags & FL_CLIENT) || (pEnt->v.flags & FL_FAKECLIENT))
			continue;
		if (pEnt->v.solid == SOLID_NOT && pEnt->v.movetype == MOVETYPE_NONE)
			continue;
		if (pEnt->v.modelindex == 0)
			continue;
		const char *cls = STRING(pEnt->v.classname);
		if (!cls || !*cls) continue;
		if (FStrEq(cls, "worldspawn") || FStrEq(cls, "player"))
			continue;
		if (PP_IsHazardous(cls))
			continue;
		// "Very scared" — refuse to hide on any anchor sitting in a
		// hunter's lap, even a paused one.  The prop would otherwise
		// happily run straight at a frozen hunter because the pickup
		// next to them was the closest weapon_*.
		if (PP_AnyHunterNear(pEnt->v.origin, PP_HUNTER_ANCHOR_AVOID))
			continue;

		float d = (pEnt->v.origin - pEdict->v.origin).Length();
		if (PP_IsPickup(cls))
		{
			if (d < bestDist) { bestDist = d; pBest = pEnt; }
		}
		else
		{
			if (d < fallbackDist) { fallbackDist = d; pBestFallback = pEnt; }
		}
	}
	return pBest ? pBest : pBestFallback;
}

// Return distance to nearest hunter; sets out edict if non-NULL.
static float PP_NearestHunter(edict_t *pBot, edict_t **ppOut)
{
	float best = 999999.0f;
	if (ppOut) *ppOut = NULL;
	for (int i = 0; i < 32; ++i)
	{
		edict_t *p = clients[i];
		if (FNullEnt(p) || p == pBot)
			continue;
		if (!IsAlive(p))
			continue;
		if (!PP_IsHunter(p))
			continue;
		float d = (p->v.origin - pBot->v.origin).Length();
		if (d < best)
		{
			best = d;
			if (ppOut) *ppOut = p;
		}
	}
	return best;
}

// True if ANY live hunter (frozen or not, LOS or not) is within `radius`
// of `pos`.  Used to reject hide spots near paused hunters so the prop
// doesn't shelter in the hunter's lap.
static bool PP_AnyHunterNear(const Vector &pos, float radius)
{
	for (int i = 0; i < 32; ++i)
	{
		edict_t *p = clients[i];
		if (FNullEnt(p) || !IsAlive(p) || !PP_IsHunter(p))
			continue;
		if ((p->v.origin - pos).Length() < radius)
			return true;
	}
	return false;
}

// Pick a search target for a hunter — the centroid of the densest
// cluster of world-items or visible decoys within range.
static Vector PP_PickHunterSearch(edict_t *pEdict)
{
	Vector best = g_vecZero;
	int    bestCount = 0;
	edict_t *pSeed = NULL;

	while ((pSeed = UTIL_FindEntityInSphere(pSeed, pEdict->v.origin, PP_HUNTER_SEARCH_DIST)) != NULL)
	{
		if (pSeed == pEdict)
			continue;
		const char *cls = STRING(pSeed->v.classname);
		if (!cls || !*cls)
			continue;
		bool isDecoy = FStrEq(cls, "monster_propdecoy");
		// Seed clusters on safe pickups + decoys only.
		if (!isDecoy && !PP_IsPickup(cls))
			continue;
		if (pSeed->v.modelindex == 0)
			continue;

		Vector centroid = pSeed->v.origin;
		int    count    = 1;
		edict_t *pScan  = NULL;
		while ((pScan = UTIL_FindEntityInSphere(pScan, pSeed->v.origin, 256.0f)) != NULL)
		{
			if (pScan == pSeed || pScan == pEdict)
				continue;
			if (pScan->v.modelindex == 0)
				continue;
			const char *c2 = STRING(pScan->v.classname);
			if (!c2 || !PP_IsPickup(c2)) continue;
			centroid = centroid + pScan->v.origin;
			++count;
		}
		int weight = count + (isDecoy ? 2 : 0);
		if (weight > bestCount)
		{
			bestCount = weight;
			best      = centroid * (1.0f / count);
		}
	}
	return best;
}

//=========================================================
// BotProphuntPreUpdate — runs BEFORE BotFindEnemy every
// frame.  Owns role evaluation, hide-spot selection, morph
// driving, and v_goal pre-set.  Also clamps pBotEnemy so
// hunters don't fixate on frozen props and props don't
// shoot at distant hunters they can't see.
//=========================================================
void BotProphuntPreUpdate( bot_t *pBot )
{
	if (is_gameplay != GAME_PROPHUNT)
		return;

	edict_t *pEdict = pBot->pEdict;
	if (FNullEnt(pEdict) || !IsAlive(pEdict))
		return;

	const bool bIsProp     = PP_IsProp(pEdict);
	const bool bIsHunter   = PP_IsHunter(pEdict);
	const bool bDesperate  = bIsProp && PP_IsDesperate(pEdict);
	const float freezeEnds = pEdict->v.fuser3;
	const bool bFrozen     = (freezeEnds > 1.0f && freezeEnds > gpGlobals->time);

	// Throttled role evaluation.
	if (pBot->f_pp_role_eval_time < gpGlobals->time)
	{
		pBot->f_pp_role_eval_time = gpGlobals->time + PP_ROLE_EVAL_PERIOD;

		if (bIsProp)
		{
			if (bDesperate)
				pBot->i_pp_role = PROP_ROLE_DESPERATE;
			else if (bFrozen)
				pBot->i_pp_role = PROP_ROLE_FREEZE;
			else
			{
				edict_t *pHunter = NULL;
				float dh = PP_NearestHunter(pEdict, &pHunter);
				// Very scared: any hunter — frozen or roaming, LOS or not
				// — inside the fear radius forces a panic + repick.  The
				// old gate used FVisible which meant paused hunters
				// around a corner were "safe", causing props to settle a
				// few feet away from a stationary hunter.
				if (!FNullEnt(pHunter) && dh < PP_HUNTER_FEAR_DIST)
				{
					pBot->i_pp_role      = PROP_ROLE_PANIC;
					pBot->f_pp_panic_until = gpGlobals->time + 4.0f;
				}
				else if (pBot->f_pp_panic_until > gpGlobals->time)
					pBot->i_pp_role = PROP_ROLE_PANIC;
				else
				{
					// Arrived at hide spot → HIDE, else still FREEZE-walking.
					float dh2 = (pBot->v_pp_hide_spot - pEdict->v.origin).Length2D();
					if (pBot->v_pp_hide_spot != g_vecZero && dh2 < PP_HIDE_ARRIVE_DIST)
						pBot->i_pp_role = PROP_ROLE_HIDE;
					else
						pBot->i_pp_role = PROP_ROLE_FREEZE;
				}
			}
		}
		else if (bIsHunter)
		{
			// Hunter: HELP if a teammate already has a live target on a prop,
			// SEARCH otherwise.  PURSUE is implicitly set by BotFindEnemy
			// returning a prop — we represent that here only for stats.
			bool teamEngaged = false;
			for (int i = 0; i < 32; ++i)
			{
				edict_t *p = clients[i];
				if (FNullEnt(p) || p == pEdict)
					continue;
				if (!IsAlive(p) || !PP_IsHunter(p))
					continue;
				// Teammate has an enemy ref? bot struct lookup — only valid
				// for our own bots.  Skip if their bot index isn't ours.
				int idx = -1;
				for (int j = 0; j < 32; ++j) if (bots[j].pEdict == p) { idx = j; break; }
				if (idx >= 0 && !FNullEnt(bots[idx].pBotEnemy)
					&& PP_IsProp(bots[idx].pBotEnemy))
				{
					teamEngaged = true;
					break;
				}
			}
			if (!FNullEnt(pBot->pBotEnemy) && PP_IsProp(pBot->pBotEnemy))
				pBot->i_pp_role = PROP_ROLE_HUNT_PURSUE;
			else if (teamEngaged)
				pBot->i_pp_role = PROP_ROLE_HUNT_HELP;
			else
				pBot->i_pp_role = PROP_ROLE_HUNT_SEARCH;
		}
	}

	// ------------- Enemy clamping ----------------------------------------
	// Same-team enemy?  After a prop is converted to a hunter, the hunter
	// that landed the conversion still holds the now-teammate as pBotEnemy.
	// Clear it so the hunter resumes searching.
	if (!FNullEnt(pBot->pBotEnemy))
	{
		const bool enemyIsProp   = PP_IsProp(pBot->pBotEnemy);
		const bool enemyIsHunter = PP_IsHunter(pBot->pBotEnemy);
		if ((bIsHunter && enemyIsHunter) || (bIsProp && enemyIsProp))
			pBot->pBotEnemy = NULL;
	}

	// Hunters never target props during the freeze window — the server
	// freezes hunters with EnableControl(FALSE) which sets FL_FROZEN on
	// the player.  fuser3 is NOT set on hunters (props only), so we must
	// read the flag directly.
	if (bIsHunter && !FNullEnt(pBot->pBotEnemy) && PP_IsProp(pBot->pBotEnemy))
	{
		if ((pEdict->v.flags & FL_FROZEN) != 0)
			pBot->pBotEnemy = NULL;
	}
	// Props ignore hunters entirely while frozen (no engaging, no panic).
	if (bIsProp && bFrozen)
		pBot->pBotEnemy = NULL;
	// Props NEVER target a frozen hunter — the hunter is helpless and the
	// noise just reveals the prop.  Drop the lock and let panic/hide drive.
	if (bIsProp && !FNullEnt(pBot->pBotEnemy) && PP_IsHunter(pBot->pBotEnemy)
		&& PP_HunterIsFrozen(pBot->pBotEnemy))
		pBot->pBotEnemy = NULL;
	// Props only engage hunters when very close + LOS, otherwise the
	// noise gives them away.  DESPERATE props are more aggressive.
	if (bIsProp && !FNullEnt(pBot->pBotEnemy) && PP_IsHunter(pBot->pBotEnemy))
	{
		float dh = (pBot->pBotEnemy->v.origin - pEdict->v.origin).Length();
		float gate = bDesperate ? 384.0f : 192.0f;
		if (dh > gate)
			pBot->pBotEnemy = NULL;
	}

	// ------------- Prop-side actions -------------------------------------
	if (bIsProp)
	{
		// While frozen: strip any pending attack/reload buttons so bots don't
		// throw grenades or swing fists while running to a hide spot.  We do
		// NOT pause movement — props are meant to scatter during the freeze
		// window.  The morph driver below is also gated on !bFrozen.
		if (bFrozen)
		{
			pBot->f_pause_time = 0;
			pEdict->v.button &= ~(IN_ATTACK | IN_ATTACK2 | IN_RELOAD);
		}

		// Pick a hide spot the first time, after panic ends, or after a stale
		// pick has been held for too long.
		bool needsPick = (pBot->v_pp_hide_spot == g_vecZero)
			|| (pBot->i_pp_role == PROP_ROLE_PANIC
			    && pBot->f_pp_hide_arrived_time > 0
			    && (gpGlobals->time - pBot->f_pp_hide_arrived_time) > 2.0f)
			|| (pBot->f_pp_hide_arrived_time > 0
			    && (gpGlobals->time - pBot->f_pp_hide_arrived_time) > PP_HIDE_REPICK_DELAY
			    && pBot->i_pp_role != PROP_ROLE_HIDE);

		// Sanity: if the current hide spot has become compromised because a
		// hunter wandered next to it (or was always there but is now
		// alive/visible to us), throw it out and pick again.  Runs every
		// frame, not just on the throttled role-eval tick.
		if (!needsPick && pBot->v_pp_hide_spot != g_vecZero
			&& PP_AnyHunterNear(pBot->v_pp_hide_spot, PP_HUNTER_ANCHOR_AVOID))
		{
			pBot->v_pp_hide_spot         = g_vecZero;
			pBot->p_pp_target_item       = NULL;
			pBot->f_pp_hide_arrived_time = 0;
			pBot->waypoint_goal          = -1;
			pBot->old_waypoint_goal      = -1;
			pBot->f_waypoint_goal_time   = 0.0f;
			needsPick                    = true;
		}

		if (needsPick)
		{
			edict_t *pAnchor = PP_FindHideAnchor(pEdict, 1024.0f, pBot->p_pp_target_item);
			if (!FNullEnt(pAnchor))
			{
				pBot->p_pp_target_item       = pAnchor;
				pBot->v_pp_hide_spot         = pAnchor->v.origin;
				pBot->f_pp_hide_arrived_time = gpGlobals->time;
				// Match the body slot to the anchor's classname (deterministic
				// hash → 1..51 for weapon-shaped, 52..70 for ammo/item-shaped).
				const char *aCls = STRING(pAnchor->v.classname);
				pBot->i_pp_target_body       = PP_BodyForClassname(aCls);
				pBot->waypoint_goal          = -1;
				pBot->old_waypoint_goal      = -1;
				pBot->f_waypoint_goal_time   = 0.0f;
			}
		}

		// Morph driver — step pev->fuser4 toward i_pp_target_body using
		// the shorter wrap-around direction.  IN_ATTACK steps +1, IN_ATTACK2
		// steps -1 (server enforces the 0.3 s morph cooldown via fuser2).
		if (pBot->i_pp_target_body >= 1 && pBot->i_pp_target_body <= PROP_BODY_MAX
			&& (int)pEdict->v.fuser4 != pBot->i_pp_target_body
			&& pBot->f_pp_next_morph < gpGlobals->time
			&& !bFrozen)
		{
			int cur = (int)pEdict->v.fuser4;
			if (cur < 1) cur = 1;
			int tgt = pBot->i_pp_target_body;
			int fwd = (tgt - cur + PROP_BODY_MAX) % PROP_BODY_MAX;
			int bwd = (cur - tgt + PROP_BODY_MAX) % PROP_BODY_MAX;
			if (fwd <= bwd)
				pEdict->v.button |= IN_ATTACK;
			else
				pEdict->v.button |= IN_ATTACK2;
			pBot->f_pp_next_morph = gpGlobals->time + PP_MORPH_COOLDOWN;
		}

		// Optional decoy drop while panicking (server maps IN_RELOAD or
		// secondary fire to decoy spawn for props in most builds; we use
		// IN_RELOAD which is unused for fists).
		if ((pBot->i_pp_role == PROP_ROLE_PANIC || bDesperate)
			&& pBot->f_pp_decoy_drop_time < gpGlobals->time)
		{
			pEdict->v.button |= IN_RELOAD;
			pBot->f_pp_decoy_drop_time = gpGlobals->time + PP_DECOY_COOLDOWN;
		}

		// Pre-set v_goal toward the chosen hide spot — BotFindWaypointGoal
		// uses this on the very first frame after spawn.
		if (pBot->v_pp_hide_spot != g_vecZero)
		{
			pBot->v_goal           = pBot->v_pp_hide_spot;
			pBot->f_goal_proximity = PP_HIDE_ARRIVE_DIST;
		}

		// Hunter-in-path dodge: if a hunter (frozen or not) is close and
		// roughly along the line toward our hide spot, kick IN_JUMP every
		// frame the obstruction lasts.  The nav system keeps pushing us
		// forward, the repeated jumps produce a double/triple bound that
		// hops over (or at least beside) the blocking hunter instead of
		// running into them.
		if (pBot->v_pp_hide_spot != g_vecZero)
		{
			Vector fwd2d = pBot->v_pp_hide_spot - pEdict->v.origin;
			fwd2d.z = 0;
			float fwdLen = fwd2d.Length();
			if (fwdLen > 1.0f)
			{
				Vector fwdN = fwd2d * (1.0f / fwdLen);
				for (int i = 0; i < 32; ++i)
				{
					edict_t *p = clients[i];
					if (FNullEnt(p) || p == pEdict) continue;
					if (!IsAlive(p) || !PP_IsHunter(p)) continue;
					Vector toH = p->v.origin - pEdict->v.origin;
					toH.z = 0;
					float dh3 = toH.Length();
					if (dh3 < 1.0f || dh3 > PP_HUNTER_BLOCK_DIST) continue;
					float dot = DotProduct(fwdN, toH * (1.0f / dh3));
					if (dot < 0.4f) continue;  // not ahead in our heading cone
					pEdict->v.button |= IN_JUMP;
					break;
				}
			}
		}
	}

	// ------------- Hunter-side actions -----------------------------------
	if (bIsHunter)
	{
		// While frozen: full pause — the server freezes the hunter in place
		// via EnableControl(FALSE) which sets FL_FROZEN.  The bot must not
		// jump, fire, target, or path.  Strip every action button and zero
		// the goal so navigation doesn't fight the freeze — even a buffered
		// IN_JUMP can squeak through the engine's frozen-player gate.
		if ((pEdict->v.flags & FL_FROZEN) != 0)
		{
			pBot->pBotEnemy   = NULL;
			pBot->f_move_speed = 0.0f;
			pBot->v_goal       = g_vecZero;
			pBot->f_pause_time = gpGlobals->time + 0.25f;
			pEdict->v.button  &= ~(IN_ATTACK | IN_ATTACK2 | IN_RELOAD
			                       | IN_JUMP | IN_DUCK | IN_FORWARD | IN_BACK
			                       | IN_MOVELEFT | IN_MOVERIGHT);
			return;
		}

		// Refresh search cluster at PP_HUNTER_SEARCH_PERIOD cadence or when
		// the previous cluster has been reached.
		bool reached = (pBot->v_pp_search_target != g_vecZero)
			&& ((pBot->v_pp_search_target - pEdict->v.origin).Length2D() < 128.0f);
		if (pBot->f_pp_search_pick_time < gpGlobals->time || reached
			|| pBot->v_pp_search_target == g_vecZero)
		{
			Vector v = PP_PickHunterSearch(pEdict);
			if (v != g_vecZero)
			{
				pBot->v_pp_search_target = v;
				pBot->f_pp_search_pick_time = gpGlobals->time + PP_HUNTER_SEARCH_PERIOD;
				if (reached)
					pBot->f_pp_lookaround_until = gpGlobals->time + PP_HUNTER_LOOKAROUND;
				pBot->waypoint_goal        = -1;
				pBot->old_waypoint_goal    = -1;
				pBot->f_waypoint_goal_time = 0.0f;
			}
		}

		if (FNullEnt(pBot->pBotEnemy) && pBot->v_pp_search_target != g_vecZero)
		{
			pBot->v_goal           = pBot->v_pp_search_target;
			pBot->f_goal_proximity = 96.0f;
		}
	}
}

//=========================================================
// BotProphuntThink — fallback driver when no enemy is
// active.  Returns true when v_goal + f_move_speed have
// been set so the generic BotThink branch can fall through
// without clobbering them.
//=========================================================
bool BotProphuntThink( bot_t *pBot )
{
	if (is_gameplay != GAME_PROPHUNT)
		return false;

	edict_t *pEdict = pBot->pEdict;
	if (FNullEnt(pEdict) || !IsAlive(pEdict))
		return false;

	const bool bIsProp   = PP_IsProp(pEdict);
	const bool bIsHunter = PP_IsHunter(pEdict);

	if (bIsProp)
	{
		// Stand still once at hide spot — server can't see us either way,
		// movement is the give-away.
		if (pBot->i_pp_role == PROP_ROLE_HIDE)
		{
			pBot->v_goal     = pEdict->v.origin;
			pBot->f_move_speed = 0.0f;
			pBot->pBotPickupItem = NULL;
			pBot->item_waypoint  = -1;
			return true;
		}
		if (pBot->v_pp_hide_spot != g_vecZero)
		{
			pBot->v_goal           = pBot->v_pp_hide_spot;
			pBot->f_goal_proximity = PP_HIDE_ARRIVE_DIST;
			pBot->f_move_speed     = pBot->f_max_speed;
			pBot->pBotPickupItem   = NULL;
			pBot->item_waypoint    = -1;
			return true;
		}
		return false;
	}

	if (bIsHunter)
	{
		if (pBot->v_pp_search_target != g_vecZero)
		{
			pBot->v_goal           = pBot->v_pp_search_target;
			pBot->f_goal_proximity = 96.0f;
			pBot->f_move_speed     = pBot->f_max_speed;

			// Micro yaw-sweep at the cluster — server-side this just nudges
			// the ideal yaw, our normal aim smoothing handles the rest.
			if (pBot->f_pp_lookaround_until > gpGlobals->time)
			{
				float swing = sin(gpGlobals->time * 4.0f) * 30.0f;
				pEdict->v.ideal_yaw = pEdict->v.v_angle.y + swing;
			}
			return true;
		}
	}

	return false;
}
