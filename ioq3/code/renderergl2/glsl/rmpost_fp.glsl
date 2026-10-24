uniform sampler2D u_TextureMap;

uniform vec4      u_Color;		// x: sharpen 0..1, y: vignette, z: grain, w: chroma (0 or 1)
uniform vec2      u_InvTexRes;	// 1 / texture size
uniform vec4      u_ViewInfo;	// xy: view origin in texcoords, zw: texcoords to 0..1 across the view
uniform float     u_Time;
varying vec2      var_TexCoords;

const float VIGNETTE_STRENGTH = 0.18;	// darkening at the far corners
const float GRAIN_AMPLITUDE   = 0.03;
const float CHROMA_PIXELS     = 1.5;	// red/blue offset at the far corners

const vec3  LUMINANCE_VECTOR = vec3(0.2125, 0.7154, 0.0721);

// hash without sine, stable at large pixel coordinates (Dave Hoskins)
float Hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

void main()
{
	vec4 src = texture2D(u_TextureMap, var_TexCoords);
	vec3 e = src.rgb;
	vec3 color = e;

	// view position, centred, in pixels, so it stays round at any aspect ratio
	vec2 viewPixels = 1.0 / (u_InvTexRes * u_ViewInfo.zw);
	vec2 uv = (var_TexCoords - u_ViewInfo.xy) * u_ViewInfo.zw;
	vec2 pos = (uv - 0.5) * viewPixels;
	// 0 at the centre, 1 at the far corners
	float radius = length(pos) / (0.5 * length(viewPixels));

	// contrast adaptive sharpening, 5 tap cross (AMD FidelityFX CAS)
	if (u_Color.x > 0.0)
	{
		vec3 b = texture2D(u_TextureMap, var_TexCoords + vec2( 0.0, -u_InvTexRes.y)).rgb;
		vec3 d = texture2D(u_TextureMap, var_TexCoords + vec2(-u_InvTexRes.x,  0.0)).rgb;
		vec3 f = texture2D(u_TextureMap, var_TexCoords + vec2( u_InvTexRes.x,  0.0)).rgb;
		vec3 h = texture2D(u_TextureMap, var_TexCoords + vec2( 0.0,  u_InvTexRes.y)).rgb;

		vec3 cb = clamp(b, 0.0, 1.0);
		vec3 cd = clamp(d, 0.0, 1.0);
		vec3 ce = clamp(e, 0.0, 1.0);
		vec3 cf = clamp(f, 0.0, 1.0);
		vec3 ch = clamp(h, 0.0, 1.0);

		vec3 mn = min(ce, min(min(cb, cd), min(cf, ch)));
		vec3 mx = max(ce, max(max(cb, cd), max(cf, ch)));

		// less sharpening where the local contrast is already high
		vec3 amp = clamp(min(mn, 1.0 - mx) / max(mx, vec3(0.0001)), 0.0, 1.0);
		amp = sqrt(amp);

		vec3 w = amp * (-0.2 * u_Color.x);
		color = (e + (b + d + f + h) * w) / (1.0 + 4.0 * w);
	}

	// chromatic aberration, pushes red out and blue in, grows with radius squared
	if (u_Color.w > 0.0)
	{
		vec2 offset = pos / (0.5 * length(viewPixels)) * radius * CHROMA_PIXELS * u_InvTexRes;
		float r = texture2D(u_TextureMap, var_TexCoords - offset).r;
		float bl = texture2D(u_TextureMap, var_TexCoords + offset).b;
		color.r += r - e.r;
		color.b += bl - e.b;
	}

	// vignette on the round radius, only the outer part of the view darkens
	if (u_Color.y > 0.0)
	{
		color *= 1.0 - VIGNETTE_STRENGTH * smoothstep(0.4, 1.0, radius);
	}

	// animated luminance grain
	if (u_Color.z > 0.0)
	{
		float noise = Hash12(gl_FragCoord.xy + u_Time * vec2(97.13, 61.71)) - 0.5;
		float luma = dot(color, LUMINANCE_VECTOR);
		color += vec3(noise * 2.0 * GRAIN_AMPLITUDE) * (1.0 - 0.5 * clamp(luma, 0.0, 1.0));
	}

	gl_FragColor = vec4(max(color, vec3(0.0)), src.a);
}
