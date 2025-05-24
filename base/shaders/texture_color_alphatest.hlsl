
struct VertexConstants
{
	float4 rpMVPmatrixX;
	float4 rpMVPmatrixY;
	float4 rpMVPmatrixZ;
	float4 rpMVPmatrixW;
	float4 rpVertexColorModulate;
	float4 rpVertexColorAdd;
	float4 rpColor;
	float4 rpTextureMatrixS;
	float4 rpTextureMatrixT;
};

[[vk::binding(0)]] ConstantBuffer<VertexConstants> vsConsts;

struct PixelConstants
{
	float4 rpAlphaTest;
};

[[vk::binding(1)]] ConstantBuffer<PixelConstants> psConsts;

[[vk::combinedImageSampler]][[vk::binding(2)]] Texture2D<float4> tex;
[[vk::combinedImageSampler]][[vk::binding(2)]] SamplerState samp;


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
	
	float4x4 mvp = float4x4(vsConsts.rpMVPmatrixX, vsConsts.rpMVPmatrixY, vsConsts.rpMVPmatrixZ, vsConsts.rpMVPmatrixW);

	output.position = mul(float4(input.position, 1.0f), mvp);
	output.texcoord0.x = dot(float4(input.texcoord, 0.0f, 1.0f), vsConsts.rpTextureMatrixS);
	output.texcoord0.y = dot(float4(input.texcoord, 0.0f, 1.0f), vsConsts.rpTextureMatrixT);
		
	float4 vertexColor = (input.color * vsConsts.rpVertexColorModulate) + vsConsts.rpVertexColorAdd;
	output.color = vertexColor * vsConsts.rpColor;

    return output;
}

float4 MainPs(VsPsInterp input) : SV_Target0
{
	float4 color = (tex.Sample(samp, input.texcoord0.xy) * input.color);
	float alphaTest = color.a - psConsts.rpAlphaTest.x;
	if (alphaTest < 0.0f)
		discard;
	
	//color = float4(color.a, color.a, color.a, color.a);

	return color;
}