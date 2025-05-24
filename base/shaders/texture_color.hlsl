
struct Constants
{
	float4 rpMVPmatrixX;
	float4 rpMVPmatrixY;
	float4 rpMVPmatrixZ;
	float4 rpMVPmatrixW;
	float4 rpAlphaTest;
	float4 rpVertexColorModulate;
	float4 rpVertexColorAdd;
	float4 rpColor;
};

[[vk::binding(0)]]
ConstantBuffer<Constants> consts;


[[vk::combinedImageSampler]][[vk::binding(1)]] Texture2D<float4> tex;
[[vk::combinedImageSampler]][[vk::binding(1)]] SamplerState samp;


struct VsInput
{
    [[vk::location(0)]] float3 position : POSITION;
	[[vk::location(1)]] float2 texcoord : TEXCOORD0;
    [[vk::location(5)]] float4 color : COLOR;
};

struct VsPsInterp
{
    float4 position : SV_Position;
    float4 color : Color;
    float2 texcoord0 : Texcoord0;
};

VsPsInterp MainVs(VsInput input)
{
    VsPsInterp output = (VsPsInterp)0;
	
	float4x4 mvp = float4x4(consts.rpMVPmatrixX, consts.rpMVPmatrixY, consts.rpMVPmatrixZ, consts.rpMVPmatrixW);

	output.position = mul(float4(input.position, 1.0f), mvp);
	output.texcoord0 = input.texcoord.xy;
	
	float4 vertexColor = (input.color * consts.rpVertexColorModulate) + consts.rpVertexColorAdd;
	output.color = vertexColor * consts.rpColor;

    return output;
}

float4 MainPs(VsPsInterp input) : SV_Target0
{
	float4 color = (tex.Sample(samp, input.texcoord0.xy) * input.color);
	//if (color.a - consts.rpAlphaTest.x < 0.0f)
	//	discard;

	return color;
}