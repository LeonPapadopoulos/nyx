#version 450

// One pass of the blur chain (VulkanBlur.cpp, Design/ui-painter.md, stage 3, change 1). An image
// is blurred by halving it a few times, each step averaging around every pixel, then blurring
// the small result with a Gaussian along x and along y. Small images make wide blurs cheap.
//
// The source is sampled with bilinear filtering, so one tap between texels averages four.

layout(set = 0, binding = 0) uniform sampler2D Source;

// VulkanBlur.cpp, BlurPushConstants
layout(push_constant) uniform BlurPushConstants
{
	// 1 / the source's and the target's size in pixels
	vec2 SourceTexelSize;
	vec2 TargetTexelSize;
	// The Gaussian's direction, (1, 0) or (0, 1); unused when downsampling
	vec2 Direction;
	uint Mode;
	uint PushUnused;
};

const uint ModeDownsample = 0u;
const uint ModeGaussian = 1u;

layout(location = 0) out vec4 outColor;

void main()
{
	// Where this target pixel's centre lies on the source, from 0 to 1
	vec2 uv = gl_FragCoord.xy * TargetTexelSize;

	if (Mode == ModeDownsample)
	{
		// The "dual filter" downsample: the centre, and four taps a source texel away
		// diagonally, each the average of four texels. 16 texels around the pixel, the nearer
		// ones weighed more.
		vec2 offset = SourceTexelSize;
		vec4 sum = texture(Source, uv) * 4.0;
		sum += texture(Source, uv + vec2(-offset.x, -offset.y));
		sum += texture(Source, uv + vec2( offset.x, -offset.y));
		sum += texture(Source, uv + vec2(-offset.x,  offset.y));
		sum += texture(Source, uv + vec2( offset.x,  offset.y));
		outColor = sum / 8.0;
		return;
	}

	// A 9-tap Gaussian in 5 taps: each tap off the centre lies between two texels, at the place
	// that weighs them as the Gaussian does.
	vec2 step = Direction * SourceTexelSize;
	vec4 sum = texture(Source, uv) * 0.2270270270;
	sum += (texture(Source, uv + step * 1.3846153846) + texture(Source, uv - step * 1.3846153846)) * 0.3162162162;
	sum += (texture(Source, uv + step * 3.2307692308) + texture(Source, uv - step * 3.2307692308)) * 0.0702702703;
	outColor = sum;
}
