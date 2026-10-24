"""Remix definitions: each edit(m) gets a remix.MapFile to modify."""
from remix import box_brush

STONE = 'base_trim/pewter_shiney'


def swap(m, origin, classname):
    """Replace the item standing at origin."""
    for e in m.ents:
        if e['keys'].get('origin') == origin:
            e['keys']['classname'] = classname
            return
    raise KeyError(origin)


def light(m, origin, intensity, color):
    m.add({'classname': 'light', 'origin': '%d %d %d' % origin, 'light': str(intensity), '_color': '%.2f %.2f %.2f' % color})


def stone(m, center, half=48, thick=16, lit=(0.3, 0.8, 1.0)):
    """Floating platform with an accent light below it."""
    x, y, z = center
    world = m.find(classname='worldspawn')[0]
    world['brushes'] += '\t// rearmed stone\n' + box_brush((x - half, y - half, z - thick), (x + half, y + half, z), STONE)
    light(m, (x, y, z - thick - 24), 300, lit)
    light(m, (x, y, z + 48), 120, lit)


def unchanged(m):
    pass


def yard(m):
    m.find(classname='worldspawn')[0]['keys']['message'] = 'The Longest Yard: Rearmed'
    # team arena guns replace the two shotguns
    swap(m, '-576 1216 592', 'weapon_chaingun')
    swap(m, '-448 1280 592', 'ammo_belt')
    swap(m, '-576 -1088 592', 'weapon_nailgun')
    swap(m, '-448 -1152 592', 'ammo_nails')
    # stepping stones from the lower deck to the railgun platform: dash / double jump route
    for x, z in ((1380, 72), (1680, 150), (1980, 216)):
        stone(m, (x, 64, z))
    m.add({'classname': 'weapon_prox_launcher', 'origin': '1680 64 174'})
    m.add({'classname': 'ammo_mines', 'origin': '1980 64 240'})
    # a jetpack on the lower deck: the arena is mostly sky
    m.add({'classname': 'item_jetpack', 'origin': '1072 64 40'})


def tint_lights(m, color, scale=1.0):
    """Recolor every light (mood change for the whole level)."""
    for e in m.find(classname='light'):
        k = e['keys']
        base = [float(c) for c in k.get('_color', '1 1 1').split()]
        k['_color'] = '%.2f %.2f %.2f' % tuple(b * c for b, c in zip(base, color))
        k['light'] = str(int(float(k.get('light', '300')) * scale))


def camp(m):
    m.find(classname='worldspawn')[0]['keys']['message'] = 'The Campgrounds: Rearmed'
    swap(m, '192 -320 16', 'weapon_chaingun')
    swap(m, '144 -176 16', 'ammo_belt')
    swap(m, '-1144 -832 208', 'weapon_prox_launcher')
    swap(m, '896 -672 400', 'ammo_mines')
    tint_lights(m, (0.8, 0.9, 1.15), 1.05)


def lost(m):
    m.find(classname='worldspawn')[0]['keys']['message'] = 'Lost World: Rearmed'
    swap(m, '548 1288 464', 'weapon_prox_launcher')
    swap(m, '988 544 208', 'ammo_mines')
    swap(m, '816 720 336', 'ammo_mines')
    swap(m, '-40 304 -136', 'weapon_chaingun')
    swap(m, '-688 480 336', 'ammo_belt')
    swap(m, '1128 480 208', 'weapon_nailgun')
    swap(m, '1184 480 208', 'ammo_nails')
    tint_lights(m, (1.15, 0.9, 0.7), 1.05)


REMIXES = {
    'rm_test': dict(source='q3dm17', title='test', edit=unchanged),
    'rm_yard': dict(source='q3dm17', title='The Longest Yard: Rearmed', edit=yard),
    'rm_camp': dict(source='q3dm6', title='The Campgrounds: Rearmed', edit=camp),
    'rm_lost': dict(source='q3dm13', title='Lost World: Rearmed', edit=lost),
}
