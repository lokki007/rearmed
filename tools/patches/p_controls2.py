"""Continuation of p_controls.py (anchors without trailing whitespace)."""
import sys, pathlib, re
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

F = str(pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code' / 'q3_ui' / 'ui_controls2.c')


def resub(pattern, repl):
    s = open(F, newline='').read()
    s2, n = re.subn(pattern, repl, s)
    assert n == 1, (pattern, n)
    open(F, 'w', newline='').write(s2)


resub(r'(\t\(menucommon_s \*\)&s_controls\.attack,[ \t]*(\r?\n))',
      r'\1\t(menucommon_s *)&s_controls.altattack,\2')
resub(r'(\t\(menucommon_s \*\)&s_controls\.bfg,[ \t]*(\r?\n))',
      r'\1\t(menucommon_s *)&s_controls.nailgun,\2\t(menucommon_s *)&s_controls.proxlauncher,\2\t(menucommon_s *)&s_controls.chaingun,\2')

init = ''
for field, ident in (('altattack', 'ID_ALTATTACK'), ('hook', 'ID_HOOK'), ('dash', 'ID_DASH'),
                     ('nailgun', 'ID_WEAPON11'), ('proxlauncher', 'ID_WEAPON12'), ('chaingun', 'ID_WEAPON13')):
    init += f'''	s_controls.{field}.generic.type	     = MTYPE_ACTION;
	s_controls.{field}.generic.flags     = QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS|QMF_GRAYED|QMF_HIDDEN;
	s_controls.{field}.generic.callback  = Controls_ActionEvent;
	s_controls.{field}.generic.ownerdraw = Controls_DrawKeyBinding;
	s_controls.{field}.generic.id        = {ident};

'''
sub(F, '''	s_controls.gesture.generic.type	     = MTYPE_ACTION;''', init + '''	s_controls.gesture.generic.type	     = MTYPE_ACTION;''')
sub(F, '''	Menu_AddItem( &s_controls.menu, &s_controls.bfg );
''', '''	Menu_AddItem( &s_controls.menu, &s_controls.bfg );
	Menu_AddItem( &s_controls.menu, &s_controls.nailgun );
	Menu_AddItem( &s_controls.menu, &s_controls.proxlauncher );
	Menu_AddItem( &s_controls.menu, &s_controls.chaingun );
	Menu_AddItem( &s_controls.menu, &s_controls.altattack );
	Menu_AddItem( &s_controls.menu, &s_controls.hook );
	Menu_AddItem( &s_controls.menu, &s_controls.dash );
''')
print('controls2 ok')
