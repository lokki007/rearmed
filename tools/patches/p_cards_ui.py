import pathlib, sys

def patch(fn, rep):
    p = pathlib.Path(fn)
    s = p.read_bytes().decode('latin-1')
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in rep:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        c = s.count(a)
        if c != 1:
            print(fn, 'count', c, 'for:', a[:80]); sys.exit(1)
        s = s.replace(a, b)
    p.write_bytes(s.encode('latin-1'))
    print(fn, 'ok')

UI = r'G:\_Games\q3mod\ioq3\code\q3_ui'
patch(UI + r'\ui_roguelike.c', [
('static const char *rl_kindNames[7] = { "ABILITY", "ALT-FIRE", "STAT", "PERK", "PASSIVE", "WEAPON", "SECRET" };',
 'static const char *rl_kindNames[8] = { "ABILITY", "ALT-FIRE", "STAT", "PERK", "PASSIVE", "WEAPON", "SECRET", "LEGEND" };'),
('static vec4_t rl_kindColors[7] = {', 'static vec4_t rl_kindColors[8] = {'),
('''	{ 1.0f, 0.84f, 0.25f, 1.0f }	// secret
};
static vec4_t rl_rarityColors[3] = {
	{ 0.55f, 0.5f, 0.45f, 1.0f },
	{ 0.3f, 0.6f, 1.0f, 1.0f },
	{ 1.0f, 0.75f, 0.2f, 1.0f }
};''', '''	{ 1.0f, 0.84f, 0.25f, 1.0f },	// secret
	{ 1.0f, 0.62f, 0.1f, 1.0f }		// legendary (round 3, ds-cards)
};
static vec4_t rl_rarityColors[4] = {
	{ 0.55f, 0.5f, 0.45f, 1.0f },
	{ 0.3f, 0.6f, 1.0f, 1.0f },
	{ 1.0f, 0.75f, 0.2f, 1.0f },
	{ 1.0f, 0.62f, 0.1f, 1.0f }
};'''),
('''	kind = bg_rlCardKind[card];
	if ( kind < 0 || kind > 6 ) {
		kind = 0;
	}
	rarity = bg_rlCardRarity[card];
	if ( rarity < 0 || rarity > 2 ) {
		rarity = 0;
	}''', '''	kind = bg_rlCardKind[card];
	if ( kind < 0 || kind > 7 ) {
		kind = 0;
	}
	rarity = bg_rlCardRarity[card];
	if ( rarity < 0 || rarity > 3 ) {
		rarity = 0;
	}'''),
('''	RL_Text( x + w / 2, y + h - 16, rarity == 2 ? "EPIC" : rarity == 1 ? "RARE" : "COMMON", 6, 9, rl_rarityColors[rarity], 1 );''',
 '''	RL_Text( x + w / 2, y + h - 16, rarity == 3 ? "LEGENDARY" : rarity == 2 ? "EPIC" : rarity == 1 ? "RARE" : "COMMON", 6, 9, rl_rarityColors[rarity], 1 );'''),
# the build list: legendaries first, so they never fall under "+N MORE"
('''	for ( kind = 0; kind < 7; kind++ ) {
		for ( i = 0; i < RLC_NUM; i++ ) {
			if ( !counts[i] || bg_rlCardKind[i] != kind ) {''',
 '''	for ( col = 0, kind = RLK_LEGEND; col < 8; col++, kind = col - 1 ) {
		for ( i = 0; i < RLC_NUM; i++ ) {
			if ( !counts[i] || bg_rlCardKind[i] != kind ) {'''),
])

patch(UI + r'\ui_ds_meta.c', [
('''#define DSC_SECRETS		7
#define DSC_NUM			8''', '''#define DSC_SECRETS		7
#define DSC_LEGENDS		8		// round 3 (ds-cards): legendary cards
#define DSC_NUM			9'''),
('''static const char *dsc_tabNames[DSC_NUM] = { "WEAPONS", "EVOLUTIONS", "PASSIVES", "PERKS", "ABILITIES", "CHARACTERS", "BOSSES", "SECRETS" };''',
 '''static const char *dsc_tabNames[DSC_NUM] = { "WEAPONS", "EVOLUTIONS", "PASSIVES", "PERKS", "ABILITIES", "CHARACTERS", "BOSSES", "SECRETS", "LEGENDS" };'''),
('''	case DSC_SECRETS:
		for ( i = 0; i < RLS_NUM; i++ ) {
			out[n++] = i;
		}
		break;
	}
	return n;''', '''	case DSC_SECRETS:
		for ( i = 0; i < RLS_NUM; i++ ) {
			out[n++] = i;
		}
		break;
	case DSC_LEGENDS:
		for ( i = 0; i < LG_NUM; i++ ) {
			out[n++] = RLC_LEGEND_FIRST + i;
		}
		break;
	}
	return n;'''),
('''	case DSC_PASSIVES:
	case DSC_PERKS:
	case DSC_ABILITIES:
		return DSM_BitGet( RL_KVGet( &rl_meta, "seen_cards" ), item );''', '''	case DSC_PASSIVES:
	case DSC_PERKS:
	case DSC_ABILITIES:
	case DSC_LEGENDS:
		return DSM_BitGet( RL_KVGet( &rl_meta, "seen_cards" ), item );'''),
('''	case DSC_SECRETS:	return found ? bg_rlCardNames[RLC_SECRET_FIRST + item] : "???";''',
 '''	case DSC_SECRETS:	return found ? bg_rlCardNames[RLC_SECRET_FIRST + item] : "???";
	case DSC_LEGENDS:	return found ? bg_rlCardNames[item] : "???";'''),
('''	case DSC_SECRETS:
		DSM_Tile( x, y, sz, found ? DSM_Initials( bg_rlCardNames[RLC_SECRET_FIRST + item] ) : "?", dsm_colGold, found );
		break;
	}''', '''	case DSC_SECRETS:
		DSM_Tile( x, y, sz, found ? DSM_Initials( bg_rlCardNames[RLC_SECRET_FIRST + item] ) : "?", dsm_colGold, found );
		break;
	case DSC_LEGENDS:
		if ( found ) {
			RL_Scale( c, dsm_colLegend, 0.55f, 1 );
			RL_Pic( x - sz * 0.15f, y - sz * 0.15f, sz * 1.3f, sz * 1.3f, rl_shGlow, c );
		}
		DSM_Tile( x, y, sz, found ? bg_rlCardShort[item] : "?", dsm_colLegend, found );
		break;
	}'''),
('''	case DSC_SECRETS:
		if ( found ) {
			ty = RL_TextWrap( x + 8, ty, w - 16, bg_rlCardDescs[RLC_SECRET_FIRST + item], 6, 10, rl_colBone, 6 );''',
 '''	case DSC_LEGENDS:
		if ( found ) {
			ty = RL_TextWrap( x + 8, ty, w - 16, bg_rlCardDescs[item], 6, 10, rl_colBone, 6 );
			ty += 4;
			RL_TextWrap( x + 8, ty, w - 16, "Taken in a descent.", 5, 8, rl_colGood, 2 );
		}
		else {
			ty = RL_TextWrap( x + 8, ty, w - 16, "A legendary card. Rare in any offer, likelier deeper down, from elites and bosses, and with luck.", 6, 10, dsm_colLegend, 6 );
		}
		break;
	case DSC_SECRETS:
		if ( found ) {
			ty = RL_TextWrap( x + 8, ty, w - 16, bg_rlCardDescs[RLC_SECRET_FIRST + item], 6, 10, rl_colBone, 6 );'''),
('''static vec4_t dsm_colGold		= { 1.0f, 0.78f, 0.25f, 1.0f };''',
 '''static vec4_t dsm_colGold		= { 1.0f, 0.78f, 0.25f, 1.0f };
static vec4_t dsm_colLegend		= { 1.0f, 0.6f, 0.1f, 1.0f };		// round 3: legendary cards'''),
])
