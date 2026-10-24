"""Bloom post-process for the GL2 renderer (r_bloom, r_bloomThreshold, r_bloomIntensity)."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from patch import sub

R = pathlib.Path(__file__).resolve().parents[2] / 'ioq3' / 'code' / 'renderergl2'
r = lambda f: str(R / f)

sub(r('tr_local.h'), '\tshaderProgram_t down4xShader;\n', '\tshaderProgram_t down4xShader;\n\tshaderProgram_t bloomShader;\n')
sub(r('tr_local.h'), 'extern  cvar_t  *r_ssao;\n', 'extern  cvar_t  *r_ssao;\nextern  cvar_t  *r_bloom;\nextern  cvar_t  *r_bloomThreshold;\nextern  cvar_t  *r_bloomKnee;\nextern  cvar_t  *r_bloomIntensity;\nextern  cvar_t  *r_bloomPasses;\n')

sub(r('tr_init.c'), 'cvar_t  *r_ssao;\n', 'cvar_t  *r_ssao;\ncvar_t  *r_bloom;\ncvar_t  *r_bloomThreshold;\ncvar_t  *r_bloomKnee;\ncvar_t  *r_bloomIntensity;\ncvar_t  *r_bloomPasses;\n')
sub(r('tr_init.c'), '\tr_ssao = ri.Cvar_Get( "r_ssao", "0", CVAR_LATCH | CVAR_ARCHIVE );\n',
    '\tr_ssao = ri.Cvar_Get( "r_ssao", "0", CVAR_LATCH | CVAR_ARCHIVE );\n'
    '\tr_bloom = ri.Cvar_Get( "r_bloom", "1", CVAR_ARCHIVE );\n'
    '\tr_bloomThreshold = ri.Cvar_Get( "r_bloomThreshold", "0.62", CVAR_ARCHIVE );\n'
    '\tr_bloomKnee = ri.Cvar_Get( "r_bloomKnee", "0.25", CVAR_ARCHIVE );\n'
    '\tr_bloomIntensity = ri.Cvar_Get( "r_bloomIntensity", "0.7", CVAR_ARCHIVE );\n'
    '\tr_bloomPasses = ri.Cvar_Get( "r_bloomPasses", "4", CVAR_ARCHIVE );\n')

sub(r('tr_glsl.c'), 'extern const char *fallbackShader_down4x_fp;\n',
    'extern const char *fallbackShader_down4x_fp;\nextern const char *fallbackShader_bloom_vp;\nextern const char *fallbackShader_bloom_fp;\n')
sub(r('tr_glsl.c'), '''	GLSL_FinishGPUShader(&tr.down4xShader);
''', '''	GLSL_FinishGPUShader(&tr.down4xShader);

	numEtcShaders++;


	attribs = ATTR_POSITION | ATTR_TEXCOORD;
	extradefines[0] = '\\0';

	if (!GLSL_InitGPUShader(&tr.bloomShader, "bloom", attribs, qtrue, extradefines, qtrue, fallbackShader_bloom_vp, fallbackShader_bloom_fp))
	{
		ri.Error(ERR_FATAL, "Could not load bloom shader!");
	}

	GLSL_InitUniforms(&tr.bloomShader);

	GLSL_SetUniformInt(&tr.bloomShader, UNIFORM_TEXTUREMAP, TB_DIFFUSEMAP);

	GLSL_FinishGPUShader(&tr.bloomShader);
''')
sub(r('tr_glsl.c'), '\tGLSL_DeleteGPUShader(&tr.down4xShader);\n', '\tGLSL_DeleteGPUShader(&tr.down4xShader);\n\tGLSL_DeleteGPUShader(&tr.bloomShader);\n')

sub(r('tr_postprocess.h'), 'void RB_GaussianBlur(FBO_t *srcFbo, FBO_t *dstFbo, float blur);\n',
    'void RB_GaussianBlur(FBO_t *srcFbo, FBO_t *dstFbo, float blur);\nvoid RB_Bloom(FBO_t *srcFbo, ivec4_t srcBox);\n')

sub(r('tr_postprocess.c'), '''void RB_GaussianBlur(FBO_t *srcFbo, FBO_t *dstFbo, float blur)
{''', '''/*
=============
RB_Bloom

Bright pass into the half resolution buffers, widening separable blurs,
then added back on top of the (tone mapped) frame
=============
*/
void RB_Bloom(FBO_t *srcFbo, ivec4_t srcBox)
{
	vec4_t color;
	vec2_t texScale;
	ivec4_t halfBox;
	int i, passes;

	if (!tr.quarterFbo[0] || !tr.quarterFbo[1] || r_bloomIntensity->value <= 0.0f)
		return;

	// bright pass + 2x downsample
	VectorSet4(color, r_bloomThreshold->value, r_bloomKnee->value, 1.0f, 1.0f);
	VectorSet2(texScale, 1.0f, 1.0f);
	VectorSet4(halfBox, 0, 0, tr.quarterFbo[0]->width, tr.quarterFbo[0]->height);
	FBO_Blit(srcFbo, srcBox, texScale, tr.quarterFbo[0], halfBox, &tr.bloomShader, color, GLS_DEPTHTEST_DISABLE);

	// each pass doubles the blur radius for a wide, smooth falloff
	passes = r_bloomPasses->integer;
	if (passes < 1)
		passes = 1;
	else if (passes > 6)
		passes = 6;
	for (i = 0; i < passes; i++)
	{
		float strength = (float)(1 << i);
		RB_HBlur(tr.quarterFbo[0], tr.quarterFbo[1], strength);
		RB_VBlur(tr.quarterFbo[1], tr.quarterFbo[0], strength);
	}

	// add it back
	VectorSet4(color, r_bloomIntensity->value, r_bloomIntensity->value, r_bloomIntensity->value, 1.0f);
	FBO_Blit(tr.quarterFbo[0], NULL, NULL, srcFbo, srcBox, NULL, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHTEST_DISABLE);
}

void RB_GaussianBlur(FBO_t *srcFbo, FBO_t *dstFbo, float blur)
{''')

sub(r('tr_backend.c'), '''	if (r_drawSunRays->integer)
		RB_SunRays(srcFbo, srcBox, srcFbo, srcBox);
''', '''	if (r_bloom->integer && srcFbo)
		RB_Bloom(srcFbo, srcBox);

	if (r_drawSunRays->integer)
		RB_SunRays(srcFbo, srcBox, srcFbo, srcBox);
''')
print('bloom ok')
