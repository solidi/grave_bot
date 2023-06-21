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
#include "bot_weapons.h"

bot_weapon_select_t valve_weapon_select[] =
{
	// crowbar
	{
		VALVE_WEAPON_CROWBAR,	// id
		"weapon_crowbar",		// classname
		"crowbar",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		10,						// priority
		0.0,					// min primary distance
		75.0,					// max primary distance
		0.0,					// min secondary distance
		1000.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		90,						// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// egon
	{
		VALVE_WEAPON_EGON,		// id
		"weapon_egon",			// classname
		"egon",					// third person model
		{						// primary ammo pickup classnames
			"ammo_gaussclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		5,						// priority
		32.0,					// min primary distance
		1024.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		FALSE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		TRUE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// gauss
	{
		VALVE_WEAPON_GAUSS,		// id
		"weapon_gauss",			// classname
		"gauss",				// third person model
		{						// primary ammo pickup classnames
			"ammo_gaussclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		5,						// priority
		0.0,					// min primary distance
		9999.0,					// max primary distance
		0.0,					// min secondary distance
		9999.0,					// max secondary distance
		FALSE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		75,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		TRUE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.8						// time to charge weapon (secondary)
	},
	// rpg
	{
		VALVE_WEAPON_RPG,		// id
		"weapon_rpg",			// classname
		"rpg",					// third person model
		{						// primary ammo pickup classnames
			"ammo_rpgclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		4,						// priority
		250.0,					// min primary distance
		9999.0,					// max primary distance
		250.0,					// min secondary distance
		9999.0,					// max secondary distance
		TRUE,					// can use underwater?
		2.0,					// how long does this weapon take to reload?
		50,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// shotgun
	{
		VALVE_WEAPON_SHOTGUN,	// id
		"weapon_shotgun",		// classname
		"shotgun",	// third person model
		{						// primary ammo pickup classnames
			"ammo_buckshot",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		6,						// priority
		0.0,					// min primary distance
		300.0,					// max primary distance
		0.0,					// min secondary distance
		300.0,					// max secondary distance
		FALSE,					// can use underwater?
		2.0,					// how long does this weapon take to reload?
		60,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		2,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// crossbow
	{
		VALVE_WEAPON_CROSSBOW,	// id
		"weapon_crossbow",		// classname
		"crossbow",				// third person model
		{						// primary ammo pickup classnames
			"ammo_crossbow",
			""
		},
		{						// secondary ammo pickup classnames
			"",
			""
		},
		5,						// skill level
		6,						// priority
		50.0,					// min primary distance
		9999.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		TRUE,					// can use underwater?
		4.5,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// .357 python
	{
		VALVE_WEAPON_PYTHON,	// id
		"weapon_357",			// classname
		"357",					// third person model
		{						// primary ammo pickup classnames
			"ammo_357",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		7,						// priority
		30.0,					// min primary distance
		2048.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		FALSE,					// can use underwater?
		2.4,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// assault rifle (mp5)
	{
		VALVE_WEAPON_MP5,		// id
		"weapon_9mmAR",			// classname
		"9mmAR",				// third person model
		{						// primary ammo pickup classnames
			"ammo_9mmclip",
			"ammo_9mmAR",
		},
		{						// secondary ammo pickup classnames
			"ammo_ARgrenades",
			"",
		},
		5,						// skill level
		5,						// priority
		0.0,					// min primary distance
		786.0,					// max primary distance
		300.0,					// min secondary distance
		600.0,					// max secondary distance
		FALSE,					// can use underwater?
		1.5,					// how long does this weapon take to reload?
		90,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// 9mm pistol
	{
		VALVE_WEAPON_GLOCK,		// id
		"weapon_9mmhandgun",	// classname
		"9mmhandgun",// third person model
		{						// primary ammo pickup classnames
			"ammo_9mmAR",
			"ammo_9mmclip"
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		9,						// priority
		0.0,					// min primary distance
		9999.0,					// max primary distance
		0.0,					// min secondary distance
		256.0,					// max secondary distance
		TRUE,					// can use underwater?
		1.5,					// how long does this weapon take to reload?
		70,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// hornet gun
	{
		VALVE_WEAPON_HORNETGUN,	// id
		"weapon_hornetgun",		// classname
		"hgun",					// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		5,						// priority
		30.0,					// min primary distance
		1000.0,					// max primary distance
		30.0,					// min secondary distance
		1000.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		90,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		4,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		TRUE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// hand grenades
	{
		VALVE_WEAPON_HANDGRENADE,// id
		"weapon_handgrenade",	// classname
		"grenade",				// third person model
		{						// primary ammo pickup classnames
			"weapon_handgrenade",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		4,						// priority
		250.0,					// min primary distance
		750.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// snarks
	{
		VALVE_WEAPON_SNARK,		// id
		"weapon_snark",			// classname
		"squeak",	// third person model
		{						// primary ammo pickup classnames
			"weapon_snark",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		5,						// priority
		150.0,					// min primary distance
		500.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		FALSE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		90,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// vest
	{
		VALVE_WEAPON_VEST,	// id
		"weapon_vest",		// classname
		"vest",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		1,						// priority
		0.0,					// min primary distance
		400.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// knife
	{
		VALVE_WEAPON_KNIFE,	// id
		"weapon_knife",		// classname
		"knife",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		9,						// priority
		0.0,					// min primary distance
		50.0,					// max primary distance
		0.0,					// min secondary distance
		1000.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		90,						// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// chumtoad
	{
		VALVE_WEAPON_CHUMTOAD,		// id
		"weapon_chumtoad",			// classname
		"chumtoad",	// third person model
		{						// primary ammo pickup classnames
			"weapon_chumtoad",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		3,						// priority
		150.0,					// min primary distance
		500.0,					// max primary distance
		150.0,					// min secondary distance
		500.0,					// max secondary distance
		FALSE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		90,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		5,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// sniper rifle
	{
		VALVE_WEAPON_SNIPER_RIFLE,	// id
		"weapon_sniperrifle",		// classname
		"sniperrifle",				// third person model
		{						// primary ammo pickup classnames
			"ammo_crossbow",
			""
		},
		{						// secondary ammo pickup classnames
			"",
			""
		},
		5,						// skill level
		6,						// priority
		50.0,					// min primary distance
		9999.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		FALSE,					// can use underwater?
		1.5,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// railgun
	{
		VALVE_WEAPON_RAILGUN,		// id
		"weapon_railgun",			// classname
		"railgun",				// third person model
		{						// primary ammo pickup classnames
			"ammo_gaussclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		4,						// priority
		0.0,					// min primary distance
		9999.0,					// max primary distance
		0.0,					// min secondary distance
		9999.0,					// max secondary distance
		FALSE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		100,						// times out of 100 to use primary fire
		2,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		TRUE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// cannon
	{
		VALVE_WEAPON_CANNON,		// id
		"weapon_cannon",			// classname
		"cannon",					// third person model
		{						// primary ammo pickup classnames
			"ammo_flak",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		4,						// priority
		300.0,					// min primary distance
		9999.0,					// max primary distance
		300.0,					// min secondary distance
		9999.0,					// max secondary distance
		FALSE,					// can use underwater?
		0,					// how long does this weapon take to reload?
		50,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// mag 60
	{
		VALVE_WEAPON_MAG60,		// id
		"weapon_mag60",	// classname
		"mag60",// third person model
		{						// primary ammo pickup classnames
			"ammo_9mmAR",
			"ammo_9mmclip"
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		6,						// priority
		0.0,					// min primary distance
		9999.0,					// max primary distance
		0.0,					// min secondary distance
		256.0,					// max secondary distance
		FALSE,					// can use underwater?
		1.4,					// how long does this weapon take to reload?
		70,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		TRUE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// chaingun
	{
		VALVE_WEAPON_CHAINGUN,		// id
		"weapon_chaingun",			// classname
		"chaingun",				// third person model
		{						// primary ammo pickup classnames
			"ammo_9mmclip",
			"ammo_9mmAR",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		4,						// priority
		0.0,					// min primary distance
		786.0,					// max primary distance
		300.0,					// min secondary distance
		600.0,					// max secondary distance
		FALSE,					// can use underwater?
		2.5,					// how long does this weapon take to reload?
		100,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		TRUE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// grenade launcher
	{
		VALVE_WEAPON_GLAUNCHER,		// id
		"weapon_glauncher",			// classname
		"glauncher",				// third person model
		{						// primary ammo pickup classnames
			"ammo_ARgrenades",
			"",
		},
		{						// secondary ammo pickup classnames
			"ammo_ARgrenades",
			"",
		},
		5,						// skill level
		4,						// priority
		300.0,					// min primary distance
		600.0,					// max primary distance
		300.0,					// min secondary distance
		786.0,					// max secondary distance
		FALSE,					// can use underwater?
		1.5,					// how long does this weapon take to reload?
		50,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// smg
	{
		VALVE_WEAPON_SMG,		// id
		"weapon_smg",			// classname
		"smg",				// third person model
		{						// primary ammo pickup classnames
			"ammo_9mmclip",
			"ammo_9mmAR",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		6,						// priority
		0.0,					// min primary distance
		786.0,					// max primary distance
		300.0,					// min secondary distance
		600.0,					// max secondary distance
		FALSE,					// can use underwater?
		1.5,					// how long does this weapon take to reload?
		70,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		TRUE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// usas
	{
		VALVE_WEAPON_USAS,	// id
		"weapon_usas",		// classname
		"usas",	// third person model
		{						// primary ammo pickup classnames
			"ammo_buckshot",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		5,						// priority
		0.0,					// min primary distance
		300.0,					// max primary distance
		0.0,					// min secondary distance
		300.0,					// max secondary distance
		FALSE,					// can use underwater?
		1.5,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		2,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		TRUE,					// hold down secondary fire button to use?
		TRUE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// fists
	{
		VALVE_WEAPON_FISTS,	// id
		"weapon_fists",		// classname
		"fists",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		10,						// priority
		0.0,					// min primary distance
		100.0,					// max primary distance
		0.0,					// min secondary distance
		100.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		80,						// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// wrench
	{
		VALVE_WEAPON_WRENCH,	// id
		"weapon_wrench",		// classname
		"wrench",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		8,						// priority
		0.0,					// min primary distance
		75.0,					// max primary distance
		0.0,					// min secondary distance
		1000.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		90,						// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// snowball
	{
		VALVE_WEAPON_SNOWBALL,	// id
		"weapon_snowball",	// classname
		"snowball",				// third person model
		{						// primary ammo pickup classnames
			"weapon_snowball",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		9,						// priority
		50.0,					// min primary distance
		750.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		FALSE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		90,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// chainsaw
	{
		VALVE_WEAPON_CHAINSAW,	// id
		"weapon_chainsaw",		// classname
		"chainsaw",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		2,						// priority
		0.0,					// min primary distance
		75.0,					// max primary distance
		0.0,					// min secondary distance
		75.0,					// max secondary distance
		FALSE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		80,						// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		TRUE,					// hold down primary fire button to use?
		TRUE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// 12 gauge
	{
		VALVE_WEAPON_12GAUGE,	// id
		"weapon_12gauge",		// classname
		"12gauge",	// third person model
		{						// primary ammo pickup classnames
			"ammo_buckshot",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		6,						// priority
		0.0,					// min primary distance
		300.0,					// max primary distance
		0.0,					// min secondary distance
		300.0,					// max secondary distance
		FALSE,					// can use underwater?
		2.0,					// how long does this weapon take to reload?
		70,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		2,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// nuke
	{
		VALVE_WEAPON_NUKE,		// id
		"weapon_nuke",			// classname
		"nuke",					// third person model
		{						// primary ammo pickup classnames
			"ammo_rpgclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		1,						// priority
		300.0,					// min primary distance
		9999.0,					// max primary distance
		300.0,					// min secondary distance
		9999.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,					// how long does this weapon take to reload?
		50,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// deagle
	{
		VALVE_WEAPON_DEAGLE,	// id
		"weapon_deagle",			// classname
		"357",					// third person model
		{						// primary ammo pickup classnames
			"ammo_357",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		6,						// priority
		30.0,					// min primary distance
		2048.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		FALSE,					// can use underwater?
		1.7,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// dual deagle
	{
		VALVE_WEAPON_DUAL_DEAGLE,	// id
		"weapon_dual_deagle",	// classname
		"dual_deagle",					// third person model
		{						// primary ammo pickup classnames
			"ammo_357",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		5,						// priority
		30.0,					// min primary distance
		2048.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		FALSE,					// can use underwater?
		2.5,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// dual rpg
	{
		VALVE_WEAPON_DUAL_RPG,		// id
		"weapon_dual_rpg",			// classname
		"dual_rpg",					// third person model
		{						// primary ammo pickup classnames
			"ammo_rpgclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		2,						// priority
		250.0,					// min primary distance
		9999.0,					// max primary distance
		250.0,					// min secondary distance
		9999.0,					// max secondary distance
		TRUE,					// can use underwater?
		2.0,					// how long does this weapon take to reload?
		50,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// dual smg
	{
		VALVE_WEAPON_DUAL_SMG,		// id
		"weapon_dual_smg",			// classname
		"dual_smg",				// third person model
		{						// primary ammo pickup classnames
			"ammo_9mmclip",
			"ammo_9mmAR",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		3,						// priority
		0.0,					// min primary distance
		786.0,					// max primary distance
		300.0,					// min secondary distance
		600.0,					// max secondary distance
		FALSE,					// can use underwater?
		2.8,					// how long does this weapon take to reload?
		100,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// dual wrench
	{
		VALVE_WEAPON_DUAL_WRENCH,	// id
		"weapon_dual_wrench",		// classname
		"dual_wrench",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		3,						// priority
		0.0,					// min primary distance
		75.0,					// max primary distance
		0.0,					// min secondary distance
		1000.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		90,						// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// dual usas
	{
		VALVE_WEAPON_DUAL_USAS,	// id
		"weapon_dual_usas",		// classname
		"dual_usas",	// third person model
		{						// primary ammo pickup classnames
			"ammo_buckshot",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		2,						// priority
		0.0,					// min primary distance
		400.0,					// max primary distance
		0.0,					// min secondary distance
		400.0,					// max secondary distance
		FALSE,					// can use underwater?
		2.8,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		2,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		TRUE,					// hold down secondary fire button to use?
		TRUE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// freezegun
	{
		VALVE_WEAPON_FREEZEGUN,		// id
		"weapon_freezegun",			// classname
		"freezegun",				// third person model
		{						// primary ammo pickup classnames
			"ammo_gaussclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		5,						// priority
		0.0,					// min primary distance
		9999.0,					// max primary distance
		0.0,					// min secondary distance
		9999.0,					// max secondary distance
		FALSE,					// can use underwater?
		3.5,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// dual mag60
	{
		VALVE_WEAPON_DUAL_MAG60,		// id
		"weapon_dual_mag60",			// classname
		"dual_mag60",				// third person model
		{						// primary ammo pickup classnames
			"ammo_9mmclip",
			"ammo_9mmAR",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		3,						// priority
		0.0,					// min primary distance
		786.0,					// max primary distance
		300.0,					// min secondary distance
		600.0,					// max secondary distance
		FALSE,					// can use underwater?
		2.5,					// how long does this weapon take to reload?
		100,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// rocket crowbar
	{
		VALVE_WEAPON_ROCKETCROWBAR,	// id
		"weapon_rocketcrowbar",		// classname
		"rocketcrowbar",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		5,						// priority
		0.0,					// min primary distance
		75.0,					// max primary distance
		0.0,					// min secondary distance
		1000.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		50,						// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// dual railgun
	{
		VALVE_WEAPON_DUAL_RAILGUN,		// id
		"weapon_dual_railgun",			// classname
		"dual_railgun",				// third person model
		{						// primary ammo pickup classnames
			"ammo_gaussclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		3,						// priority
		0.0,					// min primary distance
		9999.0,					// max primary distance
		0.0,					// min secondary distance
		9999.0,					// max secondary distance
		FALSE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		50,						// times out of 100 to use primary fire
		2,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		TRUE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// gravity gun
	{
		VALVE_WEAPON_GRAVITYGUN,	// id
		"weapon_gravitygun",		// classname
		"gauss",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		9,						// priority
		0.0,					// min primary distance
		500.0,					// max primary distance
		0.0,					// min secondary distance
		500.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		50,						// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// flame thrower
	{
		VALVE_WEAPON_FLAMETHROWER,		// id
		"weapon_flamethrower",			// classname
		"flamethrower",					// third person model
		{						// primary ammo pickup classnames
			"ammo_gaussclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		5,						// priority
		32.0,					// min primary distance
		1024.0,					// max primary distance
		32.0,					// min secondary distance
		1024.0,					// max secondary distance
		FALSE,					// can use underwater?
		3,						// how long does this weapon take to reload?
		70,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		TRUE,					// hold down primary fire button to use?
		TRUE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// dual flame thrower
	{
		VALVE_WEAPON_DUAL_FLAMETHROWER,		// id
		"weapon_dual_flamethrower",			// classname
		"flamethrower",					// third person model
		{						// primary ammo pickup classnames
			"ammo_gaussclip",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		4,						// priority
		32.0,					// min primary distance
		1024.0,					// max primary distance
		32.0,					// min secondary distance
		1024.0,					// max secondary distance
		FALSE,					// can use underwater?
		3,						// how long does this weapon take to reload?
		70,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		1,						// minimum ammout of seconday ammo needed to fire
		TRUE,					// hold down primary fire button to use?
		TRUE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// portal gun
	{
		VALVE_WEAPON_ASHPOD,	// id
		"weapon_ashpod",		// classname
		"gauss",				// third person model
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		9,						// priority
		0.0,					// min primary distance
		500.0,					// max primary distance
		0.0,					// min secondary distance
		500.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		50,						// times out of 100 to use primary fire
		0,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// satchels
	{
		VALVE_WEAPON_SATCHEL,// id
		"weapon_satchel",	// classname
		"satchel",				// third person model
		{						// primary ammo pickup classnames
			"weapon_satchel",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		4,						// priority
		250.0,					// min primary distance
		750.0,					// max primary distance
		0.0,					// min secondary distance
		0.0,					// max secondary distance
		TRUE,					// can use underwater?
		0,						// how long does this weapon take to reload?
		50,						// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		0,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// sawedoff
	{
		VALVE_WEAPON_SAWEDOFF,	// id
		"weapon_sawedoff",		// classname
		"sawedoff",	// third person model
		{						// primary ammo pickup classnames
			"ammo_buckshot",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		8,						// priority
		0.0,					// min primary distance
		300.0,					// max primary distance
		0.0,					// min secondary distance
		300.0,					// max secondary distance
		FALSE,					// can use underwater?
		3.0,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		2,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	// dual sawedoff
	{
		VALVE_WEAPON_DUAL_SAWEDOFF,	// id
		"weapon_dual_sawedoff",		// classname
		"sawedoff",	// third person model
		{						// primary ammo pickup classnames
			"ammo_buckshot",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		5,						// skill level
		8,						// priority
		0.0,					// min primary distance
		300.0,					// max primary distance
		0.0,					// min secondary distance
		300.0,					// max secondary distance
		FALSE,					// can use underwater?
		3.0,					// how long does this weapon take to reload?
		100,					// times out of 100 to use primary fire
		1,						// minimum ammout of primary ammo needed to fire
		2,						// minimum ammout of seconday ammo needed to fire
		FALSE,					// hold down primary fire button to use?
		FALSE,					// hold down secondary fire button to use?
		FALSE,					// charge weapon using primary fire?
		FALSE,					// charge weapon using secondary fire?
		0.0,					// time to charge weapon (primary)
		0.0						// time to charge weapon (secondary)
	},
	/* terminator */
	{
		0,
		"",
		"",
		{						// primary ammo pickup classnames
			"",
			"",
		},
		{						// secondary ammo pickup classnames
			"",
			"",
		},
		0,
		MAX_WEAPONS,
		0.0,
		0.0,
		0.0,
		0.0,
		TRUE,
		0,
		0,
		1,
		1,
		FALSE,
		FALSE,
		FALSE,
		FALSE,
		0.0,
		0.0
	}
};

// weapon firing delay based on skill (min and max delay for each weapon)
// THESE MUST MATCH THE SAME ORDER AS THE WEAPON SELECT ARRAY!!!

bot_fire_delay_t valve_fire_delay[] =
{
	// crowbar
	{
		VALVE_WEAPON_CROWBAR,
		0.0,
		{
			0.0,
			0.2,
			0.3,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// egon
	{
		VALVE_WEAPON_EGON,
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// gauss
	{
		VALVE_WEAPON_GAUSS,
		0.2,
		{
			0.0,
			0.0,
			0.3,
			0.5,
			1.0
		},
		{
			0.0,
			0.1,
			0.5,
			0.8,
			1.2
		},
		1.0,
		{
			0.0,
			0.0,
			0.5,
			0.8,
			1.2
		},
		{
			0.0,
			0.7,
			1.0,
			1.5,
			2.0
		}
	},
	// rpg
	{
		VALVE_WEAPON_RPG,
		1.5,
		{
			0.0,
			0.0,
			1.0,
			2.0,
			3.0
		},
		{
			0.0,
			1.0,
			2.0,
			4.0,
			5.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// shotgun
	{
		VALVE_WEAPON_SHOTGUN,
		0.75,
		{
			0.0,
			0.0,
			0.1,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		1.5,
		{
			0.0,
			0.0,
			0.4,
			0.6,
			0.8
		},
		{
			0.0,
			0.2,
			0.5,
			0.8,
			1.2
		}
	},
	// crossbow
	{
		VALVE_WEAPON_CROSSBOW,
		0.75,
		{
			0.0,
			0.0,
			0.5,
			0.8,
			1.0
		},
		{
			0.0,
			0.4,
			0.7,
			1.0,
			1.3
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// python
	{
		VALVE_WEAPON_PYTHON,
		0.75,
		{
			0.0,
			0.0,
			0.2,
			0.4,
			0.75
		},
		{
			0.0,
			0.2,
			0.4,
			0.8,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// mp5
	{
		VALVE_WEAPON_MP5,
		0.1,
		{
			0.0,
			0.0,
			0.0,
			0.1,
			0.2
		},
		{
			0.0,
			0.05,
			0.1,
			0.3,
			0.5
		},
		1.0,
		{
			0.0,
			0.0,
			0.7,
			1.0,
			1.4
		},
		{
			0.0,
			0.7,
			1.0,
			1.6,
			2.0
		}
	},
	// 9mm
	{
		VALVE_WEAPON_GLOCK,
		0.2,
		{
			0.0,
			0.0,
			0.2,
			0.3,
			0.4
		},
		{
			0.0,
			0.1,
			0.3,
			0.4,
			0.5
		},
		0.2,
		{
			0.0,
			0.0,
			0.1,
			0.1,
			0.2
		},
		{
			0.0,
			0.1,
			0.2,
			0.2,
			0.4
		}
	},
	// hornetgun
	{
		VALVE_WEAPON_HORNETGUN,
		0.25,
		{
			0.0,
			0.0,
			0.4,
			0.6,
			1.0
		},
		{
			0.0,
			0.0,
			0.7,
			1.0,
			1.5
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// handgrenade
	{
		VALVE_WEAPON_HANDGRENADE,
		0.5,
		{
			0.0,
			0.0,
			1.0,
			2.0,
			3.0
		},
		{
			0.0,
			1.0,
			2.0,
			3.0,
			4.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// snark
	{
		VALVE_WEAPON_SNARK,
		0.1,
		{
			0.0,
			0.0,
			0.2,
			0.4,
			0.6
		},
		{
			0.0,
			0.2,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// vest
	{
		VALVE_WEAPON_VEST,
		0.0,
		{
			0.0,
			0.2,
			0.3,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// knife
	{
		VALVE_WEAPON_KNIFE,
		0.0,
		{
			0.0,
			0.2,
			0.3,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// chumtoad
	{
		VALVE_WEAPON_CHUMTOAD,
		0.1,
		{
			0.0,
			0.0,
			0.2,
			0.4,
			0.6
		},
		{
			0.0,
			0.2,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// sniper
	{
		VALVE_WEAPON_SNIPER_RIFLE,
		0.75,
		{
			0.0,
			0.0,
			0.5,
			0.8,
			1.0
		},
		{
			0.0,
			0.4,
			0.7,
			1.0,
			1.3
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// railgun
	{
		VALVE_WEAPON_RAILGUN,
		0.2,
		{
			0.0,
			0.0,
			0.3,
			0.5,
			1.0
		},
		{
			0.0,
			0.1,
			0.5,
			0.8,
			1.2
		},
		1.0,
		{
			0.0,
			0.0,
			0.5,
			0.8,
			1.2
		},
		{
			0.0,
			0.7,
			1.0,
			1.5,
			2.0
		}
	},
	// cannon
	{
		VALVE_WEAPON_CANNON,
		1.5,
		{
			0.0,
			0.0,
			1.0,
			2.0,
			3.0
		},
		{
			0.0,
			1.0,
			2.0,
			4.0,
			5.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// mag60
	{
		VALVE_WEAPON_MAG60,
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		0.2,
		{
			0.0,
			0.0,
			0.1,
			0.1,
			0.2
		},
		{
			0.0,
			0.1,
			0.2,
			0.2,
			0.4
		}
	},
	// chaingun
	{
		VALVE_WEAPON_CHAINGUN,
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// glauncher
	{
		VALVE_WEAPON_GLAUNCHER,
		0.1,
		{
			0.0,
			0.0,
			0.0,
			0.1,
			0.2
		},
		{
			0.0,
			0.05,
			0.1,
			0.3,
			0.5
		},
		1.0,
		{
			0.0,
			0.0,
			0.7,
			1.0,
			1.4
		},
		{
			0.0,
			0.7,
			1.0,
			1.6,
			2.0
		}
	},
	// smg
	{
		VALVE_WEAPON_SMG,
		0.1,
		{
			0.0,
			0.0,
			0.0,
			0.1,
			0.2
		},
		{
			0.0,
			0.05,
			0.1,
			0.3,
			0.5
		},
		1.0,
		{
			0.0,
			0.0,
			0.7,
			1.0,
			1.4
		},
		{
			0.0,
			0.7,
			1.0,
			1.6,
			2.0
		}
	},
	// usas
	{
		VALVE_WEAPON_USAS,
		0.75,
		{
			0.0,
			0.0,
			0.1,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		1.5,
		{
			0.0,
			0.0,
			0.4,
			0.6,
			0.8
		},
		{
			0.0,
			0.2,
			0.5,
			0.8,
			1.2
		}
	},
	// fists
	{
		VALVE_WEAPON_FISTS,
		0.0,
		{
			0.0,
			0.2,
			0.3,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// wrench
	{
		VALVE_WEAPON_WRENCH,
		0.0,
		{
			0.0,
			0.2,
			0.3,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// snowball
	{
		VALVE_WEAPON_SNOWBALL,
		0.5,
		{
			0.0,
			0.0,
			1.0,
			2.0,
			3.0
		},
		{
			0.0,
			1.0,
			2.0,
			3.0,
			4.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// chainsaw
	{
		VALVE_WEAPON_CHAINSAW,
		0.0,
		{
			0.0,
			0.2,
			0.3,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// 12 gauge
	{
		VALVE_WEAPON_12GAUGE,
		0.75,
		{
			0.0,
			0.0,
			0.1,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		1.5,
		{
			0.0,
			0.0,
			0.4,
			0.6,
			0.8
		},
		{
			0.0,
			0.2,
			0.5,
			0.8,
			1.2
		}
	},
	// nuke
	{
		VALVE_WEAPON_NUKE,
		1.5,
		{
			0.0,
			0.0,
			1.0,
			2.0,
			3.0
		},
		{
			0.0,
			1.0,
			2.0,
			4.0,
			5.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// deagle
	{
		VALVE_WEAPON_DEAGLE,
		0.75,
		{
			0.0,
			0.0,
			0.2,
			0.4,
			0.75
		},
		{
			0.0,
			0.2,
			0.4,
			0.8,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// dual deagle
	{
		VALVE_WEAPON_DUAL_DEAGLE,
		0.75,
		{
			0.0,
			0.0,
			0.2,
			0.4,
			0.75
		},
		{
			0.0,
			0.2,
			0.4,
			0.8,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// dual rpg
	{
		VALVE_WEAPON_DUAL_RPG,
		1.5,
		{
			0.0,
			0.0,
			1.0,
			2.0,
			3.0
		},
		{
			0.0,
			1.0,
			2.0,
			4.0,
			5.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// dual smg
	{
		VALVE_WEAPON_DUAL_SMG,
		0.1,
		{
			0.0,
			0.0,
			0.0,
			0.1,
			0.2
		},
		{
			0.0,
			0.05,
			0.1,
			0.3,
			0.5
		},
		1.0,
		{
			0.0,
			0.0,
			0.7,
			1.0,
			1.4
		},
		{
			0.0,
			0.7,
			1.0,
			1.6,
			2.0
		}
	},
	// dual wrench
	{
		VALVE_WEAPON_DUAL_WRENCH,
		0.0,
		{
			0.0,
			0.2,
			0.3,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// dual usas
	{
		VALVE_WEAPON_DUAL_USAS,
		0.75,
		{
			0.0,
			0.0,
			0.1,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		1.5,
		{
			0.0,
			0.0,
			0.4,
			0.6,
			0.8
		},
		{
			0.0,
			0.2,
			0.5,
			0.8,
			1.2
		}
	},
	// freezegun
	{
		VALVE_WEAPON_FREEZEGUN,
		0.2,
		{
			0.0,
			0.0,
			0.3,
			0.5,
			1.0
		},
		{
			0.0,
			0.1,
			0.5,
			0.8,
			1.2
		},
		1.0,
		{
			0.0,
			0.0,
			0.5,
			0.8,
			1.2
		},
		{
			0.0,
			0.7,
			1.0,
			1.5,
			2.0
		}
	},
	// dual mag60
	{
		VALVE_WEAPON_DUAL_MAG60,
		0.1,
		{
			0.0,
			0.0,
			0.0,
			0.1,
			0.2
		},
		{
			0.0,
			0.05,
			0.1,
			0.3,
			0.5
		},
		1.0,
		{
			0.0,
			0.0,
			0.7,
			1.0,
			1.4
		},
		{
			0.0,
			0.7,
			1.0,
			1.6,
			2.0
		}
	},
	// rocket crowbar
	{
		VALVE_WEAPON_ROCKETCROWBAR,
		0.0,
		{
			0.0,
			0.2,
			0.3,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		0.0,
		{
			0.0,
			0.2,
			0.3,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		}
	},
	// dual railgun
	{
		VALVE_WEAPON_DUAL_RAILGUN,
		0.2,
		{
			0.0,
			0.0,
			0.3,
			0.5,
			1.0
		},
		{
			0.0,
			0.1,
			0.5,
			0.8,
			1.2
		},
		1.0,
		{
			0.0,
			0.0,
			0.5,
			0.8,
			1.2
		},
		{
			0.0,
			0.7,
			1.0,
			1.5,
			2.0
		}
	},
	// gravitygun
	{
		VALVE_WEAPON_GRAVITYGUN,
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// flamethrower
	{
		VALVE_WEAPON_FLAMETHROWER,
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// dual flamethrower
	{
		VALVE_WEAPON_DUAL_FLAMETHROWER,
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// ashpod
	{
		VALVE_WEAPON_ASHPOD,
		0.2,
		{
			0.0,
			0.0,
			0.3,
			0.5,
			1.0
		},
		{
			0.0,
			0.1,
			0.5,
			0.8,
			1.2
		},
		1.0,
		{
			0.0,
			0.0,
			0.5,
			0.8,
			1.2
		},
		{
			0.0,
			0.7,
			1.0,
			1.5,
			2.0
		}
	},
	// satchels
	{
		VALVE_WEAPON_SATCHEL,
		0.5,
		{
			0.0,
			0.0,
			1.0,
			2.0,
			3.0
		},
		{
			0.0,
			1.0,
			2.0,
			3.0,
			4.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	},
	// sawedoff
	{
		VALVE_WEAPON_SAWEDOFF,
		0.75,
		{
			0.0,
			0.0,
			0.1,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		1.5,
		{
			0.0,
			0.0,
			0.4,
			0.6,
			0.8
		},
		{
			0.0,
			0.2,
			0.5,
			0.8,
			1.2
		}
	},
	// dual sawedoff
	{
		VALVE_WEAPON_DUAL_SAWEDOFF,
		0.75,
		{
			0.0,
			0.0,
			0.1,
			0.4,
			0.6
		},
		{
			0.0,
			0.3,
			0.5,
			0.7,
			1.0
		},
		1.5,
		{
			0.0,
			0.0,
			0.4,
			0.6,
			0.8
		},
		{
			0.0,
			0.2,
			0.5,
			0.8,
			1.2
		}
	},
	// terminator 
	{
		0,
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		0.0,
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		},
		{
			0.0,
			0.0,
			0.0,
			0.0,
			0.0
		}
	}
};