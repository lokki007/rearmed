// DESCENT round 3 CARDS (ds-cards): the DOUBLE AGENT's green turncoat shell
rearmed/lgAgentShell
{
	deformVertexes wave 100 sin 2.0 0 0 0
	{
		map textures/effects/regenmap2.tga
		blendfunc GL_ONE GL_ONE
		tcGen environment
		tcmod rotate 60
		tcmod scroll -0.5 1.2
		rgbGen const ( 0.15 0.95 0.3 )
	}
}
