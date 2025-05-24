
struct VertexConstants
{
	float4 rpLocalLightOrigin;
	float4 rpLocalViewOrigin;
	float4 rpLightProjectionS;
	float4 rpLightProjectionT;
	float4 rpLightProjectionQ;
	float4 rpLightFalloffS;
	float4 rpBumpMatrixS;
	float4 rpBumpMatrixT;
	float4 rpDiffuseMatrixS;
	float4 rpDiffuseMatrixT;
	float4 rpSpecularMatrixS;
	float4 rpSpecularMatrixT;
	float4 rpVertexColorModulate;
	float4 rpVertexColorAdd;
	float4 rpMVPmatrixX;
	float4 rpMVPmatrixY;
	float4 rpMVPmatrixZ;
	float4 rpMVPmatrixW;
};

[[vk::binding(0)]]
ConstantBuffer<VertexConstants> vsConsts;

struct PixelConstants
{
	float4 rpDiffuseModifier;
	float4 rpSpecularModifier;
};

[[vk::binding(1)]]
ConstantBuffer<PixelConstants> psConsts;

[[vk::combinedImageSampler]][[vk::binding(2)]] Texture2D<float4> bumpTexture;
[[vk::combinedImageSampler]][[vk::binding(3)]] Texture2D<float4> lightFalloffTexture;
[[vk::combinedImageSampler]][[vk::binding(4)]] Texture2D<float4> lightProjTexture;
[[vk::combinedImageSampler]][[vk::binding(5)]] Texture2D<float4> diffuseTexture;
[[vk::combinedImageSampler]][[vk::binding(6)]] Texture2D<float4> specularTexture;

[[vk::combinedImageSampler]][[vk::binding(2)]] SamplerState bumpSampler;
[[vk::combinedImageSampler]][[vk::binding(3)]] SamplerState lightFalloffSampler;
[[vk::combinedImageSampler]][[vk::binding(4)]] SamplerState lightProjSampler;
[[vk::combinedImageSampler]][[vk::binding(5)]] SamplerState diffuseSampler;
[[vk::combinedImageSampler]][[vk::binding(6)]] SamplerState specularSampler;

struct VsInput
{
    [[vk::location(0)]] float3 position : POSITION;
	[[vk::location(1)]] float2 texcoord : TEXCOORD0;
	[[vk::location(2)]] float3 normal : NORMAL;
	[[vk::location(3)]] float3 tangent : TANGENT;
	[[vk::location(4)]] float3 bitangent : BITANGENT;
    [[vk::location(5)]] float4 color : COLOR;
};

struct VsPsInterp
{
    float4 position : SV_Position;
    float4 texcoord0 : Texcoord0;
	float4 texcoord1 : Texcoord1;
	float4 texcoord2 : Texcoord2;
	float4 texcoord3 : Texcoord3;
	float4 texcoord4 : Texcoord4;
	float4 texcoord5 : Texcoord5;
	float4 texcoord6 : Texcoord6;
	float4 color : Color;
};

VsPsInterp MainVs(VsInput input)
{
    VsPsInterp output = (VsPsInterp)0;
	
	float4x4 mvp = float4x4(vsConsts.rpMVPmatrixX, vsConsts.rpMVPmatrixY, vsConsts.rpMVPmatrixZ, vsConsts.rpMVPmatrixW);
	float4 position = float4(input.position, 1.0f);
	output.position = mul(position, mvp);
	
	const float4 kDefaultTexcoord = float4(0.0f, 0.5f, 0.0f, 1.0f);
	
	float3 toLight = vsConsts.rpLocalLightOrigin.xyz - input.position;
	output.texcoord0.x = dot(input.tangent, toLight);
	output.texcoord0.y = dot(input.bitangent, toLight);
	output.texcoord0.z = dot(input.normal, toLight);
	output.texcoord0.w = 1.0f;
	
	output.texcoord1 = kDefaultTexcoord;
	output.texcoord1.x = dot(float4(input.texcoord, 0.0f, 1.0f), vsConsts.rpBumpMatrixS);
	output.texcoord1.y = dot(float4(input.texcoord, 0.0f, 1.0f), vsConsts.rpBumpMatrixT);
	
	output.texcoord2 = kDefaultTexcoord;
	output.texcoord2.x = dot(position, vsConsts.rpLightFalloffS);
	
	output.texcoord3.x = dot(position, vsConsts.rpLightProjectionS);
	output.texcoord3.y = dot(position, vsConsts.rpLightProjectionT);
	output.texcoord3.z = 0.0f;
	output.texcoord3.w = dot(position, vsConsts.rpLightProjectionQ);
	
	output.texcoord4 = kDefaultTexcoord;
	output.texcoord4.x = dot(float4(input.texcoord, 0.0f, 1.0f), vsConsts.rpDiffuseMatrixS);
	output.texcoord4.y = dot(float4(input.texcoord, 0.0f, 1.0f), vsConsts.rpDiffuseMatrixT);
	
	output.texcoord5 = kDefaultTexcoord;
	output.texcoord5.x = dot(float4(input.texcoord, 0.0f, 1.0f), vsConsts.rpSpecularMatrixS);
	output.texcoord5.y = dot(float4(input.texcoord, 0.0f, 1.0f), vsConsts.rpSpecularMatrixT);
	
	toLight = normalize(toLight);
	float3 toView = normalize(vsConsts.rpLocalViewOrigin.xyz - input.position);
	float3 halfAngleVec = toLight + toView;
	output.texcoord6.x = dot(input.tangent, halfAngleVec);
	output.texcoord6.y = dot(input.bitangent, halfAngleVec);
	output.texcoord6.z = dot(input.normal, halfAngleVec);
	output.texcoord6.w = 1.0f;
	
	output.color = (input.color * vsConsts.rpVertexColorModulate) + vsConsts.rpVertexColorAdd;

    return output;
}

float4 MainPs(VsPsInterp input) : SV_Target0
{
	float4 bumpMap = bumpTexture.Sample(bumpSampler, input.texcoord1.xy);
	float4 diffuseMap = diffuseTexture.Sample(diffuseSampler, input.texcoord4.xy);
	
	//return float4(diffuseMap.xyz, 1.0f);
	
	
	float3 localNormal;
	localNormal.xy = bumpMap.wy - 0.5f;
	localNormal.z = sqrt(abs(dot(localNormal.xy, localNormal.xy) - 0.25f));
	localNormal = normalize(localNormal);
	
	float hdotn = dot(normalize(input.texcoord6.xyz), localNormal);
	float specPow = pow(hdotn, 10.0f);
	float4 specMap = specularTexture.Sample(specularSampler, input.texcoord5.xy);
	float3 diffuseColor = diffuseMap.xyz * psConsts.rpDiffuseModifier.xyz;
	float3 specularColor = specMap.xyz * psConsts.rpSpecularModifier.xyz * specPow;
	
	float3 lightVec = normalize(input.texcoord0.xyz);
	float4 lightFalloff = lightFalloffTexture.Sample(lightFalloffSampler, input.texcoord2.xy / input.texcoord2.w);
	float4 lightProj = lightProjTexture.Sample(lightProjSampler, input.texcoord3.xy / input.texcoord3.w);
	float3 lightColor = dot(lightVec, localNormal) * lightProj.xyz * lightFalloff.xyz;
	float3 color = (diffuseColor + specularColor) * lightColor * input.color.xyz;
	return float4(color, 1.0f);
	
}