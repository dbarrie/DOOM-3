
struct Constants
{
	float4 rpMVPmatrixX;
	float4 rpMVPmatrixY;
	float4 rpMVPmatrixZ;
	float4 rpMVPmatrixW;
};

[[vk::binding(0)]]
ConstantBuffer<Constants> consts;

struct VsInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

struct VsPsInterp
{
    float4 position : SV_Position;
};

VsPsInterp MainVs(VsInput input)
{
    VsPsInterp output = (VsPsInterp)0;
	
	float4x4 mvp = float4x4(consts.rpMVPmatrixX, consts.rpMVPmatrixY, consts.rpMVPmatrixZ, consts.rpMVPmatrixW);
	output.position = mul(float4(input.position, 1.0f), mvp);

    return output;
}
