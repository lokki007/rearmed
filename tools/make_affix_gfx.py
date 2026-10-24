"""DESCENT round 4 affixes (en-affix): sprites, the frost screen edge, the tether beam, the ground
ring and the shaders (gfx/rearmed/affix/*.tga, scripts/rearmed_affix.shader). Our own generated
content; the chrome / gold shells reference env maps of the player's own Q3 install by path."""
import math, random, pathlib
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent / 'assets'
GFX = ROOT / 'gfx' / 'rearmed' / 'affix'


def save(im, name):
    GFX.mkdir(parents=True, exist_ok=True)
    im.save(GFX / name)


def grey(v):
    g = int(max(0.0, min(1.0, v)) * 255 + 0.5)
    return (g, g, g, 255)


def value_noise(w, h, cell, seed):
    rnd = random.Random(seed)
    gw, gh = w // cell + 2, h // cell + 2
    grid = [[rnd.random() for _ in range(gw)] for _ in range(gh)]

    def at(x, y):
        fx, fy = x / cell, y / cell
        ix, iy = int(fx), int(fy)
        tx, ty = fx - ix, fy - iy
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a = grid[iy % gh][ix % gw] * (1 - tx) + grid[iy % gh][(ix + 1) % gw] * tx
        b = grid[(iy + 1) % gh][ix % gw] * (1 - tx) + grid[(iy + 1) % gh][(ix + 1) % gw] * tx
        return a * (1 - ty) + b * ty
    return at


def mist():
    """a soft, ragged smoke puff: white, the shape in alpha (blended, tinted by the vertex colour)"""
    n = 64
    im = Image.new('RGBA', (n, n))
    px = im.load()
    a1, a2 = value_noise(n, n, 8, 3), value_noise(n, n, 4, 4)
    for y in range(n):
        for x in range(n):
            dx, dy = (x + 0.5) / n * 2 - 1, (y + 0.5) / n * 2 - 1
            r = math.sqrt(dx * dx + dy * dy)
            fall = max(0.0, 1.0 - r) ** 1.4
            nz = 0.55 * a1(x, y) + 0.45 * a2(x, y)
            a = max(0.0, min(1.0, fall * (0.35 + 0.9 * nz)))
            px[x, y] = (255, 255, 255, int(a * 255))
    save(im, 'mist.tga')


def crystal():
    """a little six-pointed ice crystal (additive)"""
    n = 32
    im = Image.new('RGBA', (n, n))
    px = im.load()
    for y in range(n):
        for x in range(n):
            dx, dy = (x + 0.5) / n * 2 - 1, (y + 0.5) / n * 2 - 1
            r = math.sqrt(dx * dx + dy * dy)
            ang = math.atan2(dy, dx)
            arm = abs(math.cos(3 * ang))           # six arms
            v = max(0.0, 1.0 - r / (0.25 + 0.75 * arm ** 6)) ** 1.2
            v = max(v, max(0.0, 1.0 - r * 3.0))    # bright core
            px[x, y] = grey(v)
    save(im, 'crystal.tga')


def shard():
    """a long sharp diamond with a bright rim (violet shards, split tears)"""
    n = 32
    im = Image.new('RGBA', (n, n))
    px = im.load()
    for y in range(n):
        for x in range(n):
            dx, dy = (x + 0.5) / n * 2 - 1, (y + 0.5) / n * 2 - 1
            d = abs(dx) / 0.35 + abs(dy) / 0.95      # diamond distance
            v = 0.0
            if d < 1.0:
                v = 0.45 + 0.55 * (d ** 3)            # bright rim, softer inside
            v = max(v, max(0.0, 1.0 - d) * 0.3 + (0.25 * max(0.0, 1.2 - d) if d < 1.2 else 0))
            px[x, y] = grey(v)
    save(im, 'shard.tga')


def glint():
    """a four-pointed star twinkle (reflect glints)"""
    n = 32
    im = Image.new('RGBA', (n, n))
    px = im.load()
    for y in range(n):
        for x in range(n):
            dx, dy = abs((x + 0.5) / n * 2 - 1), abs((y + 0.5) / n * 2 - 1)
            v = max(0.0, 1.0 - dx * 9) * max(0.0, 1.0 - dy) + max(0.0, 1.0 - dy * 9) * max(0.0, 1.0 - dx)
            v += max(0.0, 1.0 - math.sqrt(dx * dx + dy * dy) * 3.5)
            px[x, y] = grey(v)
    save(im, 'glint.tga')


def frost_edges():
    """frost creeping in from the screen edge: bright at the edge, a crystalline fringe inward.
    edge_h: the left edge (x 0 = the edge), edge_v: the top edge (y 0 = the edge)"""
    w, h = 128, 256
    n1, n2 = value_noise(w, h, 16, 11), value_noise(w, h, 5, 12)
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            t = x / (w - 1)
            reach = 0.55 + 0.45 * n1(x * 0.5, y)          # the frost line wanders
            base = max(0.0, 1.0 - t / reach) ** 1.6
            crys = n2(x, y)
            fringe = 1.0 if crys > 0.62 and t < reach * 1.1 else 0.0
            v = base * (0.55 + 0.45 * crys) + 0.25 * fringe * max(0.0, 1.0 - t / (reach * 1.1))
            px[x, y] = grey(v)
    im.save
    save(im, 'frostedge_h.tga')
    save(im.transpose(Image.TRANSPOSE), 'frostedge_v.tga')


def beam():
    """a tether / pillar core: bright across the middle (t), a few soft knots along (s) for the scroll"""
    w, h = 64, 32
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        t = (y + 0.5) / h * 2 - 1
        across = math.exp(-(t / 0.28) ** 2) + 0.35 * math.exp(-(t / 0.7) ** 2)
        for x in range(w):
            s = x / w
            knots = 0.75 + 0.25 * math.sin(2 * math.pi * 2 * s) ** 2
            px[x, y] = grey(across * knots)
    save(im, 'beam.tga')


def ring():
    """a thin glowing ring with a faint fill (the pull wind-up, the champion landing)"""
    n = 128
    im = Image.new('RGBA', (n, n))
    px = im.load()
    for y in range(n):
        for x in range(n):
            dx, dy = (x + 0.5) / n * 2 - 1, (y + 0.5) / n * 2 - 1
            r = math.sqrt(dx * dx + dy * dy)
            v = math.exp(-((r - 0.86) / 0.06) ** 2) + 0.12 * max(0.0, 1.0 - r) if r < 1.0 else 0.0
            px[x, y] = grey(v)
    save(im, 'ring.tga')


SHADER = '''// DESCENT round 4 affixes (en-affix): tools/make_affix_gfx.py

// vampire mist: a blended puff, colour and fade from the vertex
rearmed/dsaMist
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/mist.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/dsaCrystal
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/crystal.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

rearmed/dsaShard
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/shard.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

rearmed/dsaGlint
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/glint.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// the reflect flash: a chrome shell over the body for a moment (brightness from the entity colour)
rearmed/dsaChrome
{
	deformVertexes wave 100 sin 1.5 0 0 0
	sort additive
	{
		map textures/effects/envmap.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen entity
	}
}

// champion pack members: a faint gold sheen
rearmed/dsaGold
{
	deformVertexes wave 100 sin 0.8 0 0 0
	sort additive
	{
		map textures/effects/envmapgold.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod scroll 0.05 0.02
		rgbGen entity
	}
}

rearmed/dsaFrostEdgeH
{
	nopicmip
	nomipmaps
	{
		clampmap gfx/rearmed/affix/frostedge_h.tga
		blendFunc add
		rgbGen vertex
	}
}

rearmed/dsaFrostEdgeV
{
	nopicmip
	nomipmaps
	{
		clampmap gfx/rearmed/affix/frostedge_v.tga
		blendFunc add
		rgbGen vertex
	}
}

// the pull tether / the champion pillar: a flowing additive beam
rearmed/dsaBeam
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/beam.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		tcMod scroll -1.5 0
	}
}

rearmed/dsaRing
{
	nopicmip
	cull none
	sort additive
	{
		clampmap gfx/rearmed/affix/ring.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}
'''

if __name__ == '__main__':
    mist()
    crystal()
    shard()
    glint()
    frost_edges()
    beam()
    ring()
    (ROOT / 'scripts').mkdir(parents=True, exist_ok=True)
    (ROOT / 'scripts' / 'rearmed_affix.shader').write_text(SHADER)
    print('affix gfx written to', GFX)
