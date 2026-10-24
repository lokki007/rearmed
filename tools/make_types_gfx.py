"""Round 4 enemy archetypes (en-types): effect textures and shaders.
gfx/rearmed/types/flare.tga (soft glow), ring.tga (ground ring with notches), beam.tga (a soft
beam for RT_LIGHTNING / rail beams), shield_hex.tga (the riot shield's energy face) and
scripts/rearmed_types.shader. Our own generated content; the shells reference env maps of the
player's own Q3 install by path. The shield frame model: tools/blender/make_shield.py.
Run: python tools/make_types_gfx.py"""
import math, pathlib
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent / 'assets'
GFX = ROOT / 'gfx' / 'rearmed' / 'types'


def save(name, im):
    GFX.mkdir(parents=True, exist_ok=True)
    im.save(GFX / name)


def flare():
    s = 128
    im = Image.new('RGBA', (s, s))
    px = im.load()
    for y in range(s):
        for x in range(s):
            dx, dy = (x + 0.5) / s * 2 - 1, (y + 0.5) / s * 2 - 1
            r = math.hypot(dx, dy)
            v = math.exp(-(r / 0.22) ** 2) + 0.35 * math.exp(-(r / 0.55) ** 2)
            # a faint four-point star
            v += 0.25 * math.exp(-(abs(dx) / 0.03) ** 2) * max(0.0, 1 - abs(dy)) ** 2
            v += 0.25 * math.exp(-(abs(dy) / 0.03) ** 2) * max(0.0, 1 - abs(dx)) ** 2
            v *= max(0.0, 1 - r) ** 0.5 if r < 1 else 0.0
            g = int(max(0.0, min(1.0, v)) * 255 + 0.5)
            px[x, y] = (g, g, g, 255)
    save('flare.tga', im)


def ring():
    s = 256
    im = Image.new('RGBA', (s, s))
    px = im.load()
    for y in range(s):
        for x in range(s):
            dx, dy = (x + 0.5) / s * 2 - 1, (y + 0.5) / s * 2 - 1
            r = math.hypot(dx, dy)
            a = math.atan2(dy, dx)
            band = math.exp(-((r - 0.82) / 0.05) ** 2)
            inner = 0.45 * math.exp(-((r - 0.62) / 0.025) ** 2)
            # notches / runes on the outer band
            notch = 0.5 + 0.5 * math.cos(a * 18)
            inner *= 0.4 + 0.6 * (notch > 0.7)
            glow = 0.18 * math.exp(-((r - 0.8) / 0.18) ** 2)
            v = band + inner + glow
            v *= 1.0 if r < 1 else 0.0
            g = int(max(0.0, min(1.0, v)) * 255 + 0.5)
            px[x, y] = (g, g, g, 255)
    save('ring.tga', im)


def beam():
    w, h = 128, 32
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        t = (y + 0.5) / h * 2 - 1
        across = math.exp(-(t / 0.28) ** 2) + 0.35 * math.exp(-(t / 0.7) ** 2)
        for x in range(w):
            u = x / w
            along = 0.75 + 0.25 * math.sin(2 * math.pi * 3 * u) * math.sin(2 * math.pi * 7 * u + 1.0)
            g = int(max(0.0, min(1.0, across * along)) * 255 + 0.5)
            px[x, y] = (g, g, g, 255)
    save('beam.tga', im)


def shield_hex():
    """cyan hexagon lattice, bright edges and rim, dim cells (additive)"""
    w, h = 128, 256
    im = Image.new('RGBA', (w, h))
    px = im.load()
    size = 0.085
    for y in range(h):
        for x in range(w):
            u, v = (x + 0.5) / w, (y + 0.5) / h
            # hex distance: axial coords on a pointy grid
            qx, qy = u / size, v * 2.0 / size
            q = (qx * math.sqrt(3) / 3 - qy / 3)
            r = qy * 2 / 3
            # cube round
            cx, cz = q, r
            cy = -cx - cz
            rx, ry, rz = round(cx), round(cy), round(cz)
            dx, dy, dz = abs(rx - cx), abs(ry - cy), abs(rz - cz)
            if dx > dy and dx > dz:
                rx = -ry - rz
            elif dy > dz:
                ry = -rx - rz
            else:
                rz = -rx - ry
            d = max(abs(cx - rx), abs(cy - ry), abs(cz - rz))      # 0 centre .. 0.5 edge
            edge = math.exp(-((0.5 - d) / 0.06) ** 2)
            cell = 0.12 + 0.06 * math.sin(rx * 1.7 + rz * 2.3)
            rim = max(math.exp(-(min(u, 1 - u) / 0.035) ** 2), math.exp(-(min(v, 1 - v) / 0.02) ** 2))
            k = min(1.0, edge * 0.8 + cell + rim)
            px[x, y] = (int(80 * k), int(200 * k), int(255 * k), 255)
    save('shield_hex.tga', im)


SHADER = '''// round 4 enemy archetypes (en-types): tools/make_types_gfx.py

// glints, flashes, beep lights: an additive soft flare coloured by the entity / vertex colour
rearmed/dsTypeFlare
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/types/flare.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// the summoner's ritual ring, the kamikaze's blast ring
rearmed/dsTypeRing
{
	nopicmip
	cull none
	sort additive
	polygonOffset
	{
		map gfx/rearmed/types/ring.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// heal beams, ritual pillars, blink columns (RT_LIGHTNING): soft, scrolling
rearmed/dsTypeBeam
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/types/beam.tga
		blendFunc GL_ONE GL_ONE
		tcMod scroll -1.6 0
		rgbGen vertex
	}
}

// juggernaut gunmetal / mimic rim / kamikaze heat: an env-map shell tinted by the entity colour
rearmed/dsTypeShell
{
	deformVertexes wave 100 sin 0.6 0 0 0
	{
		map textures/effects/envmapdim.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod scroll 0.02 0.05
		rgbGen entity
	}
}

// the mimic: darkens the copy of your model to ink (multiplied over the skin)
rearmed/dsTypeDark
{
	deformVertexes wave 100 sin 0.25 0 0 0
	{
		map $whiteimage
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen const ( 0.22 0.22 0.27 )
	}
}

// the riot shield's energy face (flat fallback polygon)
rearmed/dsTypeShieldFace
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/types/shield_hex.tga
		blendFunc GL_ONE GL_ONE
		tcMod scroll 0 0.08
		rgbGen vertex
	}
}

// the riot shield's energy face on the model (models/rearmed/types/shield_face.md3)
models/rearmed/types/shield_face
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/types/shield_hex.tga
		blendFunc GL_ONE GL_ONE
		tcMod scroll 0 0.08
		rgbGen entity
	}
}
'''

if __name__ == '__main__':
    flare()
    ring()
    beam()
    shield_hex()
    (ROOT / 'scripts').mkdir(parents=True, exist_ok=True)
    (ROOT / 'scripts' / 'rearmed_types.shader').write_text(SHADER, newline='\n')
    print('types gfx: flare, ring, beam, shield_hex, rearmed_types.shader')
