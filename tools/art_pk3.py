"""ds-art test overlay: pack the prop/debris assets + rearmed_v4.shader into home_art/rearmed/zzz-art.pk3
(sorts after zz-rearmed-assets.pk3, so it wins). Test install only; build.sh packs the real pk3."""
import os
import zipfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = os.path.join(ROOT, 'assets')
out = os.path.join(ROOT, 'home_art', 'rearmed', 'zzz-art.pk3')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for d in ('models/rearmed/props', 'models/rearmed/debris'):
        for f in os.listdir(os.path.join(A, d)):
            z.write(os.path.join(A, d, f), d + '/' + f)
    z.write(os.path.join(A, 'scripts', 'rearmed_v4.shader'), 'scripts/rearmed_v4.shader')
print('wrote', out)
