// round 4 enemy archetypes (en-types): tools/make_types_gfx.py

// glints, flashes, beep lights: an additive soft flare coloured by the entity / vertex colour
rearmed/dsTypeFlare
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/types/flare.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// the summoner's ritual ring, the kamikaze's blast ring
rearmed/dsTypeRing
{
	nopicmip
	cull none
	sort additive
	polygonOffset
	{
		map gfx/rearmed/types/ring.tga
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// heal beams, ritual pillars, blink columns (RT_LIGHTNING): soft, scrolling
rearmed/dsTypeBeam
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/types/beam.tga
		blendFunc GL_ONE GL_ONE
		tcMod scroll -1.6 0
		rgbGen vertex
	}
}

// juggernaut gunmetal / mimic rim / kamikaze heat: an env-map shell tinted by the entity colour
rearmed/dsTypeShell
{
	deformVertexes wave 100 sin 0.6 0 0 0
	{
		map textures/effects/envmapdim.jpg
		blendFunc GL_ONE GL_ONE
		tcGen environment
		tcMod scroll 0.02 0.05
		rgbGen entity
	}
}

// the mimic: darkens the copy of your model to ink (multiplied over the skin)
rearmed/dsTypeDark
{
	deformVertexes wave 100 sin 0.25 0 0 0
	{
		map $whiteimage
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen const ( 0.22 0.22 0.27 )
	}
}

// the riot shield's energy face (flat fallback polygon)
rearmed/dsTypeShieldFace
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/types/shield_hex.tga
		blendFunc GL_ONE GL_ONE
		tcMod scroll 0 0.08
		rgbGen vertex
	}
}

// the riot shield's energy face on the model (models/rearmed/types/shield_face.md3)
models/rearmed/types/shield_face
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/rearmed/types/shield_hex.tga
		blendFunc GL_ONE GL_ONE
		tcMod scroll 0 0.08
		rgbGen entity
	}
}
