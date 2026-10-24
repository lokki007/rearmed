// DESCENT round 4 affixes (en-affix): tools/make_affix_gfx.py

// vampire mist: a blended puff, colour and fade from the vertex
rearmed/dsaMist
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/mist.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/dsaCrystal
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/crystal.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

rearmed/dsaShard
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/shard.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

rearmed/dsaGlint
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/glint.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// the reflect flash: a chrome shell over the body for a moment (brightness from the entity colour)
rearmed/dsaChrome
{
	deformVertexes wave 100 sin 1.5 0 0 0
	sort additive
	{
		map textures/effects/envmap.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen entity
	}
}

// champion pack members: a faint gold sheen
rearmed/dsaGold
{
	deformVertexes wave 100 sin 0.8 0 0 0
	sort additive
	{
		map textures/effects/envmapgold.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod scroll 0.05 0.02
		rgbGen entity
	}
}

rearmed/dsaFrostEdgeH
{
	nopicmip
	nomipmaps
	{
		clampmap gfx/rearmed/affix/frostedge_h.tga
		blendFunc add
		rgbGen vertex
	}
}

rearmed/dsaFrostEdgeV
{
	nopicmip
	nomipmaps
	{
		clampmap gfx/rearmed/affix/frostedge_v.tga
		blendFunc add
		rgbGen vertex
	}
}

// the pull tether / the champion pillar: a flowing additive beam
rearmed/dsaBeam
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/affix/beam.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		tcMod scroll -1.5 0
	}
}

rearmed/dsaRing
{
	nopicmip
	cull none
	sort additive
	{
		clampmap gfx/rearmed/affix/ring.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}
