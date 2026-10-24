"""ds-surprise: wait for ds-arrivals' staggered arrivals; share the announcer queue (one-shot)."""
import pathlib
root = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code'


def patch(path, pairs):
    s = open(path, newline='').read()
    for old, new in pairs:
        assert s.count(old) == 1, (path, old)
        s = s.replace(old, new)
    open(path, 'w', newline='').write(s)
    print('ok', path.name)


patch(root / 'game' / 'g_ds_surprise.c', [
    ('''static int Sur_LeaderFrags( void ) {''', '''// ds-arrivals: the floor's own bots arrive one at a time early on; nothing of ours lands on top of that
static qboolean Sur_ArrivalsDone( void ) {
	return G_DS_ArrivalsLeft() == 0 && level.time >= G_DS_ArrivalLastAt() + 3000;
}

static int Sur_LeaderFrags( void ) {'''),
    ('''	if ( sur.ambPlan && sur.ambState == 0 && el >= sur.ambEarliest ) {''',
     '''	if ( sur.ambPlan && sur.ambState == 0 && el >= sur.ambEarliest && Sur_ArrivalsDone() ) {'''),
    ('''	if ( sur.evPlan && el >= sur.evAt ) {''', '''	if ( sur.evPlan && el >= sur.evAt && Sur_ArrivalsDone() ) {'''),
    ('''	if ( sur.ghPlan && sur.ghState == 0 && el >= sur.ghAt ) {''',
     '''	if ( sur.ghPlan && sur.ghState == 0 && el >= sur.ghAt && Sur_ArrivalsDone() ) {'''),
])

patch(root / 'cgame' / 'cg_ds_surprise.c', [
    ('''	int			callKind, callA, callB, callTime, callDur;
''', '''	int			callKind, callA, callB, callTime, callDur;
	sfxHandle_t	voice;					// a voice line waiting for the announcer (ds-arrivals' queue)
	int			voiceUntil;				// dropped when it can't speak by then
'''),
    ('''	if ( voice && cg_rearmAnnouncer.integer ) {
		trap_S_StartLocalSound( voice, CHAN_ANNOUNCER );
	}''', '''	if ( voice && cg_rearmAnnouncer.integer ) {
		surc.voice = voice;
		surc.voiceUntil = cg.time + 2500;
	}'''),
    ('''	Surc_Parse();
	if ( !surc.on || cg.showScores || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	Surc_DrawCallout();''', '''	Surc_Parse();
	// our voice line: when the shared announcer is free (ds-arrivals reads the arriving bots' names)
	if ( surc.voice ) {
		if ( cg.time > surc.voiceUntil ) {
			surc.voice = 0;
		} else if ( CG_DSI_AnnouncerFreeAt() <= cg.time ) {
			trap_S_StartLocalSound( surc.voice, CHAN_ANNOUNCER );
			CG_DSI_AnnouncerHold( cg.time + 1400 );
			surc.voice = 0;
		}
	}
	if ( !surc.on || cg.showScores || cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}
	Surc_DrawCallout();'''),
])
