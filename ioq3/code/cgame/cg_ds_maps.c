/*
===========================================================================
REARMED -- DESCENT map variety, cgame side (ds-maps): the floor's lighting variant.

CS_DESCENT "mv" = the floor variant (bg_ds_maps.h). Its light part tints the world under the
HUD, on top of the act's grade and haze (cg_ds_acts.c): a colour multiply, an edge haze and
an even additive haze (fog / flooded light). Cheap: three full-screen quads.
cg_dsVariantLight 0 turns it off (1 default, 2 stronger). A dark variant steps aside for the
BLACKOUT floor modifier (ds-surprise), which darkens the world itself.
===========================================================================
*/
#include "cg_local.h"

static vmCvar_t		cg_dsVariantLight;
static qboolean		dsmRegistered;
static qhandle_t	dsmFilter, dsmVignette, dsmAdd;

static void CG_DSM_Quad( qhandle_t shader, float r, float g, float b, float a ) {
	vec4_t	c;

	c[0] = r;
	c[1] = g;
	c[2] = b;
	c[3] = a;
	trap_R_SetColor( c );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 1, 1, shader );
	trap_R_SetColor( NULL );
}

// the floor variant (0: as built, or not DESCENT)
int CG_DSM_Variant( void ) {
	const char	*info = CG_ConfigString( CS_DESCENT );

	if ( !info[0] ) {
		return 0;
	}
	return atoi( Info_ValueForKey( info, "mv" ) );
}

/*
=================
CG_DSM_Draw

cg_draw.c, right after CG_DS_DrawBiome (under the HUD)
=================
*/
void CG_DSM_Draw( void ) {
	const dsLightVariant_t	*lv;
	float					k, m;
	int						light;

	if ( !dsmRegistered ) {
		trap_Cvar_Register( &cg_dsVariantLight, "cg_dsVariantLight", "1", CVAR_ARCHIVE );
		dsmFilter = trap_R_RegisterShaderNoMip( "rearmed/evFilter" );
		dsmVignette = trap_R_RegisterShaderNoMip( "rearmed/evVignette" );
		dsmAdd = trap_R_RegisterShaderNoMip( "rearmed/evFlash" );
		dsmRegistered = qtrue;
	}
	trap_Cvar_Update( &cg_dsVariantLight );
	if ( !cg.snap || cg_dsVariantLight.integer <= 0 ) {
		return;
	}
	light = DSV_LIGHT( CG_DSM_Variant() );
	if ( light <= 0 || light >= DSV_LIGHT_NUM ) {
		return;
	}
	lv = &ds_lightVariants[light];
	if ( lv->dark && CG_DS_SurpriseMod() == DSMOD_BLACKOUT ) {
		return;
	}
	k = cg_dsVariantLight.integer >= 2 ? 1.4f : 1.0f;
	// the multiply only takes colour away; "stronger" pushes it further from white
	m = k;
	CG_DSM_Quad( dsmFilter, 1.0f - ( 1.0f - lv->mul[0] ) * m, 1.0f - ( 1.0f - lv->mul[1] ) * m, 1.0f - ( 1.0f - lv->mul[2] ) * m, 1 );
	if ( lv->haze[0] + lv->haze[1] + lv->haze[2] > 0 ) {
		CG_DSM_Quad( dsmAdd, lv->haze[0] * k, lv->haze[1] * k, lv->haze[2] * k, 1 );
	}
	if ( lv->edge[3] > 0 ) {
		CG_DSM_Quad( dsmVignette, lv->edge[0], lv->edge[1], lv->edge[2], lv->edge[3] * k > 1 ? 1 : lv->edge[3] * k );
	}
}
