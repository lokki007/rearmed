"""ds-surprise: move the dev 'look' into Sur_DevLook and add g_dsSurShots (game-driven test screenshots)."""
import pathlib
p = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code' / 'game' / 'g_ds_surprise.c'
s = open(p, newline='').read()
lines = s.split('\n')
start = next(i for i, l in enumerate(lines) if '// look: a deathmatch spot 650-1300' in l)
assert lines[start + 1].strip() == '{'
# the block ends at the line with the log + its closing brace
logi = next(i for i in range(start, start + 90) if 'DSSurprise: look at %i %i %i from' in lines[i])
assert lines[logi + 1].strip() == '}'
body = lines[start + 2:logi + 1]
# dedent 4 tabs -> 1
body = [l[3:] if l.startswith('\t\t\t\t') else l for l in body]
func = ['/*', '=================', 'Sur_DevLook', '', 'Dev / tests: the player goes to a spot 650-1300 from target with a clear line to it (never',
        'closer, so a landing there is not re-picked) and faces it', '=================', '*/',
        'static void Sur_DevLook( gentity_t *h, const vec3_t target ) {',
        '\tgentity_t\t*spot = NULL, *best = NULL;', '\ttrace_t\t\ttr;', '\tvec3_t\t\torg, tg, d, ang;', '\tfloat\t\tdd, bestScore = 1e9f;', '']
func += body[4:] if body[0].strip().startswith('gentity_t') else body
func += ['}', '']
new_lines = lines[:start] + ['\t\t\tSur_DevLook( h, target );'] + lines[logi + 2:]
s2 = '\n'.join(new_lines)
anchor = '/*\n=================\nSur_PlanSim'
assert anchor in s2
s2 = s2.replace(anchor, '\n'.join(func) + '\n' + anchor, 1)
open(p, 'w', newline='').write(s2)
print('ok', start, logi, len(body))
