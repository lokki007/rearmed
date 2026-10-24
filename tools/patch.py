"""CRLF-safe exact-text patcher. Usage in python: from patch import sub; sub(path, old, new)"""
def sub(path, old, new, count=1):
    s = open(path, newline='').read()
    crlf = '\r\n' in s
    if crlf:
        old = old.replace('\r\n', '\n').replace('\n', '\r\n')
        new = new.replace('\r\n', '\n').replace('\n', '\r\n')
    n = s.count(old)
    if n == 0:
        # fall back to ignoring trailing whitespace at line ends (editors strip it from patch scripts)
        import re
        nl = '\r\n' if crlf else '\n'
        pattern = r'[ \t]*' .join(re.escape(part) for part in old.split(nl))
        pattern = pattern.replace(re.escape(nl), '')  # (no-op, parts already split)
        rx = re.compile((r'[ \t]*' + re.escape(nl)).join(re.escape(part.rstrip(' \t')) for part in old.split(nl)))
        found = rx.findall(s)
        if len(found) == count:
            open(path, 'w', newline='').write(rx.sub(lambda m: new, s))
            return
    if n != count:
        raise SystemExit(f'{path}: expected {count} match(es), found {n}:\n{old[:200]}')
    open(path, 'w', newline='').write(s.replace(old, new))

TA_GUARD = '#if defined(MISSIONPACK) || defined(TA_WEAPONS)'

def reguard(path, anchor, max_lines=12):
    """Turn the nearest '#ifdef MISSIONPACK' above `anchor` (within max_lines) into the TA_WEAPONS guard."""
    s = open(path, newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    anchor = anchor.replace('\n', nl)
    if s.count(anchor) != 1:
        raise SystemExit(f'{path}: anchor found {s.count(anchor)}x: {anchor!r}')
    pos = s.index(anchor)
    guard = s.rfind('#ifdef MISSIONPACK', 0, pos)
    if guard < 0 or s.count(nl, guard, pos) > max_lines:
        raise SystemExit(f'{path}: no MISSIONPACK guard near {anchor!r}')
    s = s[:guard] + TA_GUARD + s[guard + len('#ifdef MISSIONPACK'):]
    open(path, 'w', newline='').write(s)

def reguard_all(path):
    """Turn every '#ifdef MISSIONPACK' in the file into the TA_WEAPONS guard."""
    s = open(path, newline='').read()
    n = s.count('#ifdef MISSIONPACK')
    s = s.replace('#ifdef MISSIONPACK', TA_GUARD)
    open(path, 'w', newline='').write(s)
    return n

def resume(script):
    """Re-run a patch script, skipping substitutions that were already applied."""
    import builtins
    global sub
    strict = sub

    def tolerant(path, old, new, count=1):
        def norm(t):
            return '\n'.join(line.rstrip(' \t') for line in t.replace('\r\n', '\n').split('\n'))
        if norm(new) in norm(open(path, newline='').read()):
            return		# already applied
        strict(path, old, new, count)
    sub = tolerant
    try:
        src = open(script).read().replace('from patch import sub', 'from patch import resume as _r')
        exec(compile(src, script, 'exec'), {'__file__': script, '__name__': '__main__', 'sub': tolerant})
    finally:
        sub = strict
