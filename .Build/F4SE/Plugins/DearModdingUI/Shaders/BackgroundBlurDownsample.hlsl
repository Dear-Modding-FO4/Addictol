// Separable Gaussian blur after Unrimp by Christian Ofenberg, MIT.

cbuffer BlurBuffer : register(b0)
{
	float4 TexelSize;  // xy = inverse size; w = downsample factor
	int4 BlurParams;
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

float4 PS_Main(VS_OUTPUT input) : SV_TARGET
{
	const int tapsPerAxis = max((int)(TexelSize.w * 0.5f), 1);
	const int2 outputPixel = (int2)input.Position.xy;
	// Fixed blocks avoid edge stretching.
	const float2 blockCenter = (outputPixel * TexelSize.w + TexelSize.w * 0.5f) * TexelSize.xy;
	float4 result = 0.0f;

	[loop] for (int y = 0; y < tapsPerAxis; ++y)
	{
		const float offsetY = (2.0f * y - (tapsPerAxis - 1)) * TexelSize.y;
		[loop] for (int x = 0; x < tapsPerAxis; ++x)
		{
			const float offsetX = (2.0f * x - (tapsPerAxis - 1)) * TexelSize.x;
			result += InputTexture.Sample(LinearSampler, blockCenter + float2(offsetX, offsetY));
		}
	}

	return result / (float)(tapsPerAxis * tapsPerAxis);
}
