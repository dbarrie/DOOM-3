
struct VertexConstants
{
	float4 rpMVPmatrixX;
	float4 rpMVPmatrixY;
	float4 rpMVPmatrixZ;
	float4 rpMVPmatrixW;
	float4 rpLocalLightOrigin;
};

[[vk::binding(0)]]
ConstantBuffer<VertexConstants> vsConsts;

struct PixelConstants
{
	float4 rpColor;
};

[[vk::binding(1)]]
ConstantBuffer<PixelConstants> psConsts;

struct VsInput
{
    [[vk::location(0)]] float4 position : POSITION;
};

struct VsPsInterp
{
    float4 position : SV_Position;
};

VsPsInterp MainVs(VsInput input)
{
    VsPsInterp output = (VsPsInterp)0;
	
	float4x4 mvp = float4x4(vsConsts.rpMVPmatrixX, vsConsts.rpMVPmatrixY, vsConsts.rpMVPmatrixZ, vsConsts.rpMVPmatrixW);
	float4 vPos = input.position - vsConsts.rpLocalLightOrigin;
	vPos = (vsConsts.rpLocalLightOrigin * input.position.w) + vPos;
	output.position = mul(vPos, mvp);

    return output;
}

float4 MainPs(VsPsInterp input) : SV_Target0
{
	return psConsts.rpColor;
}