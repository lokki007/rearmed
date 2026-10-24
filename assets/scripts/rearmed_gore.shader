// REARMED v4 gore / FX shaders (cgame/cg_gore.c). Textures: tools/make_textures_gore.py, splat/screen/drip: Blender worker.

rearmed/gore/drop
{
	nopicmip
	cull none
	{
		clampmap gfx/rearmed/gore/drop.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/gore/mist
{
	nopicmip
	cull none
	{
		clampmap gfx/rearmed/gore/mist.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/fx/fireball1
{
	nopicmip
	cull none
	{
		clampmap gfx/rearmed/fx/fireball1.tga
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/fx/fireball2
{
	nopicmip
	cull none
	{
		clampmap gfx/rearmed/fx/fireball2.tga
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/fx/smoke
{
	nopicmip
	cull none
	{
		clampmap gfx/rearmed/fx/smoke.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/gore/spatter
{
	nopicmip
	polygonOffset
	cull none
	{
		clampmap gfx/rearmed/gore/spatter.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/gore/streak
{
	nopicmip
	polygonOffset
	cull none
	{
		clampmap gfx/rearmed/gore/streak.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/gore/footprint
{
	nopicmip
	polygonOffset
	cull none
	{
		clampmap gfx/rearmed/gore/footprint.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}






rearmed/gore/visor
{
	nopicmip
	nomipmaps
	{
		clampmap gfx/rearmed/gore/visor.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}




rearmed/gore/vignette
{
	nopicmip
	nomipmaps
	{
		clampmap gfx/rearmed/gore/vignette.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/gore/speed1
{
	nopicmip
	nomipmaps
	{
		clampmap gfx/rearmed/gore/speed1.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/gore/speed2
{
	nopicmip
	nomipmaps
	{
		clampmap gfx/rearmed/gore/speed2.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

rearmed/gore/charred
{
	nopicmip
	{
		map gfx/rearmed/gore/charred.tga
		blendFunc GL_DST_COLOR GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}

rearmed/gore/embers
{
	nopicmip
	{
		map gfx/rearmed/gore/embers.tga
		blendFunc GL_ONE GL_ONE
		rgbGen entity
		tcMod scroll 0.02 0.05
	}
}
