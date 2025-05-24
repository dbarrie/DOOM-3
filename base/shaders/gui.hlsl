
struct VertexConstants
{
	float4 rpMVPmatrixX;
	float4 rpMVPmatrixY;
	float4 rpMVPmatrixZ;
	float4 rpMVPmatrixW;
};

[[vk::binding(0)]]
ConstantBuffer<VertexConstants> vsConsts;


[[vk::combinedImageSampler]][[vk::binding(1)]]
Texture2D<float4> guiTexture;
[[vk::combinedImageSampler]][[vk::binding(1)]]
SamplerState guiSampler;


struct VsInput
{
    [[vk::location(0)]] float3 position : POSITION;
	[[vk::location(1)]] float2 texcoord : TEXCOORD0;
    [[vk::location(5)]] float4 color : COLOR;
    
	//[[vk::location(6)]] float4 color2 : COLOR2;
};

struct VsPsInterp
{
    float4 position : SV_Position;
    float4 color : Color;
    float2 texcoord0 : Texcoord0;
    //float4 texcoord1 : Texcoord1;
};

VsPsInterp MainVs(VsInput input)
{
    VsPsInterp output = (VsPsInterp)0;
	
	float4x4 mvp = float4x4(vsConsts.rpMVPmatrixX, vsConsts.rpMVPmatrixY, vsConsts.rpMVPmatrixZ, vsConsts.rpMVPmatrixW);

	output.position = mul(float4(input.position, 1.0f), mvp);
	output.texcoord0 = input.texcoord.xy;
	//output.texcoord1 = input.color2 * 2.0f - 1.0f;
	output.color = input.color;

    return output;
}

float4 MainPs(VsPsInterp input) : SV_Target0
{
	float4 color = (guiTexture.Sample(guiSampler, input.texcoord0.xy) * input.color)/* + input.texcoord1*/;
	color.xyz *= color.w;
	return color;
}