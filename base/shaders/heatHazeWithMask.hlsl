
struct VertexConstants
{
	float4 rpMVPmatrixX;
	float4 rpMVPmatrixY;
	float4 rpMVPmatrixZ;
	float4 rpMVPmatrixW;
	float4 rpModelViewMatrixZ;
	float4 rpProjectionMatrixY;
	float4 rpProjectionMatrixW;
	float4 rpUser0; // rpTextureScroll
	float4 rpUser1; // rpDeformMagnitude
};

[[vk::binding(0)]]
ConstantBuffer<VertexConstants> vsConsts;

struct PixelConstants
{
	float4 rpWindowCoord;
};

[[vk::binding(1)]]
ConstantBuffer<PixelConstants> psConsts;

[[vk::combinedImageSampler]][[vk::binding(2)]] Texture2D<float4> Texture0;
[[vk::combinedImageSampler]][[vk::binding(2)]] SamplerState Sampler0;
[[vk::combinedImageSampler]][[vk::binding(3)]] Texture2D<float4> Texture1;
[[vk::combinedImageSampler]][[vk::binding(3)]] SamplerState Sampler1;
[[vk::combinedImageSampler]][[vk::binding(4)]] Texture2D<float4> Texture2;
[[vk::combinedImageSampler]][[vk::binding(4)]] SamplerState Sampler2;

struct VsInput
{
    [[vk::location(0)]] float3 position : POSITION;
	[[vk::location(1)]] float2 texcoord : TEXCOORD0;
};

struct VsPsInterp
{
    float4 position : SV_Position;
    float4 texcoord0 : Texcoord0;
	float4 texcoord1 : Texcoord1;
	float4 texcoord2 : Texcoord2;
};

VsPsInterp MainVs(VsInput input)
{
    VsPsInterp output = (VsPsInterp)0;
	
	float4x4 mvp = float4x4(vsConsts.rpMVPmatrixX, vsConsts.rpMVPmatrixY, vsConsts.rpMVPmatrixZ, vsConsts.rpMVPmatrixW);
	float4 position = float4(input.position, 1.0f);
	output.position = mul(position, mvp);
	
	output.texcoord0 = float4(input.texcoord.xy, 0, 0);
	output.texcoord1 = float4(input.texcoord.xy, 0, 0) + vsConsts.rpUser0;
	
	float4 vec = float4(0, 1, 0, 1);
	vec.z = dot(position, vsConsts.rpModelViewMatrixZ);
	float magicProjectionAdjust = -0.43f;
	float x = dot(vec, vsConsts.rpProjectionMatrixY) * magicProjectionAdjust;
	float w = dot(vec, vsConsts.rpProjectionMatrixW);
	w = max(w, 1.0f);
	x /= w;
	x = min(x, 0.02f);
	output.texcoord2 = x * vsConsts.rpUser1;

    return output;
}

float4 MainPs(VsPsInterp input) : SV_Target0
{
	float4 mask = Texture2.Sample(Sampler2, input.texcoord0.xy);
	mask.xy -= 0.01f;
	if (any(mask < 0.0f))
		discard;

	float4 bumpMap = Texture1.Sample(Sampler1, input.texcoord1.xy) * 2.0f - 1.0f;
	float2 localNormal = bumpMap.wy;
	
	float2 screenTexCoord = input.position.xy * psConsts.rpWindowCoord.xy;
	screenTexCoord += localNormal * input.texcoord2.xy;
	screenTexCoord = saturate(screenTexCoord);
	
	float4 color = Texture0.Sample(Sampler0, screenTexCoord);
	return color;
}