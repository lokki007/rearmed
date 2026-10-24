"""Pack compiled QVMs and mod assets into home/rearmed/*.pk3"""
import os, zipfile, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
out = pathlib.Path(os.environ['PK3_OUT']) if os.environ.get('PK3_OUT') else root / 'home' / 'rearmed'
out.mkdir(parents=True, exist_ok=True)

def pack(name, files):
    tmp = out / (name + '.tmp')
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for arc, src in files:
            z.write(src, arc)
    os.replace(tmp, out / name)
    print(f'{name}: {len(files)} files')

vm = root / 'ioq3' / 'build' / 'Release' / 'baseq3' / 'vm'
pack('zz-rearmed-code.pk3', [(f'vm/{f.name}', f) for f in vm.glob('*.qvm')])

# assets/: our own content. assets_ta/: extracted from the user's Team Arena install (personal use only)
for folder, name in (('assets_ta', 'zz-rearmed-ta.pk3'), ('assets', 'zz-rearmed-assets.pk3'), ('maps_build', 'zz-rearmed-maps.pk3')):
    d = root / folder
    if d.is_dir():
        files = [(f.relative_to(d).as_posix(), f) for f in d.rglob('*') if f.is_file()]
        # ds-maps: assets_ta holds the Team Arena maps (~250 MB): repack only when something changed
        if folder == 'assets_ta' and (out / name).is_file() and all(f.stat().st_mtime < (out / name).stat().st_mtime for _, f in files):
            with zipfile.ZipFile(out / name) as z:
                if len(z.namelist()) == len(files):
                    print(f'{name}: unchanged ({len(files)} files)')
                    continue
        pack(name, files)
