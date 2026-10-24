import pathlib, sys
p = pathlib.Path(r'G:\_Games\q3mod\ioq3\code\cgame\cg_roguelike.c')
s = p.read_bytes().decode('latin-1')
nl = '\r\n' if '\r\n' in s else '\n'
S = lambda x: x.replace('\n', nl)
rep = [
# kind / rarity tables: + legendary
("""static const vec4_t rlKindColors[7] = {""", """static const vec4_t rlKindColors[8] = {"""),
("""	{ 1.00f, 0.84f, 0.25f, 1 }		// secret (v2)
};
static const char *rlKindNames[7] = { "ABILITY", "ALT-FIRE", "STAT", "PERK", "PASSIVE", "WEAPON", "SECRET" };
static const char *rlRarityNames[3] = { "COMMON", "RARE", "EPIC" };""",
 """	{ 1.00f, 0.84f, 0.25f, 1 },		// secret (v2)
	{ 1.00f, 0.62f, 0.10f, 1 }		// legendary (round 3, ds-cards)
};
static const char *rlKindNames[8] = { "ABILITY", "ALT-FIRE", "STAT", "PERK", "PASSIVE", "WEAPON", "SECRET", "LEGENDARY" };
static const char *rlRarityNames[4] = { "COMMON", "RARE", "EPIC", "LEGENDARY" };"""),
("""	if ( c < 0 || c >= RLC_NUM || bg_rlCardKind[c] < 0 || bg_rlCardKind[c] > 6 ) {""",
 """	if ( c < 0 || c >= RLC_NUM || bg_rlCardKind[c] < 0 || bg_rlCardKind[c] > 7 ) {"""),
("""		Vector4Copy( rlKindColors[kind < 0 || kind > 6 ? 0 : kind], col );""",
 """		Vector4Copy( rlKindColors[kind < 0 || kind > 7 ? 0 : kind], col );"""),
("""k == RLK_SECRET ? "SECRET CLAIMED" : "PERK GAINED";""",
 """k == RLK_SECRET ? "SECRET CLAIMED" : k == RLK_LEGEND ? "LEGENDARY CLAIMED" : "PERK GAINED";"""),
# register / server commands
("""	eliteShell = trap_R_RegisterShader( "rearmed/rlEliteShell" );""",
 """	eliteShell = trap_R_RegisterShader( "rearmed/rlEliteShell" );
	CG_DSC_Register();		// CARDS: the reveal, legendaries, the tally (cg_ds_cards.c)"""),
("""qboolean CG_RL_ServerCommand( const char *cmd ) {
""", """qboolean CG_RL_ServerCommand( const char *cmd ) {
	if ( CG_DSC_ServerCommand( cmd ) ) {		// CARDS: dsctally / dsclg
		return qtrue;
	}
"""),
# picks wait for the reveal; a press during it flips everything
("""	if ( cg.time - offerStart < RLCARD_IN ) {
		return;		// let them land first
	}
	trap_SendClientCommand( va( "rlpick %i", slot + 1 ) );""",
 """	if ( CG_DSC_RevealOn() ? CG_DSC_RevealLeft( offerStart, shownNum, shownOffer ) > 0 : cg.time - offerStart < RLCARD_IN ) {
		CG_DSC_RevealSkip( offerStart );		// CARDS: a pick key during the reveal flips them all
		return;		// let them land first
	}
	CG_DSC_OnPick( rlc.offer[slot], 320 + ( slot - ( rlc.numOffer - 1 ) * 0.5f ) * ( RLCARD_W + RLCARD_GAP ), RLCARD_Y + RLCARD_H * 0.5f - 24 );	// CARDS: the synergy ding
	trap_SendClientCommand( va( "rlpick %i", slot + 1 ) );"""),
("""	if ( rlc.phase != RLP_OFFER || pickSlot >= 0 || cg.time - offerStart < RLCARD_IN + 250 ) {
		return;
	}""",
 """	if ( rlc.phase == RLP_OFFER && pickSlot < 0 && CG_DSC_RevealOn() && CG_DSC_RevealLeft( offerStart, shownNum, shownOffer ) > -250 ) {
		if ( ( ( pressed & BUTTON_ATTACK ) || up ) && CG_DSC_RevealLeft( offerStart, shownNum, shownOffer ) > 0 ) {
			CG_DSC_RevealSkip( offerStart );		// CARDS: fire / jump during the reveal flips them all
		}
		return;
	}
	if ( rlc.phase != RLP_OFFER || pickSlot >= 0 || ( !CG_DSC_RevealOn() && cg.time - offerStart < RLCARD_IN + 250 ) ) {
		return;
	}"""),
# the agent's shell
("""	if ( CG_DS_BodyShell( ent, state ) ) {""",
 """	CG_DSC_BodyShell( ent, state );		// CARDS: the DOUBLE AGENT's green shell
	if ( CG_DS_BodyShell( ent, state ) ) {"""),
# in-play 2D
("""	CG_DS_FrenzyDraw2D();	// FRENZY: chain counter, cash-out, edge glow, spike wash + ring
""", """	CG_DS_FrenzyDraw2D();	// FRENZY: chain counter, cash-out, edge glow, spike wash + ring
	CG_DSC_Draw2D();		// CARDS: legendary row, evolution ready, dstally preview
"""),
# the card: rarity 3, the back, the face FX
("""	r = bg_rlCardRarity[card];
	if ( r < 0 || r > 2 ) {
		r = 0;
	}
	sx = flip < 0.5f ? cos( flip * M_PI ) : -cos( flip * M_PI );""",
 """	r = bg_rlCardRarity[card];
	if ( r < 0 || r > 3 ) {
		r = 0;
	}
	sx = flip < 0.5f ? cos( flip * M_PI ) : -cos( flip * M_PI );"""),
("""	if ( flip < 0.5f ) {
		// the back: dark with a kind-coloured frame and a big D""",
 """	if ( flip < 0.5f ) {
		if ( CG_DSC_CardBack( card, x, y, w, h, sx, alpha ) ) {
			return;		// CARDS: the back tells the rarity
		}
		// the back: dark with a kind-coloured frame and a big D"""),
("""	chh = n > 12 ? 7.5f : 9.5f;""", """	chh = n > 17 ? 6.0f : n > 12 ? 7.5f : 9.5f;"""),
("""	if ( flash > 0 ) {
		fg[0] = fg[1] = fg[2] = 1; fg[3] = flash * alpha;
		CG_FillRect( x, y, w, h, fg );
	}
}""", """	CG_DSC_CardFace( slot, card, x, y, w, h, alpha );		// CARDS: rarity frame, shimmer, legendary sweep
	if ( flash > 0 ) {
		fg[0] = fg[1] = fg[2] = 1; fg[3] = flash * alpha;
		CG_FillRect( x, y, w, h, fg );
	}
}"""),
# the offer: the reveal animation
("""		if ( pickSlot < 0 && rlc.phase == RLP_OFFER ) {
			// slide in from below, flip open""",
 """		if ( pickSlot < 0 && rlc.phase == RLP_OFFER && CG_DSC_RevealOn() ) {
			// CARDS: dealt face-down, flipped one by one (cg_ds_cards.c)
			if ( !CG_DSC_CardAnim( i, offerStart, n, shownOffer, &e, &alpha, &flip, &scale, &flash ) ) {
				continue;
			}
			cy += e;
			scale *= cs;
			if ( i == sel && CG_DSC_RevealLeft( offerStart, n, shownOffer ) <= 0 ) {
				cy -= 8;
				scale *= 1.04f;
			}
			CG_RL_DrawCard( i, shownOffer[i], cx, cy, scale, flip, alpha, i == sel && CG_DSC_RevealLeft( offerStart, n, shownOffer ) <= 0, flash );
		} else if ( pickSlot < 0 && rlc.phase == RLP_OFFER && !CG_DSC_RevealOn() ) {
			CG_RL_DrawCard( i, shownOffer[i], cx, cy - ( i == sel ? 8 : 0 ), cs * ( i == sel ? 1.04f : 1.0f ), 1.0f, 1.0f, i == sel, 0 );	// cg_dsReveal 0: just there
		} else if ( pickSlot < 0 && rlc.phase == RLP_OFFER ) {
			// slide in from below, flip open"""),
("""	if ( rlc.phase == RLP_OFFER && pickSlot < 0 ) {
		t = (float)( cg.time - offerStart ) / 300.0f;""",
 """	if ( rlc.phase == RLP_OFFER && pickSlot < 0 ) {
		CG_DSC_DrawOfferFX( offerStart, n, shownOffer );		// CARDS: the legendary's flare
	}
	if ( rlc.phase == RLP_OFFER && pickSlot < 0 ) {
		t = (float)( cg.time - offerStart ) / 300.0f;"""),
# the intermission: the tally first, the ding over the cards
("""		if ( CG_DS_DrawJackpot( rlc.phase ) ) {		// LOOT: chests before the cards""",
 """		if ( rlc.phase == RLP_END && CG_DSC_DrawTally( rlc.floorPlayed ) ) {	// CARDS: the floor-end tally first
			return qtrue;
		}
		if ( CG_DS_DrawJackpot( rlc.phase ) ) {		// LOOT: chests before the cards"""),
("""		CG_RL_DrawOffer();
""", """		CG_RL_DrawOffer();
		CG_DSC_DrawDing();		// CARDS: the synergy ding
"""),
]
for a, b in rep:
    a = S(a); b = S(b)
    c = s.count(a)
    if c != 1:
        print('count', c, 'for:', a[:80]); sys.exit(1)
    s = s.replace(a, b)
p.write_bytes(s.encode('latin-1'))
print('ok')
