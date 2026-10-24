/*
===========================================================================
REARMED -- round 4 ENEMIES: the cgame mirror of CS_DSENEMY (contract, bg_ds_enemy.h).
CG_EN_Frame parses it when it changed; workers read CG_EN( clientNum ).
===========================================================================
*/
#include "cg_local.h"

static cgEnemy_t	cg_enemy[MAX_CLIENTS];
static cgEnemy_t	cg_enemyNone;
static char			cen_cs[MAX_INFO_STRING];
static int			cen_changed;	// cg.time of the last change

// CG_DrawActiveFrame, early: re-parse CS_DSENEMY when it changed (a copy compare)
void CG_EN_Frame( void ) {
	const char	*s = CG_ConfigString( CS_DSENEMY );
	const char	*v;
	int			i, f;
	int			n[5];

	if ( !strcmp( s, cen_cs ) ) {
		return;
	}
	Q_strncpyz( cen_cs, s, sizeof( cen_cs ) );
	cen_changed = cg.time;
	memset( cg_enemy, 0, sizeof( cg_enemy ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		v = Info_ValueForKey( s, va( "%i", i ) );
		if ( !v || !v[0] ) {
			continue;
		}
		memset( n, 0, sizeof( n ) );
		for ( f = 0; f < 5 && v; f++ ) {
			n[f] = atoi( v );
			v = strchr( v, ',' );
			if ( v ) {
				v++;
			}
		}
		cg_enemy[i].arch = n[0];
		cg_enemy[i].affix = n[1];
		cg_enemy[i].rank = n[2];
		cg_enemy[i].flags = n[3];
		cg_enemy[i].armorPct = n[4];
		if ( cg_enemy[i].arch < 0 || cg_enemy[i].arch >= DSAR_NUM ) {
			cg_enemy[i].arch = 0;
		}
	}
}

// never NULL: out-of-range clients read as a plain bot
const cgEnemy_t *CG_EN( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return &cg_enemyNone;
	}
	return &cg_enemy[clientNum];
}

int CG_EN_ChangedTime( void ) {
	return cen_changed;
}
