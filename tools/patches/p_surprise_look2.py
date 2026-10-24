"""ds-surprise: a wider dev look search (one-shot)."""
import pathlib
p = pathlib.Path(__file__).resolve().parent.parent.parent / 'ioq3' / 'code' / 'game' / 'g_ds_surprise.c'
s = open(p, newline='').read()
for old, new in (
        ('if ( dd < 650 || dd > 1300 ) {', 'if ( dd < 650 || dd > 2400 ) {'),
        ('if ( fabs( dd - 750 ) < bestScore ) {', 'if ( fabs( dd - 900 ) < bestScore ) {'),
        ('bestScore = fabs( dd - 750 );', 'bestScore = fabs( dd - 900 );'),
        ('for ( k = 0 ; k < 48 ; k++ ) {', 'for ( k = 0 ; k < 96 ; k++ ) {'),
        ('dd = 650 + ( k % 4 ) * 120;', 'dd = 650 + ( k % 6 ) * 130;')):
    assert s.count(old) == 1, old
    s = s.replace(old, new)
open(p, 'w', newline='').write(s)
print('ok')
