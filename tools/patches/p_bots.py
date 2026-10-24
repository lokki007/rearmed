"""Hook ai_rearmed.c into the bot AI + headshot aim for skilled bots."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'ioq3'
g = lambda f: str(ROOT / 'code' / 'game' / f)

sub(str(ROOT / 'cmake' / 'basegame.cmake'), '    ${SOURCE_DIR}/game/ai_dmq3.c\n', '    ${SOURCE_DIR}/game/ai_dmq3.c\n    ${SOURCE_DIR}/game/ai_rearmed.c\n')

sub(g('ai_main.h'), '\tint enemy;\t\t\t\t\t\t\t\t\t\t//enemy entity number\n', '''	int enemy;										//enemy entity number
	// rearmed moves
	int rmNextDodge;								//next time to consider a dodge dash
	int rmHookUntil;								//holding the offhand hook until
	int rmHookNext;									//next time a hook is allowed
	int rmJumpPhase;								//double jump: 1 release, 2 press, 3 done
	qboolean rmWasOnGround;
	int rmAltToggle;
	int rmAltDecided;								//level.time of the last primary/alt choice
	qboolean rmAltChoice;
''')
sub(g('ai_main.h'), 'void BotEntityInfo(int entnum, aas_entityinfo_t *info);\n',
    'void BotEntityInfo(int entnum, aas_entityinfo_t *info);\nvoid BotRearmInput(bot_state_t *bs, usercmd_t *ucmd);\n')

sub(g('ai_main.c'), '''	//convert the bot input to a usercmd
	BotInputToUserCommand(&bi, &bs->lastucmd, bs->cur_ps.delta_angles, time);''', '''	//convert the bot input to a usercmd
	BotInputToUserCommand(&bi, &bs->lastucmd, bs->cur_ps.delta_angles, time);
	//dash, double jump, jetpack, grapple, alt-fire
	BotRearmInput(bs, &bs->lastucmd);''')

sub(g('ai_dmq3.c'), '''		bestorigin[0] += 20 * crandom() * (1 - aim_accuracy);
		bestorigin[1] += 20 * crandom() * (1 - aim_accuracy);
		bestorigin[2] += 10 * crandom() * (1 - aim_accuracy);
	}''', '''		bestorigin[0] += 20 * crandom() * (1 - aim_accuracy);
		bestorigin[1] += 20 * crandom() * (1 - aim_accuracy);
		bestorigin[2] += 10 * crandom() * (1 - aim_accuracy);
		//skilled bots go for headshots with hitscan weapons
		if (aim_skill > 0.5 && (wi.number == WP_MACHINEGUN || wi.number == WP_SHOTGUN || wi.number == WP_RAILGUN
#if defined(MISSIONPACK) || defined(TA_WEAPONS)
			|| wi.number == WP_CHAINGUN
#endif
			)) {
			if (random() < (aim_skill - 0.5) * 1.2) {
				bestorigin[2] += 16;
			}
		}
	}''')
print('bots ok')
