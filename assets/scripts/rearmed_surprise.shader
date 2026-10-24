// DESCENT round 3 surprises (ds-surprise): tools/make_surprise_gfx.py

// a landing pillar / the golden meteor's line: an additive beam, coloured by the vertex colour
rearmed/dsSurBeam
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/surprise/beam.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// the ghost of a past run: a pale, see-through shell (no skin under it)
rearmed/dsGhost
{
	deformVertexes wave 80 sin 0.4 0.6 0 0.5
	sort additive
	{
		map textures/effects/envmap.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod scroll 0.02 0.05
		rgbGen const ( 0.40 0.48 0.60 )
	}
	{
		map textures/effects/invismap.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod turb 0 0.15 0 0.25
		rgbGen wave sin 0.22 0.08 0 0.4
	}
}

// the golden meteor: a molten gold sheen over the rock
rearmed/dsGold
{
	{
		map textures/effects/envmapgold.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod scroll 0.08 0.03
		rgbGen wave sin 0.75 0.2 0 1.4
	}
}
