"""ds-art: write a prop line-up screenshot cfg for tools/art_shot.sh.
python tools/art_cfg.py <name> <cam_x> <cam_y> <cam_z> <props...>   prop = type:x:y
Shots: wide (fov 60), a close-up of every prop (fov 22), then shoot the crate and the first barrel
(debris in flight / burning / exploded)."""
import math
import os
import sys

name, cx, cy, cz = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
props = [(p.split(':')[0], float(p.split(':')[1]), float(p.split(':')[2])) for p in sys.argv[5:]]
mx = sum(p[1] for p in props) / len(props)
my = sum(p[2] for p in props) / len(props)


def yaw(x, y):
    return math.degrees(math.atan2(y - cy, x - cx))


L = ['wait 300', 'god', 'cg_drawGun 0', 'cg_draw2D 0', 'cg_drawFPS 0', 'set g_propTest "list"', 'wait 100']
for t, x, y in props:
    L += [f'set g_propTest "{t} {x:.0f} {y:.0f} {cz + 4:.0f}"', 'wait 100']
L += ['wait 150', 'noclip', 'cg_fov 60', f'setviewpos {cx:.0f} {cy:.0f} {cz:.0f} {yaw(mx, my):.1f}', 'wait 60', 'screenshotJPEG',
      'cg_fov 22']
for t, x, y in props:
    L += [f'setviewpos {cx:.0f} {cy:.0f} {cz - 16:.0f} {yaw(x, y):.1f}', 'wait 40', 'screenshotJPEG']
if '--shoot' in os.environ.get('ART_FLAGS', '--shoot'):
    crate = next((p for p in props if p[0] == 'crate'), None)
    barrel = next((p for p in props if p[0] == 'barrel'), None)
    tank = [p for p in props if p[0] == 'tank'][-1:]
    L += ['noclip']             # noclip blocks firing
    # crate: break it, then look at the splinters on the floor
    if crate:
        L += [f'setviewpos {cx:.0f} {cy:.0f} {cz - 18:.0f} {yaw(crate[1], crate[2]):.1f}', 'wait 30', '+attack', 'wait 150', '-attack',
              'cg_fov 30', 'wait 20', 'screenshotJPEG', 'wait 60', 'screenshotJPEG']
    # barrel: two or three rounds hole it (health <= 25) -> it burns, then blows
    if barrel:
        L += ['cg_fov 22', f'setviewpos {cx:.0f} {cy:.0f} {cz - 18:.0f} {yaw(barrel[1], barrel[2]):.1f}', 'wait 30', '+attack', 'wait 35', '-attack',
              'wait 15', 'screenshotJPEG', 'wait 50', 'screenshotJPEG']
    if tank:
        t = tank[0]
        L += ['cg_fov 40', f'setviewpos {cx:.0f} {cy:.0f} {cz - 18:.0f} {yaw(t[1], t[2]):.1f}', 'wait 30', '+attack', 'wait 40', '-attack',
              'wait 5', 'screenshotJPEG', 'wait 25', 'screenshotJPEG']
    L += ['cg_fov 50', 'wait 500', f'setviewpos {cx:.0f} {cy:.0f} {cz - 18:.0f} {yaw(mx, my):.1f}', 'wait 20', 'screenshotJPEG']
L += ['quit']
out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'home_art', 'rearmed', name + '.cfg')
open(out, 'w').write('\n'.join(L) + '\n')
print('wrote', out)
