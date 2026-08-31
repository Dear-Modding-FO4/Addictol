// Separable Gaussian blur after Unrimp by Christian Ofenberg, MIT.

cbuffer WindowBuffer : register(b1)
{
	float4 WindowRect;    // xy = minimum; zw = maximum
	float4 WindowParams;  // x = radius; yz = screen size; w = fullscreen
};

cbuffer BlurBuffer : register(b0)
{
	float4 TexelSize;  // w = downsample factor
	int4 BlurParams;
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

static const float TWO_PI = 6.28318530718f;
static const int NUM_JITTER_SAMPLES = 4;
static const float CLIP_EPSILON = 0.001f;

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
};

VS_OUTPUT VS_Main(uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;
	output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
	output.Position = float4(output.TexCoord * 2.0f - 1.0f, 0.0f, 1.0f);
	output.Position.y = -output.Position.y;
	return output;
}

float2 Hash22(float2 p)
{
	float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
	p3 += dot(p3, p3.yzx + 33.33f);
	return frac((p3.xx + p3.yz) * p3.zy);
}

float4 SampleWithSoftening(float2 uv, float2 pixelPos, float2 texelSize)
{
	float2 noise = Hash22(pixelPos);

	static const float2 offsets[NUM_JITTER_SAMPLES] = {
		float2(-0.25f, -0.25f),
		float2(0.25f, -0.25f),
		float2(-0.25f, 0.25f),
		float2(0.25f, 0.25f)
	};

	float angle = noise.x * TWO_PI;
	float s, c;
	sincos(angle, s, c);
	float2x2 rotation = float2x2(c, -s, s, c);

	float4 result = 0;
	[unroll] for (int i = 0; i < NUM_JITTER_SAMPLES; i++)
	{
		float2 jitter = mul(rotation, offsets[i]) * texelSize;
		result += InputTexture.Sample(LinearSampler, uv + jitter);
	}

	return result / (float)NUM_JITTER_SAMPLES;
}

float RoundedRectSDF(float2 pixelPos, float2 rectMin, float2 rectMax, float radius)
{
	float2 rectCenter = (rectMin + rectMax) * 0.5f;
	float2 rectHalfSize = (rectMax - rectMin) * 0.5f;

	radius = min(radius, min(rectHalfSize.x, rectHalfSize.y));

	float2 p = abs(pixelPos - rectCenter) - rectHalfSize + radius;

	return length(max(p, 0.0f)) + min(max(p.x, p.y), 0.0f) - radius;
}

float4 PS_Main(VS_OUTPUT input) :
	SV_TARGET
{
	float2 pixelPos = input.TexCoord * float2(WindowParams.y, WindowParams.z);

	float2 rectMin = WindowRect.xy;
	float2 rectMax = WindowRect.zw;
	float cornerRadius = WindowParams.x;

	float alpha = 1.0f;
	if (WindowParams.w < 0.5f) {
		float sdf = RoundedRectSDF(pixelPos, rectMin, rectMax, cornerRadius);
		alpha = saturate(-sdf);

		if (alpha <= 0.0f) {
			discard;
		}
	}

	float2 blurTexelSize = TexelSize.w / float2(WindowParams.y, WindowParams.z);

	float4 blurColor = SampleWithSoftening(input.TexCoord, pixelPos, blurTexelSize);
	blurColor.a = alpha;

	return blurColor;
}

// Clears the HUD only where blur is drawn.
float4 PS_Clear(VS_OUTPUT input) :
	SV_TARGET
{
	float2 pixelPos = input.TexCoord * float2(WindowParams.y, WindowParams.z);
	float sdf = RoundedRectSDF(pixelPos, WindowRect.xy, WindowRect.zw, WindowParams.x);

	clip(-sdf - CLIP_EPSILON);

	return float4(0.0f, 0.0f, 0.0f, 0.0f);
}
