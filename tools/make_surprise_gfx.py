"""DESCENT round 3 surprises (ds-surprise): the landing pillar's beam texture and the shaders
(gfx/rearmed/surprise/beam.tga, scripts/rearmed_surprise.shader). Our own generated content;
the ghost / gold shells reference env maps of the player's own Q3 install by path."""
import math, pathlib
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent / 'assets'
GFX = ROOT / 'gfx' / 'rearmed' / 'surprise'


def beam():
    """grey beam for additive blending: soft across (s), bright at the base (t = 1), fading up"""
    w, h = 64, 256
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        t = (y + 0.5) / h                      # 0 top .. 1 bottom
        vert = t ** 1.6 * 0.85 + 0.15 * math.exp(-((1.0 - t) * 18))
        for x in range(w):
            s = (x + 0.5) / w * 2 - 1          # -1 .. 1
            core = math.exp(-(s / 0.30) ** 2)
            halo = math.exp(-(s / 0.70) ** 2) * 0.5
            v = max(0.0, min(1.0, (core + halo) * vert))
            g = int(v * 255 + 0.5)
            px[x, y] = (g, g, g, 255)
    GFX.mkdir(parents=True, exist_ok=True)
    im.save(GFX / 'beam.tga')


SHADER = '''// DESCENT round 3 surprises (ds-surprise): tools/make_surprise_gfx.py

// a landing pillar / the golden meteor's line: an additive beam, coloured by the vertex colour
rearmed/dsSurBeam
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/surprise/beam.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// the ghost of a past run: a pale, see-through shell (no skin under it)
rearmed/dsGhost
{
	deformVertexes wave 80 sin 0.4 0.6 0 0.5
	sort additive
	{
		map textures/effects/envmap.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod scroll 0.02 0.05
		rgbGen const ( 0.40 0.48 0.60 )
	}
	{
		map textures/effects/invismap.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod turb 0 0.15 0 0.25
		rgbGen wave sin 0.22 0.08 0 0.4
	}
}

// the golden meteor: a molten gold sheen over the rock
rearmed/dsGold
{
	{
		map textures/effects/envmapgold.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod scroll 0.08 0.03
		rgbGen wave sin 0.75 0.2 0 1.4
	}
}
'''

if __name__ == '__main__':
    beam()
    (ROOT / 'scripts').mkdir(parents=True, exist_ok=True)
    (ROOT / 'scripts' / 'rearmed_surprise.shader').write_text(SHADER, newline='\n')
    print('surprise gfx: beam.tga, rearmed_surprise.shader')
