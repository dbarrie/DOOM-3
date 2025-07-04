
#ifndef _RENDERBACKEND_H_
#define _RENDERBACKEND_H_

#include <furygl.h>
#include <functional>

#include "../idlib/Lib.h"

#include "RenderCommon.h"
#include "Model.h"
#include "Material.h"

#define ID_FGL_CHECK(x) do { \
    FglResult ret = x; \
    if (ret != FGL_SUCCESS)\
        common->FatalError("FuryGL: %s - %s", FGL_ErrorString(ret), #x); \
} while(0)

#define ID_FGL_VALIDATE(x, msg) do { if (!(x)) common->FatalError("FuryGL: %s - %s", msg, #x); } while(0)

const char* FGL_ErrorString(FglResult ret);

struct fglContext_t
{
    vertCacheHandle_t				jointCacheHandle;
    idArray<idImage*, MAX_IMAGE_PARMS> imageParms;

    FglDevice device;
    FglRenderPass renderPass;
    FglCommandBuffer cmdbuf;
};

extern fglContext_t fglcontext;

typedef struct drawSurf_s drawSurf_t;
typedef struct drawInteraction_s drawInteraction_t;

class idRenderBackend
{
public:
    idRenderBackend();
    ~idRenderBackend();

    FglExtent2D GetSwapchainExtent() const { return m_swapchainExtent; }

    void Init();
    void Shutdown();

    void Clear();

    void GL_State(uint64_t state);

    void ExecuteBackEndCommands(const emptyCommand_t* cmds);

    void SetFullscreenTriangles(const srfTriangles_t& tri) { m_fullscreenTri = tri; }

private:

    void CreateSwapchain();
    void DestroySwapchain();

    void CreateRenderPass();

    void CreateFramebuffers();
    void DestroyFramebuffers();

    void StartFrame();
    void EndFrame();

    void DrawView(const void* data);
    void SwapBuffers(const void* data);
    void CopyRender(const void* data);

    void CopyFramebuffer(idImage* image, int x, int y, int width, int height);

    void DebugMarker(const char* fmt, ...);

    void DrawView();
    void BeginDrawingView();

    void SetScissor();

    void FillDepthBuffer(const drawSurf_t* surf);
    void SurfacesFillDepthBuffer(drawSurf_t** drawSurfs, int numDrawSurfs);

    int SurfacesDrawShaderPasses(drawSurf_t** drawSurfs, int numDrawSurfs);
    void RenderShaderPasses(const drawSurf_t* surf);

    void SetProgramEnvironment();
    void SetProgramEnvironmentSpace();
    void SetVertexColorParams(stageVertexColor_t svc);

    void DrawElementsWithCounters(const srfTriangles_t* tri);
    void DrawShadowElementsWithCounters(const srfTriangles_t* tri, int numIndexes, float zmin, float zmax);

    void BindVariableStageImage(const textureStage_t* texture, const float* shaderRegisters);
    void PrepareStageTexturing(const shaderStage_t *pStage, const drawSurf_t* surf);
    void LoadShaderTextureMatrix(const float* shaderRegisters, const textureStage_t* texture);

    void DrawLightScale();

    void SubmitInteraction(drawInteraction_t* din, std::function<void(const drawInteraction_t*)> DrawInteraction);
    void SetDrawInteraction(const shaderStage_t* surfaceStage, const float* surfaceRegs, idImage** image, idVec4 matrix[2], float color[4]);

    void CreateSingleDrawInteractions(const drawSurf_t* surf, std::function<void(const drawInteraction_t*)> DrawInteraction);

    void CreateDrawInteractions(const drawSurf_t* surf, uint64_t depthFunc, bool stencilTest);
    void DrawInteractions();

    void StencilShadowPass(const drawSurf_t* drawSurfs);
    void DrawShadow(const drawSurf_t* surf);

    void DrawSurfChainWithFunction(const drawSurf_t* drawSurfs, std::function<void(const drawSurf_t*)> DrawFunc);

private:
    FglInstance m_instance;
    FglPhysicalDevice m_physDevice;
    FglDevice m_device;

    uint64_t m_frameCounter;
    uint32_t m_currentFrameData;

    uint32_t m_currentSwapIndex;

    idArray<FglSemaphore, NUM_FRAME_DATA> m_acquireSemaphores;
    idArray<FglSemaphore, NUM_FRAME_DATA> m_renderCompleteSemaphores;

    FglCommandPool m_commandPool;
    idArray<FglCommandBuffer, NUM_FRAME_DATA> m_commandBuffers;
    idArray<FglFence, NUM_FRAME_DATA> m_commandBufferFences;

    FglSurface m_surface;
    FglSwapchain m_swapchain;
    idArray<FglImage, NUM_FRAME_DATA> m_swapchainImages;
    idArray<FglImageView, NUM_FRAME_DATA> m_swapchainViews;

    FglExtent2D m_swapchainExtent;
    idArray<FglFramebuffer, NUM_FRAME_DATA> m_framebuffers;

    FglRenderPass m_renderPass;
    FglRenderPass m_renderPassResume;

    idImage* m_pDepthImage;

    uint64_t m_glStateBits;


    srfTriangles_t m_fullscreenTri;
};

#endif