// bg_killcam.h -- kill cam / replay / death cam protocol shared by game (g_killcam.c) and cgame (cg_killcam.c)
//
// server -> the human killer:  rmkc <victim> <KCF_* flags> <weapon> <quick kills>
// server -> the human victim:  rmkiller <killer> <health> <armor> <weapon>

#ifndef BG_KILLCAM_H
#define BG_KILLCAM_H

#define KCF_GIB			0x0001		// the body burst
#define KCF_HEADSHOT	0x0002		// headshot finisher (decapitation)
#define KCF_AIRSHOT		0x0004		// direct hit on an airborne target
#define KCF_MIDAIR		0x0008		// both in the air
#define KCF_ENV			0x0010		// knocked into the void / lava / a crusher
#define KCF_FINAL		0x0020		// the match / floor winning frag
#define KCF_BOSS		0x0040		// a DESCENT boss
#define KCF_ELITE		0x0080		// a DESCENT elite
#define KCF_CHARGED		0x0100		// charged alt-fire
#define KCF_NEMESIS		0x0200		// the player's nemesis (cgame: CG_KillCamNotable)
#define KCF_PHASE		0x0400		// a boss phase broken (cgame: CG_KillCamNotable)
#define KCF_OVERKILL	0x0800		// torn apart by far more damage than needed
#define KCF_MELEE		0x1000		// gauntlet / ground pound

#define KCF_NOTABLE		( KCF_GIB | KCF_HEADSHOT | KCF_AIRSHOT | KCF_MIDAIR | KCF_ENV | KCF_FINAL | KCF_BOSS \
						| KCF_NEMESIS | KCF_PHASE | KCF_OVERKILL )

#endif
