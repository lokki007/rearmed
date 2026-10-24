"""ds-art: turntable close-ups with testmodel (fixed model orientation, so every painted side is shown).
python tools/art_turn.py <name> <x> <y> <floor_z>   (x, y = where the model floats; needs ~110 units clear around)"""
import math
import os
import sys

name, mx, my, fz = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
SIDES = {'props/barrel': (0, 123, 180, 270), 'props/tank': (0, 126, 240, 300), 'props/crate': (30, 135, 225)}
Z1 = fz + 4
L = ['wait 300', 'god', 'noclip', 'cg_drawGun 0', 'cg_draw2D 0', 'cg_drawFPS 0', 'cg_fov 34']
for model, sides in SIDES.items():
    L += [f'setviewpos {mx + 100:.0f} {my:.0f} {Z1:.0f} 180', 'wait 20', f'testmodel models/rearmed/{model}.md3', 'wait 10']
    for a in sides:
        r = math.radians(a)
        L += [f'setviewpos {mx + 100 * math.cos(r):.0f} {my + 100 * math.sin(r):.0f} {Z1 + 22:.0f} {(a + 180) % 360:.0f}', 'wait 25',
              'screenshotJPEG']
L += ['quit']
out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'home_art', 'rearmed', name + '.cfg')
open(out, 'w').write('\n'.join(L) + '\n')
print('wrote', out)
