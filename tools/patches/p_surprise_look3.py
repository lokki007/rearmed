"""ds-surprise: the dev look ignores the target's own body (one-shot)."""
import pathlib
p = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code' / 'game' / 'g_ds_surprise.c'
s = p.read_bytes().decode()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')


def rep(a, b, c=1):
    global s
    assert s.count(a) == c, (a, s.count(a))
    s = s.replace(a, b)


rep("static void Sur_DevLook( gentity_t *h, const vec3_t target, qboolean near );", "static void Sur_DevLook( gentity_t *h, const vec3_t target, int live );")
rep("static void Sur_DevLook( gentity_t *h, const vec3_t target, qboolean near ) {", "static void Sur_DevLook( gentity_t *h, const vec3_t target, int live ) {")
rep("static qboolean Sur_LookTarget( int what, vec3_t out, qboolean *live ) {", "static qboolean Sur_LookTarget( int what, vec3_t out, int *live ) {")
rep("\t\t*live = qtrue;\n", "\t\t*live = t->s.number;\n")
rep("\tqboolean\tlive = qfalse;\n", "\tint\t\t\tlive = -1;\n")
rep("\t\tif ( sur.shotLook[i] && Sur_LookTarget( sur.shotLook[i], tg, &live ) ) {",
    "\t\tlive = -1;\n\t\tif ( sur.shotLook[i] && Sur_LookTarget( sur.shotLook[i], tg, &live ) ) {")
rep("\t\t\tSur_DevLook( h, target, t != NULL );", "\t\t\tSur_DevLook( h, target, t ? t->s.number : -1 );")
rep("\t\t\ttrap_Trace( &tr, tg, mins, maxs, p2, ENTITYNUM_NONE, MASK_PLAYERSOLID );",
    "\t\t\ttrap_Trace( &tr, tg, mins, maxs, p2, live >= 0 ? live : ENTITYNUM_NONE, MASK_PLAYERSOLID );")
rep("\t\t\tif ( dd < ( k < 48 ? 650 : 350 ) || ( k >= 48 && !near ) ) {",
    "\t\t\tif ( dd < ( k < 48 ? 650 : 350 ) || ( k >= 48 && live < 0 ) ) {")
p.write_bytes(s.replace('\n', nl).encode())
print('ok')
