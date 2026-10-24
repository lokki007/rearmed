// DESCENT (roguelike mode): elite and boss auras (rlcore worker)

rearmed/rlEliteShell
{
	deformVertexes wave 100 sin 2.2 0 0 0
	{
		map textures/effects/regenmap2.tga
		blendfunc GL_ONE GL_ONE
		tcGen environment
		tcmod rotate 45
		tcmod scroll 0.4 -1.1
		rgbGen const ( 0.9 0.38 0.04 )
	}
}

rearmed/rlBossShell
{
	deformVertexes wave 100 sin 3.2 0.6 0 0.9
	{
		map textures/effects/regenmap2.tga
		blendfunc GL_ONE GL_ONE
		tcGen environment
		tcmod rotate -70
		tcmod scroll -0.3 -1.6
		rgbGen const ( 0.6 0.08 0.9 )
	}
	{
		map textures/effects/envmapgold2.tga
		blendfunc GL_ONE GL_ONE
		tcGen environment
		tcmod scroll 1.5 0.3
		rgbGen wave sin 0.25 0.2 0 0.7
	}
}
