/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 
Copyright (C) 2016-2017 Dustin Land

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#pragma hdrstop
#include "../idlib/precompiled.h"
//#include "GLState.h"
#include "RenderProgs.h"
#include "RenderBackend.h"
#include "BufferObject.h"

#include "tr_local.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Unknwn.h>
#include <dxcapi.h>
#include <wrl.h>

using namespace Microsoft::WRL;

#include <spirv_reflect.h>


idRenderProgManager renderProgManager;

struct vertexLayout_t {
	idList< FglVertexInputBindingDescription > bindingDesc;
	idList< FglVertexInputAttributeDescription > attributeDesc;
};

static vertexLayout_t vertexLayouts[NUM_VERTEX_LAYOUTS];

static shader_t defaultShader;
static idUniformBuffer emptyUBO;

static const char* renderProgBindingStrings[BINDING_TYPE_MAX] = {
	"ubo",
	"sampler"
};


// For GLSL we need to have the names for the renderparms so we can look up 
// their run time indices within the renderprograms
const char* GLSLParmNames[RENDERPARM_TOTAL] = {
	"rpScreenCorrectionFactor",
	"rpWindowCoord",
	"rpDiffuseModifier",
	"rpSpecularModifier",

	"rpLocalLightOrigin",
	"rpLocalViewOrigin",

	"rpLightProjectionS",
	"rpLightProjectionT",
	"rpLightProjectionQ",
	"rpLightFalloffS",

	"rpBumpMatrixS",
	"rpBumpMatrixT",

	"rpDiffuseMatrixS",
	"rpDiffuseMatrixT",

	"rpSpecularMatrixS",
	"rpSpecularMatrixT",

	"rpVertexColorModulate",
	"rpVertexColorAdd",

	"rpColor",
	"rpViewOrigin",
	"rpGlobalEyePos",

	"rpMVPmatrixX",
	"rpMVPmatrixY",
	"rpMVPmatrixZ",
	"rpMVPmatrixW",

	"rpModelMatrixX",
	"rpModelMatrixY",
	"rpModelMatrixZ",
	"rpModelMatrixW",

	"rpProjectionMatrixX",
	"rpProjectionMatrixY",
	"rpProjectionMatrixZ",
	"rpProjectionMatrixW",

	"rpModelViewMatrixX",
	"rpModelViewMatrixY",
	"rpModelViewMatrixZ",
	"rpModelViewMatrixW",

	"rpTextureMatrixS",
	"rpTextureMatrixT",

	"rpTexGen0S",
	"rpTexGen0T",
	"rpTexGen0Q",
	"rpTexGen0Enabled",

	"rpTexGen1S",
	"rpTexGen1T",
	"rpTexGen1Q",
	"rpTexGen1Enabled",

	"rpWobbleSkyX",
	"rpWobbleSkyY",
	"rpWobbleSkyZ",

	"rpOverbright",
	"rpEnableSkinning",
	"rpAlphaTest",

	"rpUser0",
	"rpUser1",
	"rpUser2",
	"rpUser3",
	"rpUser4",
	"rpUser5",
	"rpUser6",
	"rpUser7"
};

/*
=============
CreateVertexDescriptions
=============
*/
static void CreateVertexDescriptions()
{
	FglVertexInputBindingDescription binding{};
	FglVertexInputAttributeDescription attribute{};

	{
		vertexLayout_t& layout = vertexLayouts[LAYOUT_DRAW_VERT];

		uint32_t locationNo = 0;

		binding.binding = 0;
		binding.stride = sizeof(idDrawVert);
		layout.bindingDesc.Append(binding);

		// Position
		attribute.format = FGL_FORMAT_R32G32B32_FLOAT;
		attribute.location = locationNo++;
		attribute.offset = offsetof(idDrawVert, xyz);
		layout.attributeDesc.Append(attribute);

		// TexCoord
		attribute.format = FGL_FORMAT_R32G32_FLOAT;
		attribute.location = locationNo++;
		attribute.offset = offsetof(idDrawVert, st);
		layout.attributeDesc.Append(attribute);

		// Normal
		attribute.format = FGL_FORMAT_R32G32B32_FLOAT;
		attribute.location = locationNo++;
		attribute.offset = offsetof(idDrawVert, normal);
		layout.attributeDesc.Append(attribute);

		// Tangent
		attribute.format = FGL_FORMAT_R32G32B32_FLOAT;
		attribute.location = locationNo++;
		attribute.offset = offsetof(idDrawVert, tangents[0]);
		layout.attributeDesc.Append(attribute);

		// Tangent
		attribute.format = FGL_FORMAT_R32G32B32_FLOAT;
		attribute.location = locationNo++;
		attribute.offset = offsetof(idDrawVert, tangents[1]);
		layout.attributeDesc.Append(attribute);

		// Color1
		attribute.format = FGL_FORMAT_R8G8B8A8_UNORM;
		attribute.location = locationNo++;
		attribute.offset = offsetof(idDrawVert, color);
		layout.attributeDesc.Append(attribute);

		// Color2
		//attribute.format = FGL_FORMAT_R8G8B8A8_UNORM;
		//attribute.location = locationNo++;
		//attribute.offset = offset;
		//layout.attributeDesc.Append(attribute);
	}
	/*
	{
		vertexLayout_t& layout = vertexLayouts[LAYOUT_DRAW_SHADOW_VERT_SKINNED];

		uint32_t locationNo = 0;
		uint32_t offset = 0;

		binding.stride = sizeof(idShadowVertSkinned);
		layout.bindingDesc.Append(binding);

		// Position
		attribute.format = FGL_FORMAT_R32G32B32_FLOAT;
		attribute.location = locationNo++;
		attribute.offset = offset;
		layout.attributeDesc.Append(attribute);
		offset += sizeof(idShadowVertSkinned::xyzw);

		// Color1
		attribute.format = FGL_FORMAT_R8G8B8A8_UNORM;
		attribute.location = locationNo++;
		attribute.offset = offset;
		layout.attributeDesc.Append(attribute);
		offset += sizeof(idShadowVertSkinned::color);

		// Color2
		attribute.format = FGL_FORMAT_R8G8B8A8_UNORM;
		attribute.location = locationNo++;
		attribute.offset = offset;
		layout.attributeDesc.Append(attribute);
	}
	*/
	{
		vertexLayout_t& layout = vertexLayouts[LAYOUT_DRAW_SHADOW_VERT];

		binding.stride = sizeof(shadowCache_t);
		layout.bindingDesc.Append(binding);

		// Position
		attribute.format = FGL_FORMAT_R32G32B32A32_FLOAT;
		attribute.location = 0;
		attribute.offset = 0;
		layout.attributeDesc.Append(attribute);
	}
}

/*
========================
CreateDescriptorPools
========================
*/
static void CreateDescriptorPools(FglDescriptorPool(&pools)[NUM_FRAME_DATA])
{
	std::array<FglDescriptorPoolSize, 2> poolSizes{};
	poolSizes[0].type = FGL_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = MAX_DESC_UNIFORM_BUFFERS;
	poolSizes[1].type = FGL_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = MAX_DESC_IMAGE_SAMPLERS;

	FglDescriptorPoolCreateInfo poolCreateInfo = {};
	poolCreateInfo.maxSets = MAX_DESC_SETS;
	poolCreateInfo.poolSizeCount = (uint32_t)poolSizes.size();
	poolCreateInfo.pPoolSizes = poolSizes.data();

	for (int i = 0; i < NUM_FRAME_DATA; ++i) {
		ID_FGL_CHECK(fglCreateDescriptorPool(fglcontext.device, &poolCreateInfo, nullptr, &pools[i]));
	}
}

/*
========================
CreateDescriptorSetLayout
========================
*/
static void CreateDescriptorSetLayout(const shader_t& vertexShader, const shader_t& fragmentShader, renderProg_t& renderProg)
{
	auto GetDescriptorType = [](rpBinding_t type)
	{
		switch (type)
		{
		case BINDING_TYPE_UNIFORM_BUFFER: return FGL_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case BINDING_TYPE_SAMPLER: return FGL_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		default:
			common->Error("Unknown rpBinding_t %d", (int)type);
			return _FGL_DESCRIPTOR_TYPE_MAX_ENUM;
		}
	};


	// Descriptor Set Layout
	{
		idList< FglDescriptorSetLayoutBinding > layoutBindings;
		FglDescriptorSetLayoutBinding binding = {};
		binding.descriptorCount = 1;

		uint32_t bindingId = 0;

		binding.stageFlags = FGL_SHADER_STAGE_VERTEX_BIT;
		for (int i = 0; i < vertexShader.bindings.Num(); ++i)
		{
			binding.binding = bindingId++;
			binding.descriptorType = GetDescriptorType(vertexShader.bindings[i]);
			renderProg.bindings.Append(vertexShader.bindings[i]);

			layoutBindings.Append(binding);
		}

		binding.stageFlags = FGL_SHADER_STAGE_FRAGMENT_BIT;
		for (int i = 0; i < fragmentShader.bindings.Num(); ++i)
		{
			binding.binding = bindingId++;
			binding.descriptorType = GetDescriptorType(fragmentShader.bindings[i]);
			renderProg.bindings.Append(fragmentShader.bindings[i]);

			layoutBindings.Append(binding);
		}

		FglDescriptorSetLayoutCreateInfo createInfo{};
		createInfo.bindingCount = layoutBindings.Num();
		createInfo.pBindings = layoutBindings.Ptr();

		ID_FGL_CHECK(fglCreateDescriptorSetLayout(fglcontext.device, &createInfo, nullptr, &renderProg.descriptorSetLayout));
	}

	// Pipeline Layout
	{
		FglPipelineLayoutCreateInfo createInfo{};
		createInfo.setLayoutCount = 1;
		createInfo.pSetLayouts = &renderProg.descriptorSetLayout;

		ID_FGL_CHECK(fglCreatePipelineLayout(fglcontext.device, &createInfo, nullptr, &renderProg.pipelineLayout));
	}
}

/*
========================
GetStencilOpState
========================
*/
static FglStencilOpState GetStencilOpState(uint64_t stencilBits)
{
	FglStencilOpState state{};
	switch (stencilBits & GLS_STENCIL_OP_FAIL_BITS)
	{
	case GLS_STENCIL_OP_FAIL_KEEP:		state.failOp = FGL_STENCIL_OP_KEEP; break;
	case GLS_STENCIL_OP_FAIL_ZERO:		state.failOp = FGL_STENCIL_OP_ZERO; break;
	case GLS_STENCIL_OP_FAIL_REPLACE:	state.failOp = FGL_STENCIL_OP_REPLACE; break;
	case GLS_STENCIL_OP_FAIL_INCR:		state.failOp = FGL_STENCIL_OP_INCREMENT_AND_CLAMP; break;
	case GLS_STENCIL_OP_FAIL_DECR:		state.failOp = FGL_STENCIL_OP_DECREMENT_AND_CLAMP; break;
	case GLS_STENCIL_OP_FAIL_INVERT:	state.failOp = FGL_STENCIL_OP_INVERT; break;
	case GLS_STENCIL_OP_FAIL_INCR_WRAP: state.failOp = FGL_STENCIL_OP_INCREMENT_AND_WRAP; break;
	case GLS_STENCIL_OP_FAIL_DECR_WRAP: state.failOp = FGL_STENCIL_OP_DECREMENT_AND_WRAP; break;
	}
	switch (stencilBits & GLS_STENCIL_OP_ZFAIL_BITS)
	{
	case GLS_STENCIL_OP_ZFAIL_KEEP:		state.depthFailOp = FGL_STENCIL_OP_KEEP; break;
	case GLS_STENCIL_OP_ZFAIL_ZERO:		state.depthFailOp = FGL_STENCIL_OP_ZERO; break;
	case GLS_STENCIL_OP_ZFAIL_REPLACE:	state.depthFailOp = FGL_STENCIL_OP_REPLACE; break;
	case GLS_STENCIL_OP_ZFAIL_INCR:		state.depthFailOp = FGL_STENCIL_OP_INCREMENT_AND_CLAMP; break;
	case GLS_STENCIL_OP_ZFAIL_DECR:		state.depthFailOp = FGL_STENCIL_OP_DECREMENT_AND_CLAMP; break;
	case GLS_STENCIL_OP_ZFAIL_INVERT:	state.depthFailOp = FGL_STENCIL_OP_INVERT; break;
	case GLS_STENCIL_OP_ZFAIL_INCR_WRAP:state.depthFailOp = FGL_STENCIL_OP_INCREMENT_AND_WRAP; break;
	case GLS_STENCIL_OP_ZFAIL_DECR_WRAP:state.depthFailOp = FGL_STENCIL_OP_DECREMENT_AND_WRAP; break;
	}
	switch (stencilBits & GLS_STENCIL_OP_PASS_BITS)
	{
	case GLS_STENCIL_OP_PASS_KEEP:		state.passOp = FGL_STENCIL_OP_KEEP; break;
	case GLS_STENCIL_OP_PASS_ZERO:		state.passOp = FGL_STENCIL_OP_ZERO; break;
	case GLS_STENCIL_OP_PASS_REPLACE:	state.passOp = FGL_STENCIL_OP_REPLACE; break;
	case GLS_STENCIL_OP_PASS_INCR:		state.passOp = FGL_STENCIL_OP_INCREMENT_AND_CLAMP; break;
	case GLS_STENCIL_OP_PASS_DECR:		state.passOp = FGL_STENCIL_OP_DECREMENT_AND_CLAMP; break;
	case GLS_STENCIL_OP_PASS_INVERT:	state.passOp = FGL_STENCIL_OP_INVERT; break;
	case GLS_STENCIL_OP_PASS_INCR_WRAP:	state.passOp = FGL_STENCIL_OP_INCREMENT_AND_WRAP; break;
	case GLS_STENCIL_OP_PASS_DECR_WRAP:	state.passOp = FGL_STENCIL_OP_DECREMENT_AND_WRAP; break;
	}

	return state;
}

/*
========================
CreateGraphicsPipeline
========================
*/
static FglPipeline CreateGraphicsPipeline(vertexLayoutType_t vertexLayoutType, FglShaderModule vertexShader, FglShaderModule fragmentShader, FglPipelineLayout pipelineLayout, uint64_t stateBits)
{
	// Pipeline
	vertexLayout_t& vertexLayout = vertexLayouts[vertexLayoutType];

	// Vertex Input
	FglPipelineVertexInputStateCreateInfo vertexInputState{};
	vertexInputState.vertexBindingDescriptionCount = vertexLayout.bindingDesc.Num();
	vertexInputState.pVertexBindingDescriptions = vertexLayout.bindingDesc.Ptr();
	vertexInputState.vertexAttributeDescriptionCount = vertexLayout.attributeDesc.Num();
	vertexInputState.pVertexAttributeDescriptions = vertexLayout.attributeDesc.Ptr();

	// Input Assembly
	FglPipelineInputAssemblyStateCreateInfo assemblyInputState{};
	assemblyInputState.topology = FGL_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// Rasterization
	FglPipelineRasterizationStateCreateInfo rasterizationState{};
	//rasterizationState.depthBiasEnable = (stateBits & GLS_POLYGON_OFFSET) != 0;
	//rasterizationState.depthClampEnable = VK_FALSE;
	//rasterizationState.frontFace = (stateBits & GLS_CLOCKWISE) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationState.polygonMode = (stateBits & GLS_POLYMODE_LINE) ? FGL_POLYGON_MODE_LINE : FGL_POLYGON_MODE_FILL;

	// We don't have a way to set the front face at the moment, so we need to invert the culling
	switch (stateBits & GLS_CULL_BITS)
	{
	case GLS_CULL_TWOSIDED:
		rasterizationState.cullMode = FGL_CULL_MODE_NONE;
		break;
	case GLS_CULL_BACKSIDED:
		rasterizationState.cullMode = (stateBits & GLS_MIRROR_VIEW) ? FGL_CULL_MODE_BACK : FGL_CULL_MODE_FRONT; //(stateBits & GLS_MIRROR_VIEW) ? FGL_CULL_MODE_FRONT : FGL_CULL_MODE_BACK;
		break;
	case GLS_CULL_FRONTSIDED:
	default:
		rasterizationState.cullMode = (stateBits & GLS_MIRROR_VIEW) ? FGL_CULL_MODE_FRONT : FGL_CULL_MODE_BACK; //(stateBits & GLS_MIRROR_VIEW) ? FGL_CULL_MODE_BACK : FGL_CULL_MODE_FRONT;
		break;
	}

	// Color Blend Attachment
	FglPipelineColorBlendStateCreateInfo colorBlendState{};
	{
		FglBlendFactor srcFactor = FGL_BLEND_FACTOR_ONE;
		switch (stateBits & GLS_SRCBLEND_BITS) {
		case GLS_SRCBLEND_ZERO:					srcFactor = FGL_BLEND_FACTOR_ZERO; break;
		case GLS_SRCBLEND_ONE:					srcFactor = FGL_BLEND_FACTOR_ONE; break;
		case GLS_SRCBLEND_DST_COLOR:			srcFactor = FGL_BLEND_FACTOR_DST_COLOR; break;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:	srcFactor = FGL_BLEND_FACTOR_ONE_MINUS_DST_COLOR; break;
		case GLS_SRCBLEND_SRC_ALPHA:			srcFactor = FGL_BLEND_FACTOR_SRC_ALPHA; break;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:	srcFactor = FGL_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
		case GLS_SRCBLEND_DST_ALPHA:			srcFactor = FGL_BLEND_FACTOR_DST_ALPHA; break;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:	srcFactor = FGL_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; break;
		}

		FglBlendFactor dstFactor = FGL_BLEND_FACTOR_ZERO;
		switch (stateBits & GLS_DSTBLEND_BITS) {
		case GLS_DSTBLEND_ZERO:					dstFactor = FGL_BLEND_FACTOR_ZERO; break;
		case GLS_DSTBLEND_ONE:					dstFactor = FGL_BLEND_FACTOR_ONE; break;
		case GLS_DSTBLEND_SRC_COLOR:			dstFactor = FGL_BLEND_FACTOR_SRC_COLOR; break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:	dstFactor = FGL_BLEND_FACTOR_ONE_MINUS_SRC_COLOR; break;
		case GLS_DSTBLEND_SRC_ALPHA:			dstFactor = FGL_BLEND_FACTOR_SRC_ALPHA; break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:	dstFactor = FGL_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
		case GLS_DSTBLEND_DST_ALPHA:			dstFactor = FGL_BLEND_FACTOR_DST_ALPHA; break;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:	dstFactor = FGL_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; break;
		}

		/*
		FglBlendOp blendOp = VK_BLEND_OP_ADD;
		switch (stateBits & GLS_BLENDOP_BITS) {
		case GLS_BLENDOP_MIN: blendOp = VK_BLEND_OP_MIN; break;
		case GLS_BLENDOP_MAX: blendOp = VK_BLEND_OP_MAX; break;
		case GLS_BLENDOP_ADD: blendOp = VK_BLEND_OP_ADD; break;
		case GLS_BLENDOP_SUB: blendOp = VK_BLEND_OP_SUBTRACT; break;
		}
		*/

		colorBlendState.blendEnable = (srcFactor != FGL_BLEND_FACTOR_ONE || dstFactor != FGL_BLEND_FACTOR_ZERO);
		colorBlendState.srcBlendFactor = srcFactor;
		colorBlendState.dstBlendFactor = dstFactor;

		// Color Mask
		colorBlendState.colorWriteMask = 0;
		colorBlendState.colorWriteMask |= (stateBits & GLS_REDMASK)   ? 0 : FGL_COLOR_COMPONENT_R_BIT;
		colorBlendState.colorWriteMask |= (stateBits & GLS_GREENMASK) ? 0 : FGL_COLOR_COMPONENT_G_BIT;
		colorBlendState.colorWriteMask |= (stateBits & GLS_BLUEMASK)  ? 0 : FGL_COLOR_COMPONENT_B_BIT;
		colorBlendState.colorWriteMask |= (stateBits & GLS_ALPHAMASK) ? 0 : FGL_COLOR_COMPONENT_A_BIT;
	}

	// Depth / Stencil
	FglPipelineDepthStencilStateCreateInfo depthStencilState{};
	{
		FglCompareOp depthCompareOp = FGL_COMPARE_OP_ALWAYS;
		switch (stateBits & GLS_DEPTHFUNC_BITS)
		{
		case GLS_DEPTHFUNC_EQUAL:		depthCompareOp = FGL_COMPARE_OP_EQUAL; break;
		case GLS_DEPTHFUNC_ALWAYS:		depthCompareOp = FGL_COMPARE_OP_ALWAYS; break;
		case GLS_DEPTHFUNC_LESS:		depthCompareOp = FGL_COMPARE_OP_LESS_EQUAL; break;
		//case GLS_DEPTHFUNC_GREATER:		depthCompareOp = FGL_COMPARE_OP_GREATER_EQUAL; break;
		}

		FglCompareOp stencilCompareOp = FGL_COMPARE_OP_ALWAYS;
		switch (stateBits & GLS_STENCIL_FUNC_BITS) {
		case GLS_STENCIL_FUNC_NEVER:	stencilCompareOp = FGL_COMPARE_OP_NEVER; break;
		case GLS_STENCIL_FUNC_LESS:		stencilCompareOp = FGL_COMPARE_OP_LESS; break;
		case GLS_STENCIL_FUNC_EQUAL:	stencilCompareOp = FGL_COMPARE_OP_EQUAL; break;
		case GLS_STENCIL_FUNC_LEQUAL:	stencilCompareOp = FGL_COMPARE_OP_LESS_EQUAL; break;
		case GLS_STENCIL_FUNC_GREATER:	stencilCompareOp = FGL_COMPARE_OP_GREATER; break;
		case GLS_STENCIL_FUNC_NOTEQUAL: stencilCompareOp = FGL_COMPARE_OP_NOT_EQUAL; break;
		case GLS_STENCIL_FUNC_GEQUAL:	stencilCompareOp = FGL_COMPARE_OP_GREATER_EQUAL; break;
		case GLS_STENCIL_FUNC_ALWAYS:	stencilCompareOp = FGL_COMPARE_OP_ALWAYS; break;
		}

		depthStencilState.depthTestEnable = (stateBits & GLS_DEPTHTEST_DISABLE) ? FGL_FALSE : FGL_TRUE;
		depthStencilState.depthWriteEnable = (stateBits & GLS_DEPTHMASK) == 0;
		depthStencilState.depthCompareOp = depthCompareOp;
		depthStencilState.depthBoundsTestEnable = (stateBits & GLS_DEPTH_BOUNDS_TEST) != 0;
		depthStencilState.minDepthBounds = 0.0f;
		depthStencilState.maxDepthBounds = 1.0f;
		depthStencilState.stencilTestEnable = (stateBits & (GLS_STENCIL_FUNC_BITS | GLS_STENCIL_OP_BITS)) != 0;

		uint32_t ref = uint32_t((stateBits & GLS_STENCIL_FUNC_REF_BITS) >> GLS_STENCIL_FUNC_REF_SHIFT);
		uint32_t mask = uint32_t((stateBits & GLS_STENCIL_FUNC_MASK_BITS) >> GLS_STENCIL_FUNC_MASK_SHIFT);

		if (stateBits & GLS_SEPARATE_STENCIL)
		{
			depthStencilState.front = GetStencilOpState(stateBits & GLS_STENCIL_FRONT_OPS);
			depthStencilState.front.writeMask = 0xFFFFFFFF;
			depthStencilState.front.compareOp = stencilCompareOp;
			depthStencilState.front.compareMask = mask;
			depthStencilState.front.reference = ref;

			depthStencilState.back = GetStencilOpState((stateBits & GLS_STENCIL_BACK_OPS) >> 12);
			depthStencilState.back.writeMask = 0xFFFFFFFF;
			depthStencilState.back.compareOp = stencilCompareOp;
			depthStencilState.back.compareMask = mask;
			depthStencilState.back.reference = ref;
		}
		else
		{
			depthStencilState.front = GetStencilOpState(stateBits);
			depthStencilState.front.writeMask = 0xFFFFFFFF;
			depthStencilState.front.compareOp = stencilCompareOp;
			depthStencilState.front.compareMask = mask;
			depthStencilState.front.reference = ref;
			depthStencilState.back = depthStencilState.front;
		}
	}

	// Shader Stages
	idList< FglPipelineShaderStageCreateInfo > stages;
	FglPipelineShaderStageCreateInfo stage{};

	{
		stage.module = vertexShader;
		stage.stage = FGL_SHADER_STAGE_VERTEX_BIT;
		stage.pName = "MainVs";
		stages.Append(stage);
	}

	if (fragmentShader != FGL_NULL_HANDLE)
	{
		stage.module = fragmentShader;
		stage.stage = FGL_SHADER_STAGE_FRAGMENT_BIT;
		stage.pName = "MainPs";
		stages.Append(stage);
	}

	// Dynamic
	idList< FglDynamicState > dynamic;
	dynamic.Append(FGL_DYNAMIC_STATE_SCISSOR);
	dynamic.Append(FGL_DYNAMIC_STATE_VIEWPORT);

	/*
	if (stateBits & GLS_POLYGON_OFFSET) {
		dynamic.Append(VK_DYNAMIC_STATE_DEPTH_BIAS);
	}
	*/

	if (stateBits & GLS_DEPTH_BOUNDS_TEST)
	{
		dynamic.Append(FGL_DYNAMIC_STATE_DEPTH_BOUNDS);
	}

	FglPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.dynamicStateCount = dynamic.Num();
	dynamicState.pDynamicStates = dynamic.Ptr();

	// Viewport / Scissor
	FglPipelineViewportStateCreateInfo viewportState{};
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// Pipeline Create
	FglPipelineCreateInfo createInfo{};
	createInfo.layout = pipelineLayout;
	createInfo.renderPass = fglcontext.renderPass;
	createInfo.pVertexInputState = &vertexInputState;
	createInfo.pInputAssemblyState = &assemblyInputState;
	createInfo.pRasterizationState = &rasterizationState;
	createInfo.pColorBlendState = &colorBlendState;
	createInfo.pDepthStencilState = &depthStencilState;
	createInfo.pDynamicState = &dynamicState;
	createInfo.pViewportState = &viewportState;
	createInfo.stageCount = stages.Num();
	createInfo.pStages = stages.Ptr();

	FglPipeline pipeline = FGL_NULL_HANDLE;
	ID_FGL_CHECK(fglCreatePipelines(fglcontext.device, 1, &createInfo, nullptr, &pipeline));

	return pipeline;
}


/*
========================
renderProg_t::GetPipeline
========================
*/
FglPipeline renderProg_t::GetPipeline(uint64_t stateBits, FglShaderModule vertexShader, FglShaderModule fragmentShader) {
	for (int i = 0; i < pipelines.Num(); ++i) {
		if (stateBits == pipelines[i].stateBits) {
			return pipelines[i].pipeline;
		}
	}

	FglPipeline pipeline = CreateGraphicsPipeline(vertexLayoutType, vertexShader, fragmentShader, pipelineLayout, stateBits);

	pipelineState_t pipelineState;
	pipelineState.pipeline = pipeline;
	pipelineState.stateBits = stateBits;
	pipelines.Append(pipelineState);

	return pipeline;
}

/*
========================
RpPrintState
========================
*/
void RpPrintState(uint64_t stateBits)
{
	// culling
	common->Printf( "Culling: " );
	switch ( stateBits & GLS_CULL_BITS ) {
		case GLS_CULL_FRONTSIDED:	common->Printf( "FRONTSIDED -> BACK" ); break;
		case GLS_CULL_BACKSIDED:	common->Printf( "BACKSIDED -> FRONT" ); break;
		case GLS_CULL_TWOSIDED:		common->Printf( "TWOSIDED" ); break;
		default:					common->Printf( "NA" ); break;
	}
	common->Printf( "\n" );

	// polygon mode
	common->Printf( "PolygonMode: %s\n", ( stateBits & GLS_POLYMODE_LINE ) ? "LINE" : "FILL" );

	// color mask
	common->Printf( "ColorMask: " );
	common->Printf( ( stateBits & GLS_REDMASK ) ? "_" : "R" );
	common->Printf( ( stateBits & GLS_GREENMASK ) ? "_" : "G" );
	common->Printf( ( stateBits & GLS_BLUEMASK ) ? "_" : "B" );
	common->Printf( ( stateBits & GLS_ALPHAMASK ) ? "_" : "A" );
	common->Printf( "\n" );

	// blend
	common->Printf( "Blend: src=" );
	switch ( stateBits & GLS_SRCBLEND_BITS ) {
		case GLS_SRCBLEND_ZERO:					common->Printf( "ZERO" ); break;
		case GLS_SRCBLEND_ONE:					common->Printf( "ONE" ); break;
		case GLS_SRCBLEND_DST_COLOR:			common->Printf( "DST_COLOR" ); break;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:	common->Printf( "ONE_MINUS_DST_COLOR" ); break;
		case GLS_SRCBLEND_SRC_ALPHA:			common->Printf( "SRC_ALPHA" ); break;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:	common->Printf( "ONE_MINUS_SRC_ALPHA" ); break;
		case GLS_SRCBLEND_DST_ALPHA:			common->Printf( "DST_ALPHA" ); break;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:	common->Printf( "ONE_MINUS_DST_ALPHA" ); break;
		default:								common->Printf( "NA" ); break;
	}
	common->Printf( ", dst=" );
	switch ( stateBits & GLS_DSTBLEND_BITS ) {
		case GLS_DSTBLEND_ZERO:					common->Printf( "ZERO" ); break;
		case GLS_DSTBLEND_ONE:					common->Printf( "ONE" ); break;
		case GLS_DSTBLEND_SRC_COLOR:			common->Printf( "SRC_COLOR" ); break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:	common->Printf( "ONE_MINUS_SRC_COLOR" ); break;
		case GLS_DSTBLEND_SRC_ALPHA:			common->Printf( "SRC_ALPHA" ); break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:	common->Printf( "ONE_MINUS_SRC_ALPHA" ); break;
		case GLS_DSTBLEND_DST_ALPHA:			common->Printf( "DST_ALPHA" ); break;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:	common->Printf( "ONE_MINUS_DST_ALPHA" ); break;
		default:								common->Printf( "NA" );
	}
	common->Printf( "\n" );

	// depth func
	common->Printf( "DepthFunc: " );
	switch ( stateBits & (GLS_DEPTHFUNC_ALWAYS | GLS_DEPTHFUNC_EQUAL | GLS_DEPTHFUNC_LESS)) {
		case GLS_DEPTHFUNC_EQUAL:	common->Printf( "EQUAL" ); break;
		case GLS_DEPTHFUNC_ALWAYS:	common->Printf( "ALWAYS" ); break;
		case GLS_DEPTHFUNC_LESS:	common->Printf( "LEQUAL" ); break;
		//case GLS_DEPTHFUNC_GREATER: common->Printf( "GEQUAL" ); break;
		default:					common->Printf( "NA" ); break;
	}
	common->Printf( "\n" );

	// depth mask
	common->Printf( "DepthWrite: %s\n", ( stateBits & GLS_DEPTHMASK ) ? "FALSE" : "TRUE" );

	// depth bounds
	//common->Printf( "DepthBounds: %s\n", ( stateBits & GLS_DEPTH_TEST_MASK ) ? "TRUE" : "FALSE" );

	// depth bias
	//common->Printf( "DepthBias: %s\n", ( stateBits & GLS_POLYGON_OFFSET ) ? "TRUE" : "FALSE" );

	enum stencilFace_t
	{
		STENCIL_FACE_FRONT,
		STENCIL_FACE_BACK,
		STENCIL_FACE_NUM
	};

	// stencil
	auto printStencil = [&] ( stencilFace_t face, uint64_t bits, uint64_t mask, uint64_t ref ) {
		common->Printf( "Stencil: %s, ", ( bits & ( GLS_STENCIL_FUNC_BITS | GLS_STENCIL_OP_BITS ) ) ? "ON" : "OFF" );
		common->Printf( "Face=" );
		switch ( face ) {
			case STENCIL_FACE_FRONT: common->Printf( "FRONT" ); break;
			case STENCIL_FACE_BACK:  common->Printf( "BACK" ); break;
			default:				 common->Printf( "BOTH" ); break;
		}
		common->Printf( ", Func=" );
		switch ( bits & GLS_STENCIL_FUNC_BITS ) {
			case GLS_STENCIL_FUNC_NEVER:	common->Printf( "NEVER" ); break;
			case GLS_STENCIL_FUNC_LESS:		common->Printf( "LESS" ); break;
			case GLS_STENCIL_FUNC_EQUAL:	common->Printf( "EQUAL" ); break;
			case GLS_STENCIL_FUNC_LEQUAL:	common->Printf( "LEQUAL" ); break;
			case GLS_STENCIL_FUNC_GREATER:	common->Printf( "GREATER" ); break;
			case GLS_STENCIL_FUNC_NOTEQUAL: common->Printf( "NOTEQUAL" ); break;
			case GLS_STENCIL_FUNC_GEQUAL:	common->Printf( "GEQUAL" ); break;
			case GLS_STENCIL_FUNC_ALWAYS:	common->Printf( "ALWAYS" ); break;
			default:						common->Printf( "NA" ); break;
		}
		common->Printf( ", OpFail=" );
		switch( bits & GLS_STENCIL_OP_FAIL_BITS ) {
			case GLS_STENCIL_OP_FAIL_KEEP:		common->Printf( "KEEP" ); break;
			case GLS_STENCIL_OP_FAIL_ZERO:		common->Printf( "ZERO" ); break;
			case GLS_STENCIL_OP_FAIL_REPLACE:	common->Printf( "REPLACE" ); break;
			case GLS_STENCIL_OP_FAIL_INCR:		common->Printf( "INCR" ); break;
			case GLS_STENCIL_OP_FAIL_DECR:		common->Printf( "DECR" ); break;
			case GLS_STENCIL_OP_FAIL_INVERT:	common->Printf( "INVERT" ); break;
			case GLS_STENCIL_OP_FAIL_INCR_WRAP: common->Printf( "INCR_WRAP" ); break;
			case GLS_STENCIL_OP_FAIL_DECR_WRAP: common->Printf( "DECR_WRAP" ); break;
			default:							common->Printf( "NA" ); break;
		}
		common->Printf( ", ZFail=" );
		switch( bits & GLS_STENCIL_OP_ZFAIL_BITS ) {
			case GLS_STENCIL_OP_ZFAIL_KEEP:			common->Printf( "KEEP" ); break;
			case GLS_STENCIL_OP_ZFAIL_ZERO:			common->Printf( "ZERO" ); break;
			case GLS_STENCIL_OP_ZFAIL_REPLACE:		common->Printf( "REPLACE" ); break;
			case GLS_STENCIL_OP_ZFAIL_INCR:			common->Printf( "INCR" ); break;
			case GLS_STENCIL_OP_ZFAIL_DECR:			common->Printf( "DECR" ); break;
			case GLS_STENCIL_OP_ZFAIL_INVERT:		common->Printf( "INVERT" ); break;
			case GLS_STENCIL_OP_ZFAIL_INCR_WRAP:	common->Printf( "INCR_WRAP" ); break;
			case GLS_STENCIL_OP_ZFAIL_DECR_WRAP:	common->Printf( "DECR_WRAP" ); break;
			default:								common->Printf( "NA" ); break;
		}
		common->Printf( ", OpPass=" );
		switch( bits & GLS_STENCIL_OP_PASS_BITS ) {
			case GLS_STENCIL_OP_PASS_KEEP:			common->Printf( "KEEP" ); break;
			case GLS_STENCIL_OP_PASS_ZERO:			common->Printf( "ZERO" ); break;
			case GLS_STENCIL_OP_PASS_REPLACE:		common->Printf( "REPLACE" ); break;
			case GLS_STENCIL_OP_PASS_INCR:			common->Printf( "INCR" ); break;
			case GLS_STENCIL_OP_PASS_DECR:			common->Printf( "DECR" ); break;
			case GLS_STENCIL_OP_PASS_INVERT:		common->Printf( "INVERT" ); break;
			case GLS_STENCIL_OP_PASS_INCR_WRAP:		common->Printf( "INCR_WRAP" ); break;
			case GLS_STENCIL_OP_PASS_DECR_WRAP:		common->Printf( "DECR_WRAP" ); break;
			default:								common->Printf( "NA" ); break;
		}
		common->Printf( ", mask=%llu, ref=%llu\n", mask, ref );
	};

	uint32_t mask = uint32_t( ( stateBits & GLS_STENCIL_FUNC_MASK_BITS ) >> GLS_STENCIL_FUNC_MASK_SHIFT );
	uint32_t ref = uint32_t( ( stateBits & GLS_STENCIL_FUNC_REF_BITS ) >> GLS_STENCIL_FUNC_REF_SHIFT );
	if ( stateBits & GLS_SEPARATE_STENCIL ) {
		printStencil( STENCIL_FACE_FRONT, ( stateBits & GLS_STENCIL_FRONT_OPS ), mask, ref );
		printStencil( STENCIL_FACE_BACK, ( ( stateBits & GLS_STENCIL_BACK_OPS ) >> 12 ), mask, ref );
	} else {
		printStencil( STENCIL_FACE_NUM, stateBits, mask, ref );
	}
}

static void ClearPipelines_f(const idCmdArgs& args) {
	for (int i = 0; i < renderProgManager.m_renderProgs.Num(); ++i) {
		renderProg_t& prog = renderProgManager.m_renderProgs[i];
		for (int j = 0; j < prog.pipelines.Num(); ++j) {
			fglDestroyPipeline(fglcontext.device, prog.pipelines[j].pipeline, NULL);
		}
		prog.pipelines.Clear();
	}
}

static void PrintNumPipelines_f(const idCmdArgs & args) {
	int totalPipelines = 0;
	for (int i = 0; i < renderProgManager.m_renderProgs.Num(); ++i) {
		renderProg_t& prog = renderProgManager.m_renderProgs[i];
		int progPipelines = prog.pipelines.Num();
		totalPipelines += progPipelines;
		common->Printf("%s: %d\n", prog.name.c_str(), progPipelines);
	}
	common->Printf("TOTAL: %d\n", totalPipelines);
}

static void PrintPipelineStates_f(const idCmdArgs & args) {
	for (int i = 0; i < renderProgManager.m_renderProgs.Num(); ++i) {
		renderProg_t& prog = renderProgManager.m_renderProgs[i];
		for (int j = 0; j < prog.pipelines.Num(); ++j) {
			common->Printf("%s: %llu\n", prog.name.c_str(), prog.pipelines[j].stateBits);
			common->Printf("------------------------------------------\n");
			RpPrintState(prog.pipelines[j].stateBits);
			common->Printf("\n");
		}
	}
}

static void ReloadShaders_f(const idCmdArgs& args)
{
	renderProgManager.ReloadShaders();
}

/*
========================
idRenderProgManager::idRenderProgManager
========================
*/
idRenderProgManager::idRenderProgManager() :
	m_current(0),
	m_counter(0),
	m_currentData(0),
	m_currentDescSet(0),
	m_currentParmBufferOffset(0) {

	memset(m_parmBuffers, 0, sizeof(m_parmBuffers));
}


/*
========================
idRenderProgManager::Init
========================
*/
void idRenderProgManager::Init() {
	common->Printf("----- Initializing Render Shaders -----\n");

	struct builtinShaders_t {
		int index;
		const char* name;
		rpStage_t stages;
		vertexLayoutType_t layout;
	} builtins[MAX_BUILTINS] = {
		{ BUILTIN_GUI, "gui", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		{ BUILTIN_COLOR, "color", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_SIMPLESHADE, "simpleshade", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_TEXTURED, "texture", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR, "texture_color", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR_ALPHATEST, "texture_color_alphatest", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT},
		//{ BUILTIN_TEXTURE_VERTEXCOLOR_SKINNED, "texture_color_skinned", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_TEXTURE_TEXGEN_VERTEXCOLOR, "texture_color_texgen", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION, "interaction", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_INTERACTION_SKINNED, "interaction_skinned", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_INTERACTION_AMBIENT, "interactionAmbient", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_INTERACTION_AMBIENT_SKINNED, "interactionAmbient_skinned", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_ENVIRONMENT, "environment", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_ENVIRONMENT_SKINNED, "environment_skinned", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_BUMPY_ENVIRONMENT, "bumpyEnvironment", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_BUMPY_ENVIRONMENT_SKINNED, "bumpyEnvironment_skinned", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },

		{ BUILTIN_DEPTH, "depth", SHADER_STAGE_VERTEX, LAYOUT_DRAW_VERT },
		//{ BUILTIN_DEPTH_SKINNED, "depth_skinned", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		{ BUILTIN_SHADOW, "shadow", SHADER_STAGE_VERTEX, LAYOUT_DRAW_SHADOW_VERT },
		//{ BUILTIN_SHADOW_SKINNED, "shadow_skinned", SHADER_STAGE_VERTEX, LAYOUT_DRAW_SHADOW_VERT_SKINNED },
		{ BUILTIN_SHADOW_DEBUG, "shadowDebug", SHADER_STAGE_ALL, LAYOUT_DRAW_SHADOW_VERT },
		//{ BUILTIN_SHADOW_DEBUG_SKINNED, "shadowDebug_skinned", SHADER_STAGE_ALL, LAYOUT_DRAW_SHADOW_VERT_SKINNED },

		//{ BUILTIN_BLENDLIGHT, "blendlight", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_FOG, "fog", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_FOG_SKINNED, "fog_skinned", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_SKYBOX, "skybox", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_WOBBLESKY, "wobblesky", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_BINK, "bink", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
		//{ BUILTIN_BINK_GUI, "bink_gui", SHADER_STAGE_ALL, LAYOUT_DRAW_VERT },
	};
	m_renderProgs.SetNum(MAX_BUILTINS);

	for (int i = 0; i < MAX_BUILTINS; i++) {

		int vIndex = -1;
		if (builtins[i].stages & SHADER_STAGE_VERTEX) {
			vIndex = FindShader(builtins[i].name, SHADER_STAGE_VERTEX);
		}

		int fIndex = -1;
		if (builtins[i].stages & SHADER_STAGE_FRAGMENT) {
			fIndex = FindShader(builtins[i].name, SHADER_STAGE_FRAGMENT);
		}

		renderProg_t& prog = m_renderProgs[i];
		prog.name = builtins[i].name;
		prog.vertexShaderIndex = vIndex;
		prog.fragmentShaderIndex = fIndex;
		prog.vertexLayoutType = builtins[i].layout;

		CreateDescriptorSetLayout(
			m_shaders[vIndex],
			(fIndex > -1) ? m_shaders[fIndex] : defaultShader,
			prog);
	}

	m_uniforms.SetNum(RENDERPARM_TOTAL/*, vec4_zero*/);

	/*
	m_renderProgs[BUILTIN_TEXTURE_VERTEXCOLOR_SKINNED].usesJoints = true;
	m_renderProgs[BUILTIN_INTERACTION_SKINNED].usesJoints = true;
	m_renderProgs[BUILTIN_INTERACTION_AMBIENT_SKINNED].usesJoints = true;
	m_renderProgs[BUILTIN_ENVIRONMENT_SKINNED].usesJoints = true;
	m_renderProgs[BUILTIN_BUMPY_ENVIRONMENT_SKINNED].usesJoints = true;
	m_renderProgs[BUILTIN_DEPTH_SKINNED].usesJoints = true;
	m_renderProgs[BUILTIN_SHADOW_SKINNED].usesJoints = true;
	m_renderProgs[BUILTIN_SHADOW_DEBUG_SKINNED].usesJoints = true;
	m_renderProgs[BUILTIN_FOG_SKINNED].usesJoints = true;
	*/

	// Create Vertex Descriptions
	CreateVertexDescriptions();

	// Create Descriptor Pools
	CreateDescriptorPools(m_descriptorPools);

	for (int i = 0; i < NUM_FRAME_DATA; ++i) {
		m_parmBuffers[i] = new idUniformBuffer();
		m_parmBuffers[i]->AllocBufferObject(NULL, MAX_DESC_SETS * MAX_DESC_SET_UNIFORMS * sizeof(idVec4), BU_DYNAMIC);
	}

	// Placeholder: mainly for optionalSkinning
	emptyUBO.AllocBufferObject(NULL, sizeof(idVec4), BU_DYNAMIC);

	cmdSystem->AddCommand("Fury_ClearPipelines", ClearPipelines_f, CMD_FL_RENDERER, "Clear all existing pipelines, forcing them to be recreated.");
	cmdSystem->AddCommand("Fury_PrintNumPipelines", PrintNumPipelines_f, CMD_FL_RENDERER, "Print the number of pipelines available.");
	cmdSystem->AddCommand("Fury_PrintPipelineStates", PrintPipelineStates_f, CMD_FL_RENDERER, "Print the GLState bits associated with each pipeline.");
	cmdSystem->AddCommand("ReloadShaders", ReloadShaders_f, CMD_FL_RENDERER, "Reload all shader programs from disk");
}

/*
========================
idRenderProgManager::Shutdown
========================
*/
void idRenderProgManager::Shutdown() {
	// destroy shaders
	for (int i = 0; i < m_shaders.Num(); ++i) {
		shader_t& shader = m_shaders[i];
		fglDestroyShaderModule(fglcontext.device, shader.module, NULL);
		shader.module = FGL_NULL_HANDLE;
	}

	// destroy pipelines
	for (int i = 0; i < m_renderProgs.Num(); ++i) {
		renderProg_t& prog = m_renderProgs[i];

		for (int j = 0; j < prog.pipelines.Num(); ++j) {
			fglDestroyPipeline(fglcontext.device, prog.pipelines[j].pipeline, NULL);
		}
		prog.pipelines.Clear();

		fglDestroyDescriptorSetLayout(fglcontext.device, prog.descriptorSetLayout, NULL);
		fglDestroyPipelineLayout(fglcontext.device, prog.pipelineLayout, NULL);
	}
	m_renderProgs.Clear();

	for (int i = 0; i < NUM_FRAME_DATA; ++i) {
		m_parmBuffers[i]->FreeBufferObject();
		delete m_parmBuffers[i];
		m_parmBuffers[i] = NULL;
	}

	emptyUBO.FreeBufferObject();

	for (int i = 0; i < NUM_FRAME_DATA; ++i) {
		//vkFreeDescriptorSets( vkcontext.device, m_descriptorPools[ i ], MAX_DESC_SETS, m_descriptorSets[ i ] );
		fglResetDescriptorPool(fglcontext.device, m_descriptorPools[i]);
		fglDestroyDescriptorPool(fglcontext.device, m_descriptorPools[i], NULL);
	}

	memset(m_descriptorSets, 0, sizeof(m_descriptorSets));
	memset(m_descriptorPools, 0, sizeof(m_descriptorPools));

	m_counter = 0;
	m_currentData = 0;
	m_currentDescSet = 0;
}

/*
========================
idRenderProgManager::StartFrame
========================
*/
void idRenderProgManager::StartFrame() {
	m_counter++;
	m_currentData = m_counter % NUM_FRAME_DATA;
	m_currentDescSet = 0;
	m_currentParmBufferOffset = 0;

	fglResetDescriptorPool(fglcontext.device, m_descriptorPools[m_currentData]);
}

/*
========================
idRenderProgManager::BindProgram
========================
*/
void idRenderProgManager::BindProgram(int index) {
	if (m_current == index) {
		return;
	}

	m_current = index;
	RB_LogComment("Binding SPIRV Program %s\n", m_renderProgs[index].name.c_str());
}

/*
========================
idRenderProgManager::AllocParmBlockBuffer
========================
*/
void idRenderProgManager::AllocParmBlockBuffer(const idList< int >& parmIndices, idUniformBuffer& ubo) {
	const int numParms = parmIndices.Num();
	const int bytes = numParms * sizeof(idVec4);

	ubo.Reference(*m_parmBuffers[m_currentData], m_currentParmBufferOffset, bytes);

	idVec4* uniforms = (idVec4*)ubo.MapBuffer(BM_WRITE);

	for (int i = 0; i < numParms; ++i) {
		uniforms[i] = renderProgManager.GetRenderParm(static_cast<renderParm_t>(parmIndices[i]));
	}

	ubo.UnmapBuffer();

	m_currentParmBufferOffset += bytes;
}

/*
========================
idRenderProgManager::CommitCurrent
========================
*/
void idRenderProgManager::CommitCurrent(uint64_t stateBits, FglCommandBuffer commandBuffer) {
	renderProg_t& prog = m_renderProgs[m_current];

	//common->Printf("======= CommitCurrent\n");
	//RpPrintState(stateBits);

	FglPipeline pipeline = prog.GetPipeline(
		stateBits,
		m_shaders[prog.vertexShaderIndex].module,
		prog.fragmentShaderIndex != -1 ? m_shaders[prog.fragmentShaderIndex].module : FGL_NULL_HANDLE);

	FglDescriptorSetAllocateInfo setAllocInfo = {};
	setAllocInfo.descriptorPool = m_descriptorPools[m_currentData];
	setAllocInfo.descriptorSetCount = 1;
	setAllocInfo.pSetLayouts = &prog.descriptorSetLayout;

	ID_FGL_CHECK(fglAllocateDescriptorSets(fglcontext.device, &setAllocInfo, &m_descriptorSets[m_currentData][m_currentDescSet]));

	FglDescriptorSet descSet = m_descriptorSets[m_currentData][m_currentDescSet];
	m_currentDescSet++;

	int writeIndex = 0;
	int bufferIndex = 0;
	int	imageIndex = 0;
	int bindingIndex = 0;

	FglWriteDescriptorSet writes[MAX_DESC_SET_WRITES];
	FglDescriptorBufferInfo bufferInfos[MAX_DESC_SET_WRITES];
	FglDescriptorImageInfo imageInfos[MAX_DESC_SET_WRITES];

	int uboIndex = 0;
	idUniformBuffer* ubos[3] = { NULL, NULL, NULL };

	idUniformBuffer vertParms;
	if (prog.vertexShaderIndex > -1 && m_shaders[prog.vertexShaderIndex].parmIndices.Num() > 0)
	{
		const idList<int>& params = m_shaders[prog.vertexShaderIndex].parmIndices;
		AllocParmBlockBuffer(params, vertParms);
		/*
		common->Printf("VERTEX PARAMS: %d\n", params.Num());
		for (int i = 0; i < params.Num(); ++i)
		{
			const idVec4 &param = renderProgManager.GetRenderParm(static_cast<renderParm_t>(params[i]));
			common->Printf("  %s = (%f, %f, %f, %f)\n", GLSLParmNames[params[i]], param.x, param.y, param.z, param.w);
		}
		*/

		ubos[uboIndex++] = &vertParms;
	}

	idUniformBuffer jointBuffer;
	if (prog.usesJoints && fglcontext.jointCacheHandle > 0) {
		if (!vertexCache.GetJointBuffer(fglcontext.jointCacheHandle, &jointBuffer)) {
			common->Error("idRenderProgManager::CommitCurrent: jointBuffer == NULL");
			return;
		}
		//assert((jointBuffer.GetOffset() & (vkcontext.gpu.props.limits.minUniformBufferOffsetAlignment - 1)) == 0);

		ubos[uboIndex++] = &jointBuffer;
	}
	else if (prog.optionalSkinning) {
		ubos[uboIndex++] = &emptyUBO;
	}

	idUniformBuffer fragParms;
	if (prog.fragmentShaderIndex > -1 && m_shaders[prog.fragmentShaderIndex].parmIndices.Num() > 0)
	{
		const idList<int>& params = m_shaders[prog.fragmentShaderIndex].parmIndices;
		AllocParmBlockBuffer(params, fragParms);
		/*
		common->Printf("PIXEL PARAMS: %d\n", params.Num());
		for (int i = 0; i < params.Num(); ++i)
		{
			const idVec4& param = renderProgManager.GetRenderParm(static_cast<renderParm_t>(params[i]));
			common->Printf("  %s = (%f, %f, %f, %f)\n", GLSLParmNames[params[i]], param.x, param.y, param.z, param.w);
		}
		*/

		ubos[uboIndex++] = &fragParms;
	}

	for (int i = 0; i < prog.bindings.Num(); ++i) {
		rpBinding_t binding = prog.bindings[i];

		switch (binding) {
		case BINDING_TYPE_UNIFORM_BUFFER: {
			idUniformBuffer* ubo = ubos[bufferIndex];

			FglDescriptorBufferInfo& bufferInfo = bufferInfos[bufferIndex++];
			memset(&bufferInfo, 0, sizeof(FglDescriptorBufferInfo));
			bufferInfo.buffer = ubo->GetAPIObject();
			bufferInfo.offset = ubo->GetOffset();
			bufferInfo.range = ubo->GetSize();

			FglWriteDescriptorSet& write = writes[writeIndex++];
			memset(&write, 0, sizeof(FglWriteDescriptorSet));
			write.dstSet = descSet;
			write.dstBinding = bindingIndex++;
			write.descriptorCount = 1;
			write.descriptorType = FGL_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			write.pBufferInfo = &bufferInfo;

			break;
		}
		case BINDING_TYPE_SAMPLER: {
			idImage* image = fglcontext.imageParms[imageIndex];

			FglDescriptorImageInfo& imageInfo = imageInfos[imageIndex++];
			memset(&imageInfo, 0, sizeof(FglDescriptorImageInfo));
			imageInfo.imageView = image->GetView();
			imageInfo.sampler = image->GetSampler();

			assert(image->GetView() != FGL_NULL_HANDLE);

			FglWriteDescriptorSet& write = writes[writeIndex++];
			memset(&write, 0, sizeof(FglWriteDescriptorSet));
			write.dstSet = descSet;
			write.dstBinding = bindingIndex++;
			write.descriptorCount = 1;
			write.descriptorType = FGL_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo = &imageInfo;

			break;
		}
		}
	}

	fglUpdateDescriptorSets(fglcontext.device, writeIndex, writes, 0, NULL);

	fglCmdBindDescriptorSets(commandBuffer, prog.pipelineLayout, 0, 1, &descSet);
	fglCmdBindPipeline(commandBuffer, pipeline);
}

/*
========================
idRenderProgManager::FindProgram
========================
*/
int idRenderProgManager::FindProgram(const char* name, int vIndex, int fIndex) {
	for (int i = 0; i < m_renderProgs.Num(); ++i) {
		renderProg_t& prog = m_renderProgs[i];
		if (prog.vertexShaderIndex == vIndex &&
			prog.fragmentShaderIndex == fIndex) {
			return i;
		}
	}

	renderProg_t program;
	program.name = name;
	program.vertexShaderIndex = vIndex;
	program.fragmentShaderIndex = fIndex;

	CreateDescriptorSetLayout(m_shaders[vIndex], m_shaders[fIndex], program);

	// HACK: HeatHaze ( optional skinning )
	{
		static const int heatHazeNameNum = 3;
		static const char* const heatHazeNames[heatHazeNameNum] = {
			"heatHaze",
			"heatHazeWithMask",
			"heatHazeWithMaskAndVertex"
		};
		for (int i = 0; i < heatHazeNameNum; ++i) {
			// Use the vertex shader name because the renderProg name is more unreliable
			if (idStr::Icmp(m_shaders[vIndex].name.c_str(), heatHazeNames[i]) == 0) {
				program.usesJoints = true;
				program.optionalSkinning = true;
				break;
			}
		}
	}

	int index = m_renderProgs.Append(program);
	return index;
}

/*
========================
idRenderProgManager::LoadShader
========================
*/
void idRenderProgManager::LoadShader(int index) {
	if (m_shaders[index].module != FGL_NULL_HANDLE) {
		return; // Already loaded
	}

	LoadShader(m_shaders[index]);
}

/*
========================
idRenderProgManager::LoadShader
========================
*/
void idRenderProgManager::LoadShader(shader_t& shader) {

	auto convertToWideString = [](const idStr &str)
	{
		size_t size = 0;
		mbstowcs_s(&size, nullptr, 0, str.c_str(), 0);
		std::wstring wstr(size, L'\0');
		mbstowcs_s(&size, &wstr[0], size + 1, str.c_str(), size);
		return std::move(wstr);
	};

	std::wstring entrypoint;
	std::wstring profile;
	if (shader.stage == SHADER_STAGE_FRAGMENT)
	{
		entrypoint = L"MainPs";
		profile = L"ps_6_0";
	}
	else
	{
		entrypoint = L"MainVs";
		profile = L"vs_6_0";
	}

	std::wstring wname = convertToWideString(shader.name);

	idStr shaderFile = shader.name + ".hlsl";
	idStr path = "shaders\\" + shaderFile;

	const char* src = nullptr;
	const int srclen = fileSystem->ReadFile(path, (void**)&src);
	if (srclen < 0)
	{
		common->Error("Couldn't load %s\n", path.c_str());
	}


	HRESULT hr;

	ComPtr<IDxcUtils> pUtils;
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf()));
	ComPtr<IDxcBlobEncoding> pSource;
	pUtils->CreateBlob(src, (UINT32)srclen, CP_UTF8, pSource.GetAddressOf());

	fileSystem->FreeFile((void*)src);

	DxcBuffer srcBuffer{};
	srcBuffer.Ptr = pSource->GetBufferPointer();
	srcBuffer.Size = pSource->GetBufferSize();
	BOOL knownEncoding = false;
	pSource->GetEncoding(&knownEncoding, &srcBuffer.Encoding);

	ComPtr<IDxcBlob> pShaderBlob;

	LPCWSTR compileArgs[] = { L"-spirv", L"-Zi" };

	ComPtr<IDxcCompilerArgs> pArgs;
	pUtils->BuildArguments(wname.c_str(), entrypoint.c_str(), profile.c_str(), compileArgs, sizeof(compileArgs) / sizeof(compileArgs[0]), nullptr, 0, pArgs.GetAddressOf());

	ComPtr<IDxcCompiler3> pCompiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf()));

	ComPtr<IDxcResult> pResult;
	hr = pCompiler->Compile(&srcBuffer, pArgs->GetArguments(), pArgs->GetCount(), nullptr, IID_PPV_ARGS(pResult.GetAddressOf()));
	if (FAILED(hr))
	{
		common->Error("Could not compile %s!\n", path.c_str());
	}

	if (pResult->HasOutput(DXC_OUT_ERRORS))
	{
		ComPtr<IDxcBlobEncoding> pErrors;
		hr = pResult->GetErrorBuffer(pErrors.GetAddressOf());
		if (SUCCEEDED(hr))
		{
			if (pErrors->GetBufferSize())
			{
				std::string errstr = std::string((const char*)pErrors->GetBufferPointer(), (const char*)pErrors->GetBufferPointer() + pErrors->GetBufferSize());
				common->Error("Compilation errors for %s: %s\n", path.c_str(), errstr.c_str());
			}
		}
	}

	pResult->GetResult(pShaderBlob.GetAddressOf());

	const uint32_t* pSpv = (const uint32_t*)pShaderBlob->GetBufferPointer();
	size_t spvSize = pShaderBlob->GetBufferSize();

	// Reflect the compiled shader to figure out what data it requires
	SpvReflectShaderModule module = {};
	SpvReflectResult result = spvReflectCreateShaderModule(spvSize, pSpv, &module);
	if (result != SPV_REFLECT_RESULT_SUCCESS)
	{
		common->Error("SPIR-V reflection failed! %s", shaderFile.c_str());
	}

	uint32_t count = 0;
	result = spvReflectEnumerateDescriptorSets(&module, &count, NULL);

	std::vector<SpvReflectDescriptorSet*> sets(count);
	result = spvReflectEnumerateDescriptorSets(&module, &count, sets.data());

	for (const SpvReflectDescriptorSet* pSet : sets)
	{
		for (uint32_t bi = 0; bi < pSet->binding_count; ++bi)
		{
			const SpvReflectDescriptorBinding* pBinding = pSet->bindings[bi];

			if (pBinding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			{
				shader.bindings.Append(BINDING_TYPE_UNIFORM_BUFFER);

				const SpvReflectTypeDescription* pTypeDesc = pBinding->type_description;
				for (uint32_t mi = 0; mi < pTypeDesc->member_count; ++mi)
				{
					const SpvReflectTypeDescription* pMember = &pTypeDesc->members[mi];
					int index = -1;
					for (int i = 0; i < RENDERPARM_TOTAL && index == -1; ++i)
					{
						if (!strcmp(pMember->struct_member_name, GLSLParmNames[i]))
						{
							index = i;
							break;
						}
					}

					if (index != -1)
					{
						shader.parmIndices.Append(static_cast<renderParm_t>(index));
					}
					else
					{
						common->Error("Invalid uniform %s", pMember->struct_member_name);
					}
				}
			}
			else if (pBinding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			{
				shader.bindings.Append(BINDING_TYPE_SAMPLER);
			}
		}
	}

	spvReflectDestroyShaderModule(&module);

	FglShaderModuleCreateInfo shaderModuleCreateInfo = {};
	shaderModuleCreateInfo.codeSize = spvSize;
	shaderModuleCreateInfo.pCode = pSpv;

	ID_FGL_CHECK(fglCreateShaderModule(fglcontext.device, &shaderModuleCreateInfo, NULL, &shader.module));
}

/*
========================
idRenderProgManager::GetRenderParm
========================
*/
const idVec4 & idRenderProgManager::GetRenderParm( renderParm_t rp ) {
	return m_uniforms[ rp ];
}

/*
========================
idRenderProgManager::SetRenderParm
========================
*/
void idRenderProgManager::SetRenderParm( renderParm_t rp, const float * value ) {
	for ( int i = 0; i < 4; ++i ) {
		m_uniforms[rp][i] = value[i];
	}
}

/*
========================
idRenderProgManager::SetRenderParms
========================
*/
void idRenderProgManager::SetRenderParms( renderParm_t rp, const float * value, int num ) {
	for ( int i = 0; i < num; ++i ) {
		SetRenderParm( (renderParm_t)(rp + i), value + ( i * 4 ) );
	}
}

/*
========================
idRenderProgManager::FindShader
========================
*/
int idRenderProgManager::FindShader( const char * name, rpStage_t stage ) {
	idStr shaderName( name );
	shaderName.StripFileExtension();

	for ( int i = 0; i < m_shaders.Num(); i++ ) {
		const shader_t & shader = m_shaders[ i ];
		if ( shader.name.Icmp( shaderName.c_str() ) == 0 && shader.stage == stage ) {
			LoadShader( i );
			return i;
		}
	}
	shader_t shader;
	shader.name = shaderName;
	shader.stage = stage;
	int index = m_shaders.Append( shader );
	LoadShader( index );
	return index;
}

void idRenderProgManager::ReloadShaders()
{
	for (int i = 0; i < m_shaders.Num(); ++i)
	{
		if (m_shaders[i].module != FGL_NULL_HANDLE)
		{
			fglDestroyShaderModule(fglcontext.device, m_shaders[i].module, nullptr);
			m_shaders[i].module = FGL_NULL_HANDLE;
			m_shaders[i].bindings.Clear();
			m_shaders[i].parmIndices.Clear();

			LoadShader(i);
		}
	}

	for (int i = 0; i < m_renderProgs.Num(); ++i)
	{
		m_renderProgs[i].pipelines.Clear();
		m_renderProgs[i].bindings.Clear();

		int vIndex = m_renderProgs[i].vertexShaderIndex;
		int fIndex = m_renderProgs[i].fragmentShaderIndex;

		CreateDescriptorSetLayout(
			m_shaders[vIndex],
			(fIndex > -1) ? m_shaders[fIndex] : defaultShader,
			m_renderProgs[i]);
	}
}