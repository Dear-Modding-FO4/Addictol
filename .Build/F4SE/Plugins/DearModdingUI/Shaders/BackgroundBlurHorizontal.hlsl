// Separable Gaussian blur after Unrimp by Christian Ofenberg, MIT.

cbuffer BlurBuffer : register(b0)
{
	float4 TexelSize;  // xy = inverse size; w = downsample factor
	int4 BlurParams;   // x = samples
};

SamplerState LinearSampler : register(s0);
Texture2D InputTexture : register(t0);

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

static const float WEIGHTS[8] = {
	0.1760327f,
	0.1658591f,
	0.1403215f,
	0.1069852f,
	0.0732894f,
	0.0451904f,
	0.0248657f,
	0.0122423f
};

float4 PS_Main(VS_OUTPUT input) :
	SV_TARGET
{
	const int samples = min(BlurParams.x, 15);
	const int halfSamples = samples >> 1;

	float weightSum = WEIGHTS[0];
	[unroll(7)] for (int j = 1; j <= halfSamples; ++j)
	{
		weightSum += 2.0f * WEIGHTS[min(j, 7)];
	}
	const float normalization = 1.0f / weightSum;

	float4 result = InputTexture.Sample(LinearSampler, input.TexCoord) * (WEIGHTS[0] * normalization);

	[unroll(7)] for (int i = 1; i <= halfSamples; ++i)
	{
		float weight = WEIGHTS[min(i, 7)] * normalization;
		float offset = i * TexelSize.x;

		result += InputTexture.Sample(LinearSampler, input.TexCoord + float2(offset, 0.0f)) * weight;
		result += InputTexture.Sample(LinearSampler, input.TexCoord - float2(offset, 0.0f)) * weight;
	}

	return result;
}
