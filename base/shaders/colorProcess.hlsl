
struct VertexConstants
{
	float4 rpMVPmatrixX;
	float4 rpMVPmatrixY;
	float4 rpMVPmatrixZ;
	float4 rpMVPmatrixW;
	float4 rpUser0;
	float4 rpUser1;
};

[[vk::binding(0)]]
ConstantBuffer<VertexConstants> vsConsts;

[[vk::combinedImageSampler]][[vk::binding(1)]] Texture2D<float4> Texture;
[[vk::combinedImageSampler]][[vk::binding(1)]] SamplerState Sampler;

struct VsInput
{
    [[vk::location(0)]] float3 position : POSITION;
	[[vk::location(1)]] float2 texcoord : TEXCOORD0;
};

struct VsPsInterp
{
    float4 position : SV_Position;
    float4 color : Color;
    float3 texcoord0 : Texcoord0;
};

VsPsInterp MainVs(VsInput input)
{
    VsPsInterp output = (VsPsInterp)0;
	
	float4x4 mvp = float4x4(vsConsts.rpMVPmatrixX, vsConsts.rpMVPmatrixY, vsConsts.rpMVPmatrixZ, vsConsts.rpMVPmatrixW);

	output.position = mul(float4(input.position, 1.0f), mvp);
	output.texcoord0.x = input.texcoord.x;
	output.texcoord0.y = 1.0f - input.texcoord.y;
	output.texcoord0.z = vsConsts.rpUser0.x;
	
	output.color = vsConsts.rpUser1;

    return output;
}

float4 MainPs(VsPsInterp input) : SV_Target0
{
	float4 src = Texture.Sample(Sampler, input.texcoord0.xy);
	float4 targ = input.color * dot(float3(0.333, 0.333, 0.333), src.xyz);
	float4 color = lerp(src, target, input.texcoord0.z);
	return color;
}