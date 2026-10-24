"""REARMED round 4 (en-feel) hit-feel textures, arm-less skins and the shader file.

  python tools/make_hitfeel_assets.py
writes
  assets/gfx/rearmed/hitfeel/crack1..3.tga, crackglow1..3.tga   armour cracks (procedural, our own)
  assets/scripts/rearmed_hitfeel.shader                          cracks, glow, nodraw, limb gib shaders
  assets/models/players/<model>/upper_<skin>_hf{l,r,b}.skin      arm-less torso skins

The skins are tiny surface -> shader maps made from the player's OWN installed skins (read from the
Steam install by path, never modified): the same lines with u_larm / u_rarm pointed at
rearmed/hitfeel/nodraw. Only models whose arms are separate surfaces get them (cgame falls back
to a stump + blood for the rest). They contain no art, only texture paths of the installed game.
The limb gib MD3s come from tools/blender/make_hitfeel.py.
"""
import pathlib
import zipfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / 'assets'
GFX = ASSETS / 'gfx' / 'rearmed' / 'hitfeel'
Q3 = pathlib.Path(r'H:/SteamLibrary/steamapps/common/Quake 3 Arena')
PAKS = [Q3 / 'baseq3' / 'pak0.pk3', Q3 / 'missionpack' / 'pak0.pk3']


def save(name, rgb, alpha=None):
    GFX.mkdir(parents=True, exist_ok=True)
    h, w = rgb.shape[:2]
    if alpha is None:
        img = Image.fromarray(np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8), 'RGB')
    else:
        out = np.zeros((h, w, 4), np.uint8)
        out[..., :3] = np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8)
        out[..., 3] = np.clip(alpha * 255 + 0.5, 0, 255).astype(np.uint8)
        img = Image.fromarray(out, 'RGBA')
    img.save(GFX / name)


def crack_field(size, cells, seed):
    """Tiling voronoi edge distance: small = on a crack. Returns (dist, cell id)."""
    rng = np.random.default_rng(seed)
    pts = rng.random((cells, 2))
    yy, xx = np.mgrid[0:size, 0:size]
    p = np.stack([(xx + 0.5) / size, (yy + 0.5) / size], -1)
    d1 = np.full((size, size), 9.0)
    d2 = np.full((size, size), 9.0)
    idx = np.zeros((size, size), np.int32)
    for i, c in enumerate(pts):
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                d = np.hypot(p[..., 0] - c[0] - ox, p[..., 1] - c[1] - oy)
                closer = d < d1
                d2 = np.where(closer, d1, np.minimum(d2, d))
                idx = np.where(closer, i, idx)
                d1 = np.where(closer, d, d1)
    return d2 - d1, idx


def wobble(size, seed, amp):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:size, 0:size] / size
    w = np.zeros((size, size))
    for k in range(1, 5):
        a, b, ph = rng.random(3) * 6.28
        w += np.sin(2 * np.pi * k * (xx * np.cos(a) + yy * np.sin(a)) * 2 + ph) / k
    return w * amp


def make_cracks():
    size = 512
    for stage, (cells, width, keep) in enumerate(((7, 0.012, 0.45), (12, 0.016, 0.7), (20, 0.021, 1.0)), 1):
        edge, idx = crack_field(size, cells, 70 + stage)
        edge = edge + wobble(size, 90 + stage, 0.004)
        rng = np.random.default_rng(stage)
        # only some cells' borders crack early on (keep = share of cells whose edges show)
        on = rng.random(cells) < keep
        mask = np.clip(1.0 - np.abs(edge) / width, 0, 1) ** 1.5
        mask *= on[idx] | (stage == 3)
        # hair-line branches off the main cracks
        fine, _ = crack_field(size, cells * 3, 170 + stage)
        fine = np.clip(1.0 - np.abs(fine + wobble(size, 190 + stage, 0.003)) / (width * 0.45), 0, 1)
        fine *= np.clip(1.0 - np.abs(edge) / (width * 9), 0, 1)       # only near a main crack
        m = np.clip(mask + fine * 0.6, 0, 1)
        # multiply map: dark fissure (rgb) where alpha = crack
        rgb = np.zeros((size, size, 3))
        rgb[..., 0] = 0.08 * (1 - m) + 0.02
        rgb[..., 1] = 0.06 * (1 - m) + 0.02
        rgb[..., 2] = 0.05 * (1 - m) + 0.02
        save('crack%d.tga' % stage, rgb * m[..., None], m * 0.95)
        # glow: a hot rim along the main cracks only
        g = np.clip(1.0 - np.abs(edge) / (width * 1.3), 0, 1) ** 2 * (on[idx] | (stage == 3))
        glow = np.stack([g * 1.0, g * 0.55, g * 0.18], -1)
        save('crackglow%d.tga' % stage, glow)


SHADER = """// REARMED round 4 hit feel (en-feel), written by tools/make_hitfeel_assets.py. Do not edit by hand.

// armour cracks shell (cg_hitfeel.c CG_HF_BodyShell): dark fissures multiplied over the body
rearmed/hitfeel/crack1
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crack1.tga
		tcMod scale 2.5 2.5
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}
rearmed/hitfeel/crack2
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crack2.tga
		tcMod scale 2.5 2.5
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}
rearmed/hitfeel/crack3
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crack3.tga
		tcMod scale 2.5 2.5
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}
// the hot rim along the cracks (additive, pulsing via the entity colour)
rearmed/hitfeel/crackglow1
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crackglow1.tga
		tcMod scale 2.5 2.5
		blendFunc GL_ONE GL_ONE
		rgbGen entity
	}
}
rearmed/hitfeel/crackglow2
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crackglow2.tga
		tcMod scale 2.5 2.5
		blendFunc GL_ONE GL_ONE
		rgbGen entity
	}
}
rearmed/hitfeel/crackglow3
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crackglow3.tga
		tcMod scale 2.5 2.5
		blendFunc GL_ONE GL_ONE
		rgbGen entity
	}
}

// arm-less skins point the lost arm's surface here: no stages, nothing is drawn
rearmed/hitfeel/nodraw
{
	surfaceparm nodraw
	surfaceparm nomarks
	surfaceparm nonsolid
}

// limb / helmet gibs (tools/blender/make_hitfeel.py): flesh with a wet sheen, the helmet is metal
models/rearmed/gibs/arm
{
	{
		map models/rearmed/gibs/arm.tga
		rgbGen lightingDiffuse
	}
	{
		map models/rearmed/gibs/wetenv_dim.tga
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen lightingDiffuse
	}
}
models/rearmed/gibs/leg
{
	{
		map models/rearmed/gibs/leg.tga
		rgbGen lightingDiffuse
	}
	{
		map models/rearmed/gibs/wetenv_dim.tga
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen lightingDiffuse
	}
}
models/rearmed/gibs/helmet
{
	{
		map models/rearmed/gibs/helmet.tga
		rgbGen lightingDiffuse
	}
	{
		map models/rearmed/gibs/wetenv.tga
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen lightingDiffuse
	}
}
"""


def make_shader():
    p = ASSETS / 'scripts' / 'rearmed_hitfeel.shader'
    p.write_text(SHADER, newline='\n')


def make_skins():
    n = 0
    for pak in PAKS:
        if not pak.exists():
            print('skip (not installed):', pak)
            continue
        z = zipfile.ZipFile(pak)
        names = z.namelist()
        for name in names:
            low = name.lower()
            if not (low.startswith('models/players/') and '/upper_' in low and low.endswith('.skin')):
                continue
            text = z.read(name).decode('latin-1')
            lines = [ln.strip() for ln in text.replace('\r', '').split('\n') if ln.strip()]
            surfs = [ln.split(',')[0].strip().lower() for ln in lines]
            if 'u_larm' not in surfs or 'u_rarm' not in surfs:
                continue
            base = name[:-5]
            for suffix, hide in (('_hfl', ('u_larm',)), ('_hfr', ('u_rarm',)), ('_hfb', ('u_larm', 'u_rarm'))):
                out = []
                for ln in lines:
                    s = ln.split(',')[0].strip()
                    if s.lower() in hide:
                        out.append('%s,rearmed/hitfeel/nodraw' % s)
                    else:
                        out.append(ln)
                dst = ASSETS / (base + suffix + '.skin')
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_text('\n'.join(out) + '\n', newline='\n')
                n += 1
    print('arm-less skins written:', n)


make_cracks()
make_shader()
make_skins()
print('hit-feel assets written')
