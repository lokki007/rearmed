uniform sampler2D u_TextureMap;

uniform vec4      u_Color;		// x: threshold, y: soft knee, z: output scale
uniform vec2      u_InvTexRes;
varying vec2      var_TexCoords;

// 2x2 box downsample followed by a soft-knee bright pass
void main()
{
	vec3 color;

	color  = texture2D(u_TextureMap, var_TexCoords + u_InvTexRes * vec2(-0.5, -0.5)).rgb;
	color += texture2D(u_TextureMap, var_TexCoords + u_InvTexRes * vec2( 0.5, -0.5)).rgb;
	color += texture2D(u_TextureMap, var_TexCoords + u_InvTexRes * vec2(-0.5,  0.5)).rgb;
	color += texture2D(u_TextureMap, var_TexCoords + u_InvTexRes * vec2( 0.5,  0.5)).rgb;
	color *= 0.25;

	float brightness = max(color.r, max(color.g, color.b));
	float knee = max(u_Color.y, 0.0001);
	float soft = clamp(brightness - u_Color.x + knee, 0.0, 2.0 * knee);
	soft = soft * soft / (4.0 * knee);
	float contribution = max(soft, brightness - u_Color.x) / max(brightness, 0.0001);

	gl_FragColor = vec4(color * contribution * u_Color.z, 1.0);
}
