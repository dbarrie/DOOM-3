
#ifndef _RENDERCOMMON_H_
#define _RENDERCOMMON_H_

#include <furygl.h>

const int NUM_FRAME_DATA = 2;

typedef uint64_t vertCacheHandle_t;

static const int MAX_DESC_SETS = 16384;
static const int MAX_DESC_UNIFORM_BUFFERS = 8192;
static const int MAX_DESC_IMAGE_SAMPLERS = 12384;
static const int MAX_DESC_SET_WRITES = 32;
static const int MAX_DESC_SET_UNIFORMS = 48;
static const int MAX_IMAGE_PARMS = 16;
static const int MAX_UBO_PARMS = 2;

/*
=============================================================

RENDERER BACK END COMMAND QUEUE

TR_CMDS

=============================================================
*/

class idImage;
struct viewDef_s;
typedef struct viewDef_s viewDef_t;

typedef enum {
	RC_NOP,
	RC_DRAW_VIEW,
	RC_SET_BUFFER,
	RC_COPY_RENDER,
	RC_SWAP_BUFFERS		// can't just assume swap at end of list because
						// of forced list submission before syncs
} renderCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
} emptyCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
	//GLenum	buffer;
	int		frameCount;
} setBufferCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
	viewDef_t* viewDef;
} drawSurfsCommand_t;

typedef struct {
	renderCommand_t		commandId, * next;
	int		x, y, imageWidth, imageHeight;
	idImage* image;
	int		cubeFace;					// when copying to a cubeMap
} copyRenderCommand_t;


const uint64_t GLS_SRCBLEND_ZERO =					0x0000000000000001;
const uint64_t GLS_SRCBLEND_ONE =					0x0000000000000000;
const uint64_t GLS_SRCBLEND_DST_COLOR =				0x0000000000000003;
const uint64_t GLS_SRCBLEND_ONE_MINUS_DST_COLOR =	0x0000000000000004;
const uint64_t GLS_SRCBLEND_SRC_ALPHA =				0x0000000000000005;
const uint64_t GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA =	0x0000000000000006;
const uint64_t GLS_SRCBLEND_DST_ALPHA =				0x0000000000000007;
const uint64_t GLS_SRCBLEND_ONE_MINUS_DST_ALPHA =	0x0000000000000008;
const uint64_t GLS_SRCBLEND_ALPHA_SATURATE =		0x0000000000000009;
const uint64_t GLS_SRCBLEND_BITS =					0x000000000000000f;

const uint64_t GLS_DSTBLEND_ZERO =					0x0000000000000000;
const uint64_t GLS_DSTBLEND_ONE =					0x0000000000000020;
const uint64_t GLS_DSTBLEND_SRC_COLOR =				0x0000000000000030;
const uint64_t GLS_DSTBLEND_ONE_MINUS_SRC_COLOR =	0x0000000000000040;
const uint64_t GLS_DSTBLEND_SRC_ALPHA =				0x0000000000000050;
const uint64_t GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA =	0x0000000000000060;
const uint64_t GLS_DSTBLEND_DST_ALPHA =				0x0000000000000070;
const uint64_t GLS_DSTBLEND_ONE_MINUS_DST_ALPHA =	0x0000000000000080;
const uint64_t GLS_DSTBLEND_BITS =					0x00000000000000f0;


// these masks are the inverse, meaning when set the glColorMask value will be 0,
// preventing that channel from being written
const uint64_t GLS_DEPTHMASK =						0x0000000000000100;
const uint64_t GLS_REDMASK =						0x0000000000000200;
const uint64_t GLS_GREENMASK =						0x0000000000000400;
const uint64_t GLS_BLUEMASK =						0x0000000000000800;
const uint64_t GLS_ALPHAMASK =						0x0000000000001000;
const uint64_t GLS_COLORMASK = (GLS_REDMASK | GLS_GREENMASK | GLS_BLUEMASK);

const uint64_t GLS_POLYMODE_LINE =					0x0000000000002000;

const uint64_t GLS_DEPTHTEST_DISABLE =				0x0000000000004000;
const uint64_t GLS_DEPTHFUNC_LESS =					0x0000000000000000;
const uint64_t GLS_DEPTHFUNC_ALWAYS =				0x0000000000010000;
const uint64_t GLS_DEPTHFUNC_EQUAL =				0x0000000000020000;
const uint64_t GLS_DEPTHFUNC_BITS =					0x0000000000030000;

const uint64_t GLS_CULL_FRONTSIDED =				0x0000000000000000;
const uint64_t GLS_CULL_BACKSIDED =					0x0000000000040000;
const uint64_t GLS_CULL_TWOSIDED =					0x0000000000080000;
const uint64_t GLS_CULL_BITS =						0x00000000000C0000;

const uint64_t GLS_STENCIL_FUNC_REF_SHIFT = 20;
const uint64_t GLS_STENCIL_FUNC_REF_BITS = 0xFFll << GLS_STENCIL_FUNC_REF_SHIFT;

const uint64_t GLS_STENCIL_FUNC_MASK_SHIFT = 28;
const uint64_t GLS_STENCIL_FUNC_MASK_BITS = 0xFFll << GLS_STENCIL_FUNC_MASK_SHIFT;

#define GLS_STENCIL_MAKE_REF(x) (((uint64_t)(x) << GLS_STENCIL_FUNC_REF_SHIFT) & GLS_STENCIL_FUNC_REF_BITS)
#define GLS_STENCIL_MAKE_MASK(x) (((uint64_t)(x) << GLS_STENCIL_FUNC_MASK_SHIFT) & GLS_STENCIL_FUNC_MASK_BITS)

// Next 12 bits act as front+back unless GLS_SEPARATE_STENCIL is set, in which case it acts as front.
const uint64_t GLS_STENCIL_FUNC_ALWAYS				= 0ull << 36;
const uint64_t GLS_STENCIL_FUNC_LESS				= 1ull << 36;
const uint64_t GLS_STENCIL_FUNC_LEQUAL				= 2ull << 36;
const uint64_t GLS_STENCIL_FUNC_GREATER				= 3ull << 36;
const uint64_t GLS_STENCIL_FUNC_GEQUAL				= 4ull << 36;
const uint64_t GLS_STENCIL_FUNC_EQUAL				= 5ull << 36;
const uint64_t GLS_STENCIL_FUNC_NOTEQUAL			= 6ull << 36;
const uint64_t GLS_STENCIL_FUNC_NEVER				= 7ull << 36;
const uint64_t GLS_STENCIL_FUNC_BITS				= 7ull << 36;

const uint64_t GLS_STENCIL_OP_FAIL_KEEP				= 0ull << 39;
const uint64_t GLS_STENCIL_OP_FAIL_ZERO				= 1ull << 39;
const uint64_t GLS_STENCIL_OP_FAIL_REPLACE			= 2ull << 39;
const uint64_t GLS_STENCIL_OP_FAIL_INCR				= 3ull << 39;
const uint64_t GLS_STENCIL_OP_FAIL_DECR				= 4ull << 39;
const uint64_t GLS_STENCIL_OP_FAIL_INVERT			= 5ull << 39;
const uint64_t GLS_STENCIL_OP_FAIL_INCR_WRAP		= 6ull << 39;
const uint64_t GLS_STENCIL_OP_FAIL_DECR_WRAP		= 7ull << 39;
const uint64_t GLS_STENCIL_OP_FAIL_BITS				= 7ull << 39;

const uint64_t GLS_STENCIL_OP_ZFAIL_KEEP			= 0ull << 42;
const uint64_t GLS_STENCIL_OP_ZFAIL_ZERO			= 1ull << 42;
const uint64_t GLS_STENCIL_OP_ZFAIL_REPLACE			= 2ull << 42;
const uint64_t GLS_STENCIL_OP_ZFAIL_INCR			= 3ull << 42;
const uint64_t GLS_STENCIL_OP_ZFAIL_DECR			= 4ull << 42;
const uint64_t GLS_STENCIL_OP_ZFAIL_INVERT			= 5ull << 42;
const uint64_t GLS_STENCIL_OP_ZFAIL_INCR_WRAP		= 6ull << 42;
const uint64_t GLS_STENCIL_OP_ZFAIL_DECR_WRAP		= 7ull << 42;
const uint64_t GLS_STENCIL_OP_ZFAIL_BITS			= 7ull << 42;

const uint64_t GLS_STENCIL_OP_PASS_KEEP				= 0ull << 45;
const uint64_t GLS_STENCIL_OP_PASS_ZERO				= 1ull << 45;
const uint64_t GLS_STENCIL_OP_PASS_REPLACE			= 2ull << 45;
const uint64_t GLS_STENCIL_OP_PASS_INCR				= 3ull << 45;
const uint64_t GLS_STENCIL_OP_PASS_DECR				= 4ull << 45;
const uint64_t GLS_STENCIL_OP_PASS_INVERT			= 5ull << 45;
const uint64_t GLS_STENCIL_OP_PASS_INCR_WRAP		= 6ull << 45;
const uint64_t GLS_STENCIL_OP_PASS_DECR_WRAP		= 7ull << 45;
const uint64_t GLS_STENCIL_OP_PASS_BITS				= 7ull << 45;

// Next 12 bits act as back and are only active when GLS_SEPARATE_STENCIL is set.
const uint64_t GLS_BACK_STENCIL_FUNC_ALWAYS			= 0ull << 48;
const uint64_t GLS_BACK_STENCIL_FUNC_LESS			= 1ull << 48;
const uint64_t GLS_BACK_STENCIL_FUNC_LEQUAL			= 2ull << 48;
const uint64_t GLS_BACK_STENCIL_FUNC_GREATER		= 3ull << 48;
const uint64_t GLS_BACK_STENCIL_FUNC_GEQUAL			= 4ull << 48;
const uint64_t GLS_BACK_STENCIL_FUNC_EQUAL			= 5ull << 48;
const uint64_t GLS_BACK_STENCIL_FUNC_NOTEQUAL		= 6ull << 48;
const uint64_t GLS_BACK_STENCIL_FUNC_NEVER			= 7ull << 48;
const uint64_t GLS_BACK_STENCIL_FUNC_BITS			= 7ull << 48;

const uint64_t GLS_BACK_STENCIL_OP_FAIL_KEEP		= 0ull << 51;
const uint64_t GLS_BACK_STENCIL_OP_FAIL_ZERO		= 1ull << 51;
const uint64_t GLS_BACK_STENCIL_OP_FAIL_REPLACE		= 2ull << 51;
const uint64_t GLS_BACK_STENCIL_OP_FAIL_INCR		= 3ull << 51;
const uint64_t GLS_BACK_STENCIL_OP_FAIL_DECR		= 4ull << 51;
const uint64_t GLS_BACK_STENCIL_OP_FAIL_INVERT		= 5ull << 51;
const uint64_t GLS_BACK_STENCIL_OP_FAIL_INCR_WRAP	= 6ull << 51;
const uint64_t GLS_BACK_STENCIL_OP_FAIL_DECR_WRAP	= 7ull << 51;
const uint64_t GLS_BACK_STENCIL_OP_FAIL_BITS		= 7ull << 51;

const uint64_t GLS_BACK_STENCIL_OP_ZFAIL_KEEP		= 0ull << 54;
const uint64_t GLS_BACK_STENCIL_OP_ZFAIL_ZERO		= 1ull << 54;
const uint64_t GLS_BACK_STENCIL_OP_ZFAIL_REPLACE	= 2ull << 54;
const uint64_t GLS_BACK_STENCIL_OP_ZFAIL_INCR		= 3ull << 54;
const uint64_t GLS_BACK_STENCIL_OP_ZFAIL_DECR		= 4ull << 54;
const uint64_t GLS_BACK_STENCIL_OP_ZFAIL_INVERT		= 5ull << 54;
const uint64_t GLS_BACK_STENCIL_OP_ZFAIL_INCR_WRAP	= 6ull << 54;
const uint64_t GLS_BACK_STENCIL_OP_ZFAIL_DECR_WRAP	= 7ull << 54;
const uint64_t GLS_BACK_STENCIL_OP_ZFAIL_BITS		= 7ull << 54;

const uint64_t GLS_BACK_STENCIL_OP_PASS_KEEP		= 0ull << 57;
const uint64_t GLS_BACK_STENCIL_OP_PASS_ZERO		= 1ull << 57;
const uint64_t GLS_BACK_STENCIL_OP_PASS_REPLACE		= 2ull << 57;
const uint64_t GLS_BACK_STENCIL_OP_PASS_INCR		= 3ull << 57;
const uint64_t GLS_BACK_STENCIL_OP_PASS_DECR		= 4ull << 57;
const uint64_t GLS_BACK_STENCIL_OP_PASS_INVERT		= 5ull << 57;
const uint64_t GLS_BACK_STENCIL_OP_PASS_INCR_WRAP	= 6ull << 57;
const uint64_t GLS_BACK_STENCIL_OP_PASS_DECR_WRAP	= 7ull << 57;
const uint64_t GLS_BACK_STENCIL_OP_PASS_BITS		= 7ull << 57;

const uint64_t GLS_SEPARATE_STENCIL					= GLS_BACK_STENCIL_OP_FAIL_BITS | GLS_BACK_STENCIL_OP_ZFAIL_BITS | GLS_BACK_STENCIL_OP_PASS_BITS;
const uint64_t GLS_STENCIL_OP_BITS					= GLS_STENCIL_OP_FAIL_BITS | GLS_STENCIL_OP_ZFAIL_BITS | GLS_STENCIL_OP_PASS_BITS | GLS_SEPARATE_STENCIL;
const uint64_t GLS_STENCIL_FRONT_OPS				= GLS_STENCIL_OP_FAIL_BITS | GLS_STENCIL_OP_ZFAIL_BITS | GLS_STENCIL_OP_PASS_BITS;
const uint64_t GLS_STENCIL_BACK_OPS					= GLS_SEPARATE_STENCIL;

const uint64_t GLS_DEPTH_BOUNDS_TEST				= 0x4000000000000000;
const uint64_t GLS_MIRROR_VIEW						= 0x8000000000000000;

const uint64_t GLS_DEFAULT = GLS_DEPTHFUNC_ALWAYS;

// using shorts for triangle indexes can save a significant amount of traffic, but
// to support the large models that renderBump loads, they need to be 32 bits
#if 1

#define GL_INDEX_TYPE		GL_UNSIGNED_INT
typedef int glIndex_t;

#else

#define GL_INDEX_TYPE		GL_UNSIGNED_SHORT
typedef short glIndex_t;

#endif

#endif