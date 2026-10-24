"""Controls menu: bindable alt attack, offhand hook, dash and the Team Arena weapons."""
import sys, pathlib, re
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

F = str(pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code' / 'q3_ui' / 'ui_controls2.c')

# the non-binding controls move up so new binding ids can follow ID_TOGGLEMENU
# (g_bindings[] is indexed by id)
for name, old, new in (('ID_FREELOOK', 35, 60), ('ID_INVERTMOUSE', 36, 61), ('ID_ALWAYSRUN', 37, 62),
                       ('ID_AUTOSWITCH', 38, 63), ('ID_MOUSESPEED', 39, 64), ('ID_JOYENABLE', 40, 65),
                       ('ID_JOYTHRESHOLD', 41, 66), ('ID_SMOOTHMOUSE', 42, 67)):
    s = open(F, newline='').read()
    s2, n = re.subn(rf'(#define {name}\s+){old}\b', rf'\g<1>{new}', s)
    assert n == 1, name
    open(F, 'w', newline='').write(s2)

sub(F, '#define ID_TOGGLEMENU\t34\n', '''#define ID_TOGGLEMENU\t34
#define ID_ALTATTACK	35
#define ID_HOOK			36
#define ID_DASH			37
#define ID_WEAPON11		38
#define ID_WEAPON12		39
#define ID_WEAPON13		40
''')

sub(F, '''	{"togglemenu", 		"toggle menu",		ID_TOGGLEMENU,	ANIM_IDLE,		K_ESCAPE,		-1,		-1, -1},
''', '''	{"togglemenu", 		"toggle menu",		ID_TOGGLEMENU,	ANIM_IDLE,		K_ESCAPE,		-1,		-1, -1},
	{"+button12",		"alt attack",		ID_ALTATTACK,	ANIM_ATTACK,	K_MOUSE2,		-1,		-1, -1},
	{"+button13",		"grapple",			ID_HOOK,		ANIM_IDLE,		'e',			-1,		-1, -1},
	{"+button14",		"dash",				ID_DASH,		ANIM_RUN,		'q',			-1,		-1, -1},
	{"weapon 11",		"nailgun",			ID_WEAPON11,	ANIM_WEAPON10,	-1,				-1,		-1, -1},
	{"weapon 12",		"prox launcher",	ID_WEAPON12,	ANIM_WEAPON10,	-1,				-1,		-1, -1},
	{"weapon 13",		"chaingun",			ID_WEAPON13,	ANIM_WEAPON10,	-1,				-1,		-1, -1},
''')

sub(F, '\tmenuaction_s\t\tuseitem;\n', '\tmenuaction_s\t\tuseitem;\n\tmenuaction_s\t\taltattack;\n\tmenuaction_s\t\thook;\n\tmenuaction_s\t\tdash;\n\tmenuaction_s\t\tnailgun;\n\tmenuaction_s\t\tproxlauncher;\n\tmenuaction_s\t\tchaingun;\n')

sub(F, '''	(menucommon_s *)&s_controls.sidestep,
	NULL
};''', '''	(menucommon_s *)&s_controls.sidestep,
	(menucommon_s *)&s_controls.dash,
	(menucommon_s *)&s_controls.hook,
	NULL
};''')
sub(F, '''	(menucommon_s *)&s_controls.attack,
	(menucommon_s *)&s_controls.nextweapon,''', '''	(menucommon_s *)&s_controls.attack,
	(menucommon_s *)&s_controls.altattack,
	(menucommon_s *)&s_controls.nextweapon,''')
sub(F, '''	(menucommon_s *)&s_controls.bfg,
	NULL,
};''', '''	(menucommon_s *)&s_controls.bfg,
	(menucommon_s *)&s_controls.nailgun,
	(menucommon_s *)&s_controls.proxlauncher,
	(menucommon_s *)&s_controls.chaingun,
	NULL,
};''')

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
print('controls ok')
