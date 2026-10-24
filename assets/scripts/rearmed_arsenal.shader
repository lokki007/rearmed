// DESCENT v2 ARSENAL (cg_ds_arsenal.c)

// evolved weapon shell: tinted per evolution by the entity colour
rearmed/dsEvolvedShell
{
	deformVertexes wave 100 sin 0.35 0 0 0
	{
		map textures/effects/envmapgold2.tga
		blendfunc GL_ONE GL_ONE
		tcGen environment
		tcmod scroll 1.2 0.4
		rgbGen entity
	}
}

// DOOMSDAY core: darkens what is behind it (a hole in the world)
rearmed/dsVoid
{
	nopicmip
	cull none
	sort nearest
	{
		clampmap gfx/rearmed/glow.tga
		blendFunc GL_ZERO GL_ONE_MINUS_SRC_COLOR
		rgbGen identity
	}
}
