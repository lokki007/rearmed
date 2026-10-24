// REARMED round 4 hit feel (en-feel), written by tools/make_hitfeel_assets.py. Do not edit by hand.

// armour cracks shell (cg_hitfeel.c CG_HF_BodyShell): dark fissures multiplied over the body
rearmed/hitfeel/crack1
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crack1.tga
		tcMod scale 2.5 2.5
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}
rearmed/hitfeel/crack2
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crack2.tga
		tcMod scale 2.5 2.5
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}
rearmed/hitfeel/crack3
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crack3.tga
		tcMod scale 2.5 2.5
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}
// the hot rim along the cracks (additive, pulsing via the entity colour)
rearmed/hitfeel/crackglow1
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crackglow1.tga
		tcMod scale 2.5 2.5
		blendFunc GL_ONE GL_ONE
		rgbGen entity
	}
}
rearmed/hitfeel/crackglow2
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crackglow2.tga
		tcMod scale 2.5 2.5
		blendFunc GL_ONE GL_ONE
		rgbGen entity
	}
}
rearmed/hitfeel/crackglow3
{
	nopicmip
	{
		map gfx/rearmed/hitfeel/crackglow3.tga
		tcMod scale 2.5 2.5
		blendFunc GL_ONE GL_ONE
		rgbGen entity
	}
}

// arm-less skins point the lost arm's surface here: no stages, nothing is drawn
rearmed/hitfeel/nodraw
{
	surfaceparm nodraw
	surfaceparm nomarks
	surfaceparm nonsolid
}

// limb / helmet gibs (tools/blender/make_hitfeel.py): flesh with a wet sheen, the helmet is metal
models/rearmed/gibs/arm
{
	{
		map models/rearmed/gibs/arm.tga
		rgbGen lightingDiffuse
	}
	{
		map models/rearmed/gibs/wetenv_dim.tga
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen lightingDiffuse
	}
}
models/rearmed/gibs/leg
{
	{
		map models/rearmed/gibs/leg.tga
		rgbGen lightingDiffuse
	}
	{
		map models/rearmed/gibs/wetenv_dim.tga
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen lightingDiffuse
	}
}
models/rearmed/gibs/helmet
{
	{
		map models/rearmed/gibs/helmet.tga
		rgbGen lightingDiffuse
	}
	{
		map models/rearmed/gibs/wetenv.tga
		blendFunc GL_ONE GL_ONE
		tcGen environment
		rgbGen lightingDiffuse
	}
}
