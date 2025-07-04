
#include "../idlib/precompiled.h"
#include "RenderBackend.h"
#include "tr_local.h"

#include "RenderProgs.h"
#include "Material.h"


fglContext_t fglcontext;

extern idCVar r_customWidth;
extern idCVar r_customHeight;

const char* FGL_ErrorString(FglResult ret)
{
    switch (ret)
    {
    case FGL_SUCCESS:               return "FGL_SUCCESS";
    case FGL_INCOMPLETE:            return "FGL_INCOMPLETE";
    case FGL_NOT_READY:             return "FGL_NOT_READY";
    case FGL_TIMEOUT:               return "FGL_TIMEOUT";
    case FGL_EVENT_SET:             return "FGL_EVENT_SET";
    case FGL_EVENT_RESET:           return "FGL_EVENT_RESET";
    case FGL_ERROR_FAILURE:         return "FGL_ERROR_FAILURE";
    case FGL_ERROR_UNSUPPORTED:     return "FGL_ERROR_UNSUPPORTED";
    case FGL_ERROR_INVALID:         return "FGL_ERROR_INVALID";
    case FGL_OUT_OF_HOST_MEMORY:    return "FGL_OUT_OF_HOST_MEMORY";
    case FGL_OUT_OF_DEVICE_MEMORY:  return "FGL_OUT_OF_DEVICE_MEMORY";
    case FGL_OUT_OF_POOL_MEMORY:    return "FGL_OUT_OF_POOL_MEMORY";
    case FGL_ERROR_UNIMPLEMENTED:   return "FGL_ERROR_UNIMPLEMENTED";
    }

    return "<UNKNOWN>";
}

/*
====================
R_GetModeInfo

r_mode is normally a small non-negative integer that
looks resolutions up in a table, but if it is set to -1,
the values from r_customWidth, amd r_customHeight
will be used instead.
====================
*/
typedef struct vidmode_s {
    const char* description;
    int         width, height;
} vidmode_t;

vidmode_t r_vidModes[] = {
    { "Mode  0: 640x480",		640,	480 },
    { "Mode  1: 800x600",		800,	600 },
    { "Mode  2: 1024x768",		1024,	768 },
    { "Mode  3: 1280x1024",		1280,	1024 },
    { "Mode  4: 720p",			1280,	720 },
    { "Mode  5: 1080p",			1920,	1080 },
    { "Mode  6: 2160p",			3840,	2160 },
};
static int	s_numVidModes = (sizeof(r_vidModes) / sizeof(r_vidModes[0]));

bool R_GetModeInfo(int* width, int* height, int mode) {
    vidmode_t* vm;

    if (mode < -1) {
        return false;
    }
    if (mode >= s_numVidModes) {
        return false;
    }

    if (mode == -1) {
        *width = r_customWidth.GetInteger();
        *height = r_customHeight.GetInteger();
        return true;
    }

    vm = &r_vidModes[mode];

    if (width) {
        *width = vm->width;
    }
    if (height) {
        *height = vm->height;
    }

    return true;
}

/*
==============
R_ListModes_f
==============
*/
static void R_ListModes_f(const idCmdArgs& args) {
    int i;

    common->Printf("\n");
    for (i = 0; i < s_numVidModes; i++) {
        common->Printf("%s\n", r_vidModes[i].description);
    }
    common->Printf("\n");
}

idRenderBackend::idRenderBackend()
{
    Clear();
}

idRenderBackend::~idRenderBackend()
{

}

void idRenderBackend::Clear()
{
    m_frameCounter = 0;
    m_currentFrameData = 0;
    m_instance = FGL_NULL_HANDLE;
    m_physDevice = FGL_NULL_HANDLE;
    m_device = FGL_NULL_HANDLE;
    m_commandPool = FGL_NULL_HANDLE;
    m_pDepthImage = nullptr;
}

static FglPhysicalDevice FindFuryPhysicalDevice(FglInstance inst)
{
    FglResult ret;
    FglPhysicalDevice device = FGL_NULL_HANDLE;

    uint32_t numDevices = 0;
    ret = fglEnumeratePhysicalDevices(inst, &numDevices, nullptr);
    if (ret == FGL_SUCCESS)
    {
        FglPhysicalDevice* pDevices = new FglPhysicalDevice[numDevices];

        ret = fglEnumeratePhysicalDevices(inst, &numDevices, pDevices);
        if (ret == FGL_SUCCESS)
        {
            if (numDevices)
            {
                device = pDevices[0];
            }
        }

        delete[] pDevices;
    }

    return device;
}

static FglResult FindDisplayMode(FglPhysicalDevice physicalDevice, FglExtent2D desiredExtent, FglBool desiredPixelDouble, FglDisplay* pDisplay, FglDisplayMode* pDisplayMode, FglExtent2D* pExtent, FglOffset2D* pWindowPos)
{
    FglResult ret;
    FglDisplay display = nullptr;
    FglDisplayMode displayMode = nullptr;
    FglExtent2D extent{};

    uint32_t displayPropCount = 0;
    FglDisplayProperties* pDisplayProps = nullptr;
    ret = fglGetPhysicalDeviceDisplayProperties(physicalDevice, &displayPropCount, nullptr);
    if (ret == FGL_SUCCESS && displayPropCount)
    {
        pDisplayProps = new FglDisplayProperties[displayPropCount];

        ret = fglGetPhysicalDeviceDisplayProperties(physicalDevice, &displayPropCount, pDisplayProps);
        if (ret == FGL_SUCCESS)
        {
            // Iterate over what we've got and check it all makes sense!
            display = pDisplayProps[0].display;
            *pWindowPos = pDisplayProps[0].displayPosition;
        }

        delete[] pDisplayProps;
    }

    if (display)
    {
        uint32_t displayModeCount = 0;
        FglDisplayModeProperties* pDisplayModeProps = nullptr;
        ret = fglGetDisplayModeProperties(physicalDevice, display, &displayModeCount, nullptr);
        if (ret == FGL_SUCCESS)
        {
            pDisplayModeProps = new FglDisplayModeProperties[displayModeCount];

            ret = fglGetDisplayModeProperties(physicalDevice, display, &displayModeCount, pDisplayModeProps);
            if (ret == FGL_SUCCESS)
            {
                for (uint32_t i = 0; i < displayModeCount; ++i)
                {
                    FglDisplayModeProperties* pProps = &pDisplayModeProps[i];

                    if (pProps->parameters.visibleRegion.width == desiredExtent.width &&
                        pProps->parameters.visibleRegion.height == desiredExtent.height &&
                        pProps->parameters.pixelDouble == desiredPixelDouble)
                    {
                        displayMode = pProps->displayMode;
                        if (pProps->parameters.pixelDouble)
                            extent = FglExtent2D{ pProps->parameters.visibleRegion.width / 2, pProps->parameters.visibleRegion.height / 2 };
                        else
                            extent = pProps->parameters.visibleRegion;
                    }
                }
            }

            delete[] pDisplayModeProps;
        }
    }

    *pDisplay = display;
    *pDisplayMode = displayMode;
    *pExtent = extent;

    return ret;
}

void idRenderBackend::CreateSwapchain()
{
    FglResult ret;

    int vidWidth, vidHeight;
    R_GetModeInfo(&vidWidth, &vidHeight, r_mode.GetInteger());

    FglExtent2D desiredExtent{ (uint32_t)vidWidth, (uint32_t)vidHeight };

    FglDisplay display;
    FglDisplayMode displayMode;
    FglExtent2D extent;
    FglOffset2D windowPos;
    ret = FindDisplayMode(m_physDevice, desiredExtent, r_pixelDouble.GetBool(), &display, &displayMode, &extent, &windowPos);
    if (ret != FGL_SUCCESS)
        common->FatalError("FindDisplayMode failed for desired extent %dx%d", desiredExtent.width, desiredExtent.height);

    FglPresentMode presentMode = FGL_PRESENT_MODE_FIFO;
    if (r_swapInterval.GetInteger() < 1)
        presentMode = FGL_PRESENT_MODE_MAILBOX;

    
    FglDisplaySurfaceCreateInfo surfaceInfo{};
    surfaceInfo.displayMode = displayMode;
    surfaceInfo.imageExtent = extent;
    ID_FGL_CHECK(fglCreateDisplaySurface(m_instance, &surfaceInfo, nullptr, &m_surface));

    FglSwapchainCreateInfo swapchainInfo{};
    swapchainInfo.surface = m_surface;
    swapchainInfo.minImageCount = NUM_FRAME_DATA;
    swapchainInfo.imageFormat = FGL_FORMAT_R8G8B8A8_UNORM;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.presentMode = presentMode;
    ID_FGL_CHECK(fglCreateSwapchain(m_device, &swapchainInfo, nullptr, &m_swapchain));

    m_swapchainExtent = extent;

    uint32_t numImages = 0;
    ID_FGL_CHECK(fglGetSwapchainImages(m_device, m_swapchain, &numImages, nullptr));
    ID_FGL_VALIDATE(numImages > 0, "No swapchain images?");

    ID_FGL_CHECK(fglGetSwapchainImages(m_device, m_swapchain, &numImages, m_swapchainImages.Ptr()));

    for (uint32_t i = 0; i < numImages; ++i)
    {
        FglImageViewCreateInfo viewInfo{};
        viewInfo.image = m_swapchainImages[i];
        viewInfo.format = FGL_FORMAT_R8G8B8A8_UNORM;
        viewInfo.viewType = FGL_IMAGE_VIEW_TYPE_2D;
        ID_FGL_CHECK(fglCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainViews[i]));
    }
}

void idRenderBackend::DestroySwapchain()
{
    for (uint32_t i = 0; i < NUM_FRAME_DATA; ++i)
    {
        fglDestroyImageView(m_device, m_swapchainViews[i], nullptr);
        m_swapchainViews[i] = FGL_NULL_HANDLE;
    }

    fglDestroySwapchain(m_device, m_swapchain, nullptr);
    fglDestroySurface(m_instance, m_surface, nullptr);
}

void idRenderBackend::CreateRenderPass()
{
    std::array<FglAttachmentDescription, 2> attachments{};

    FglAttachmentDescription& colorAttachment = attachments[0];
    colorAttachment.format = FGL_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.loadOp = FGL_ATTACHMENT_LOAD_OP_CLEAR;// FGL_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = FGL_ATTACHMENT_STORE_OP_STORE;

    FglAttachmentDescription& depthAttachment = attachments[1];
    depthAttachment.format = FGL_FORMAT_D24_UNORM_S8_UINT;
    depthAttachment.loadOp = FGL_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = FGL_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = FGL_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = FGL_ATTACHMENT_STORE_OP_DONT_CARE;

    FglAttachmentReference colorRef{};
    colorRef.attachment = 0;

    FglAttachmentReference depthRef{};
    depthRef.attachment = 1;

    FglSubpassDescription subpassDesc{};
    subpassDesc.pColorAttachment = &colorRef;
    subpassDesc.pDepthStencilAttachment = &depthRef;

    FglRenderPassCreateInfo rpInfo{};
    rpInfo.attachmentCount = (uint32_t)attachments.size();
    rpInfo.pAttachments = attachments.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpassDesc;
    ID_FGL_CHECK(fglCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass));

    fglcontext.renderPass = m_renderPass;

    attachments[0].loadOp = FGL_ATTACHMENT_LOAD_OP_LOAD;

    ID_FGL_CHECK(fglCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPassResume));
}

void idRenderBackend::CreateFramebuffers()
{
    m_pDepthImage = globalImages->ScratchImage("_viewDepth", m_swapchainExtent.width, m_swapchainExtent.height, FGL_FORMAT_D24_UNORM_S8_UINT);
    if (!m_pDepthImage)
        common->FatalError("No depth image??");

    std::array<FglImageView, 2> attachViews;

    attachViews[1] = m_pDepthImage->GetView();

    FglFramebufferCreateInfo fbInfo{};
    fbInfo.attachmentCount = (uint32_t)attachViews.size();
    fbInfo.pAttachments = attachViews.data();
    fbInfo.width = m_swapchainExtent.width;
    fbInfo.height = m_swapchainExtent.height;
    fbInfo.renderPass = m_renderPass;

    for (int i = 0; i < NUM_FRAME_DATA; ++i)
    {
        attachViews[0] = m_swapchainViews[i];
        ID_FGL_CHECK(fglCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffers[i]));
    }
}

void idRenderBackend::DestroyFramebuffers()
{
    for (int i = 0; i < NUM_FRAME_DATA; ++i)
    {
        fglDestroyFramebuffer(m_device, m_framebuffers[i], nullptr);
        m_framebuffers[i] = FGL_NULL_HANDLE;
    }
}

void idRenderBackend::Init()
{
    common->Printf("--- idRenderBackend::Init ---\n");

    ID_FGL_CHECK(fglCreateInstance(nullptr, &m_instance));

    m_physDevice = FindFuryPhysicalDevice(m_instance);
    if (m_physDevice == FGL_NULL_HANDLE)
        common->FatalError("Failed to find physical FuryGpu device!");

    ID_FGL_CHECK(fglCreateDevice(m_physDevice, nullptr, &m_device));
    fglcontext.device = m_device;

    FglDeviceDebugFlags flags = 0;
    flags |= FGL_DEVICE_DEBUG_LOG_COMMAND_BUFFERS_BIT;
    //flags |= FGL_DEVICE_DEBUG_SKIP_SHADER_OPTIMIZATION_BIT;
    fglDeviceSetDebugFlags(m_device, flags);

    // Create semaphores
    {
        FglSemaphoreCreateInfo semInfo{};
        for (int i = 0; i < NUM_FRAME_DATA; ++i)
        {
            ID_FGL_CHECK(fglCreateSemaphore(m_device, &semInfo, nullptr, &m_acquireSemaphores[i]));
            ID_FGL_CHECK(fglCreateSemaphore(m_device, &semInfo, nullptr, &m_renderCompleteSemaphores[i]));
        }
    }

    // Create command pool
    {
        FglCommandPoolCreateInfo cpInfo{};
        cpInfo.flags = FGL_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ID_FGL_CHECK(fglCreateCommandPool(m_device, &cpInfo, nullptr, &m_commandPool));
    }

    // Command buffers
    {
        FglCommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = m_commandPool;
        allocInfo.commandBufferCount = NUM_FRAME_DATA;

        ID_FGL_CHECK(fglAllocateCommandBuffers(m_device, &allocInfo, nullptr, m_commandBuffers.Ptr()));

        FglFenceCreateInfo fenceInfo{};
        for (int i = 0; i < NUM_FRAME_DATA; ++i)
        {
            ID_FGL_CHECK(fglCreateFence(m_device, &fenceInfo, nullptr, &m_commandBufferFences[i]));
        }
    }

    int vidWidth, vidHeight;
    R_GetModeInfo(&vidWidth, &vidHeight, r_mode.GetInteger());

    glimpParms_t parms;
    parms.displayHz = 60;
    parms.fullScreen = true;
    parms.width = vidWidth;
    parms.height = vidHeight;
    parms.multiSamples = 0;
    parms.stereo = false;
    Fgl_Init(parms);

    CreateSwapchain();

    CreateRenderPass();

    CreateFramebuffers();

    renderProgManager.Init();

    // in case we had an error while doing a tiled rendering
    tr.viewportOffset[0] = 0;
    tr.viewportOffset[1] = 0;


    // input and sound systems need to be tied to the new window
    Sys_InitInput();
    soundSystem->InitHW();

    glConfig.isInitialized = true;

    // allocate the vertex array range or vertex objects
    vertexCache.Init(16);

    // select which renderSystem we are going to use
    r_renderer.SetModified();
    tr.SetBackEndRenderer();

    // allocate the frame data, which may be more if smp is enabled
    R_InitFrameData();

    cmdSystem->AddCommand("listModes", R_ListModes_f, CMD_FL_RENDERER, "lists all video modes");

}

void idRenderBackend::Shutdown()
{
    fglDestroyCommandPool(m_device, m_commandPool, nullptr);

    for (int i = 0; i < NUM_FRAME_DATA; ++i)
    {
        fglDestroySemaphore(m_device, m_acquireSemaphores[i], nullptr);
        fglDestroySemaphore(m_device, m_renderCompleteSemaphores[i], nullptr);
    }

    fglDestroyDevice(m_device, nullptr);
    fglDestroyInstance(m_instance, nullptr);

    Clear();
}

/*
=============
RB_DrawView
=============
*/
void idRenderBackend::DrawView(const void* data)
{
    const drawSurfsCommand_t* cmd;

    cmd = (const drawSurfsCommand_t*)data;

    backEnd.viewDef = cmd->viewDef;

    // we will need to do a new copyTexSubImage of the screen
    // when a SS_POST_PROCESS material is used
    backEnd.currentRenderCopied = false;

    // if there aren't any drawsurfs, do nothing
    if (!backEnd.viewDef->numDrawSurfs) {
        return;
    }

    // skip render bypasses everything that has models, assuming
    // them to be 3D views, but leaves 2D rendering visible
    if (r_skipRender.GetBool() && backEnd.viewDef->viewEntitys) {
        return;
    }

    // skip render context sets the wgl context to NULL,
    // which should factor out the API cost, under the assumption
    // that all gl calls just return if the context isn't valid
    if (r_skipRenderContext.GetBool() && backEnd.viewDef->viewEntitys) {
        //GLimp_DeactivateContext();
    }

    backEnd.pc.c_surfaces += backEnd.viewDef->numDrawSurfs;

    RB_ShowOverdraw();

    // render the scene, jumping to the hardware specific interaction renderers
    DrawView();

    // restore the context for 2D drawing if we were stubbing it out
    if (r_skipRenderContext.GetBool() && backEnd.viewDef->viewEntitys) {
        //GLimp_ActivateContext();
        //RB_SetDefaultGLState();
    }
}


void idRenderBackend::SwapBuffers(const void* data)
{
    // texture swapping test
    if (r_showImages.GetInteger() != 0) {
        RB_ShowImages();
    }

    // force a gl sync if requested
    if (r_finish.GetBool()) {
        //	qglFinish();
    }

    RB_LogComment("***************** RB_SwapBuffers *****************\n\n\n");

    // don't flip if drawing to front buffer
    if (!r_frontBuffer.GetBool()) {
        //GLimp_SwapBuffers();
    }
}

/*
====================
RB_ExecuteBackEndCommands

This function will be called syncronously if running without
smp extensions, or asyncronously by another thread.
====================
*/
int		backEndStartTime, backEndFinishTime;
void idRenderBackend::ExecuteBackEndCommands(const emptyCommand_t* cmds)
{
    // r_debugRenderToTexture
    int	c_draw3d = 0, c_draw2d = 0, c_setBuffers = 0, c_swapBuffers = 0, c_copyRenders = 0;

    if (cmds->commandId == RC_NOP && !cmds->next)
        return;

    backEndStartTime = Sys_Milliseconds();

    //RB_SetDefaultGLState();

    // upload any image loads that have completed
    globalImages->CompleteBackgroundImageLoads();

    m_glStateBits = 0;

    StartFrame();

    for (; cmds; cmds = (const emptyCommand_t*)cmds->next)
    {
        switch (cmds->commandId)
        {
        case RC_NOP:
            break;
        case RC_DRAW_VIEW:
            DrawView(cmds);
            if (((const drawSurfsCommand_t*)cmds)->viewDef->viewEntitys) {
                c_draw3d++;
            }
            else {
                c_draw2d++;
            }
            break;
        case RC_SET_BUFFER:
            //RB_SetBuffer(cmds);
            c_setBuffers++;
            break;
        case RC_SWAP_BUFFERS:
            SwapBuffers(cmds);
            c_swapBuffers++;
            break;
        case RC_COPY_RENDER:
            CopyRender(cmds);
            c_copyRenders++;
            break;
        default:
            common->Error("ExecuteBackEndCommands: bad commandId");
            break;
        }
    }

    EndFrame();

    // go back to the default texture so the editor doesn't mess up a bound image
    //qglBindTexture(GL_TEXTURE_2D, 0);
    //backEnd.glState.tmu[0].current2DMap = -1;

    // stop rendering on this thread
    backEndFinishTime = Sys_Milliseconds();
    backEnd.pc.msec = backEndFinishTime - backEndStartTime;

    if (r_debugRenderToTexture.GetInteger() == 1)
    {
        common->Printf("3d: %i, 2d: %i, SetBuf: %i, SwpBuf: %i, CpyRenders: %i, CpyFrameBuf: %i\n", c_draw3d, c_draw2d, c_setBuffers, c_swapBuffers, c_copyRenders, backEnd.c_copyFrameBuffer);
        backEnd.c_copyFrameBuffer = 0;
    }
}


void idRenderBackend::StartFrame()
{
    ID_FGL_CHECK(fglAcquireNextImage(m_device, m_swapchain, UINT64_MAX, m_acquireSemaphores[m_currentFrameData], FGL_NULL_HANDLE, &m_currentSwapIndex));

    renderProgManager.StartFrame();

    FglCommandBuffer cmdbuf = m_commandBuffers[m_currentFrameData];

    fglcontext.cmdbuf = cmdbuf;

    ID_FGL_CHECK(fglResetCommandBuffer(cmdbuf, FGL_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT));

    FglCommandBufferBeginInfo beginInfo{};
    ID_FGL_CHECK(fglBeginCommandBuffer(cmdbuf, &beginInfo));

    fglCmdReset(cmdbuf);

    FglClearValue clearValues[2];
    clearValues[0].color = { 0.0f, 0.0f, 0.0f, 0.0f };
    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 128;

    FglRenderPassBeginInfo rpInfo{};
    rpInfo.framebuffer = m_framebuffers[m_currentFrameData];
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;
    rpInfo.renderArea.offset = FglOffset2D{ 0, 0 };
    rpInfo.renderArea.extent = m_swapchainExtent;
    rpInfo.renderPass = m_renderPass;
    fglCmdBeginRenderPass(cmdbuf, &rpInfo);
}

void idRenderBackend::EndFrame()
{
    FglCommandBuffer cmdbuf = m_commandBuffers[m_currentFrameData];

    fglCmdEndRenderPass(cmdbuf);

    fglEndCommandBuffer(cmdbuf);

    FglSubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdbuf;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_acquireSemaphores[m_currentFrameData];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderCompleteSemaphores[m_currentFrameData];
    ID_FGL_CHECK(fglSubmit(m_device, &submitInfo, m_commandBufferFences[m_currentFrameData]));

    FglPresentInfo presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderCompleteSemaphores[m_currentFrameData];
    presentInfo.swapchain = m_swapchain;
    presentInfo.imageIndex = m_currentSwapIndex;
    ID_FGL_CHECK(fglPresent(m_device, &presentInfo));

    static bool captureColor = false;
    static bool captureDepth = false;
    static uint32_t* pColorData = nullptr;
    static uint32_t* pDepthData = nullptr;
    static uint32_t memSize = 0;

    if (captureColor || captureDepth)
    {
        fglDeviceWaitIdle(m_device);

        if (!memSize)
        {
            FglImageSubresource rsrc{};
            rsrc.arrayLayer = 0;
            rsrc.mipLevel = 0;

            FglSubresourceLayout layout;
            fglGetImageSubresourceLayout(m_device, m_swapchainImages[0], &rsrc, &layout);

            memSize = layout.size;
        }

        if (captureColor && !pColorData)
        {
            pColorData = (uint32_t*)Mem_Alloc(memSize);
        }

        if (captureDepth && !pDepthData)
        {
            pDepthData = (uint32_t*)Mem_Alloc(memSize);
        }

        FglDeviceCopyImageToHostInfo copy[2];
        uint32_t copyIdx = 0;

        if (captureColor)
        {
            copy[copyIdx].image = m_swapchainImages[m_currentSwapIndex];
            copy[copyIdx].subresource.arrayLayer = 0;
            copy[copyIdx].subresource.mipLevel = 0;
            copy[copyIdx].pHostMemory = pColorData;
            ++copyIdx;
        }

        if (captureDepth)
        {
            copy[copyIdx].image = m_pDepthImage->m_image;
            copy[copyIdx].subresource.arrayLayer = 0;
            copy[copyIdx].subresource.mipLevel = 0;
            copy[copyIdx].pHostMemory = pDepthData;
            ++copyIdx;
        }

        fglDeviceCopyImageToHost(m_device, copyIdx, copy);

        captureColor = false;
        captureDepth = false;
    }

    m_currentFrameData = ++m_frameCounter % NUM_FRAME_DATA;
}

void idRenderBackend::CopyRender(const void* data)
{
    const copyRenderCommand_t* cmd;

    cmd = (const copyRenderCommand_t*)data;
    if (r_skipCopyTexture.GetBool()) {
        return;
    }

    RB_LogComment("***************** RB_CopyRender *****************\n");

    if (cmd->image) {
        CopyFramebuffer(cmd->image,
            backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
            backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
            backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1);
    }
}

void idRenderBackend::CopyFramebuffer(idImage* image, int x, int y, int width, int height)
{
    // End current render pass, which will flush the GPU to finish rendering
    fglCmdEndRenderPass(fglcontext.cmdbuf);

    // Copy the framebuffer data to a texture that can be used for post processing
    image->CopyFramebuffer(m_swapchainImages[m_currentFrameData], m_swapchainExtent.width, m_swapchainExtent.height);

    // Kick off a new render pass, rendering to the same framebuffer we just copied from
    FglRenderPassBeginInfo rpInfo{};
    rpInfo.framebuffer = m_framebuffers[m_currentFrameData];
    rpInfo.renderArea.offset = FglOffset2D{ 0, 0 };
    rpInfo.renderArea.extent = m_swapchainExtent;
    rpInfo.renderPass = m_renderPassResume;
    fglCmdBeginRenderPass(fglcontext.cmdbuf, &rpInfo);
}

void idRenderBackend::DebugMarker(const char* fmt, ...)
{
    va_list va;

    char str[256];
    va_start(va, fmt);
    vsprintf(str, fmt, va);
    va_end(va);

    FglDebugMarkerInfo marker{};
    marker.pMarkerName = str;
    fglCmdDebugMarkerInsert(fglcontext.cmdbuf, &marker);
}

void idRenderBackend::GL_State(uint64_t state)
{
    m_glStateBits = state | (m_glStateBits & (GLS_DEPTHMASK | GLS_DEPTHTEST_DISABLE));
}

//void idRenderBackend::GL_SetDefaultState

void idRenderBackend::BeginDrawingView()
{
    FglCommandBuffer cmdbuf = m_commandBuffers[m_currentFrameData];

    // set the window clipping
    FglViewport viewport{};
    viewport.x = tr.viewportOffset[0] + backEnd.viewDef->viewport.x1;
    viewport.y = tr.viewportOffset[1] + backEnd.viewDef->viewport.y1;
    viewport.width = backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1;
    viewport.height = backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    fglCmdSetViewport(cmdbuf, &viewport);

    // the scissor may be smaller than the viewport for subviews
    FglRect2D scissor{};
    scissor.offset.x = tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1;
    scissor.offset.y = tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1;
    scissor.extent.width = backEnd.viewDef->scissor.x2 + 1 - backEnd.viewDef->scissor.x1;
    scissor.extent.height = backEnd.viewDef->scissor.y2 + 1 - backEnd.viewDef->scissor.y1;
    fglCmdSetScissor(cmdbuf, &scissor);
    backEnd.currentScissor = backEnd.viewDef->scissor;

    // ensures that depth writes are enabled for the depth clear
    GL_State(GLS_DEFAULT);

    // If only doing 2D rendering, disable depth testing
    if (!backEnd.viewDef->viewEntitys)
    {
        GL_State(GLS_DEPTHTEST_DISABLE);
    }

    /*
    FglClearValue clearValues[2];
    clearValues[0].color = { 0.2f, 0.2f, 0.2f, 0.0f };
    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 128;

    FglRenderPassBeginInfo rpInfo{};
    rpInfo.framebuffer = m_framebuffers[m_currentFrameData];
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;
    rpInfo.renderArea.offset = FglOffset2D{ 0, 0 };
    rpInfo.renderArea.extent = m_swapchainExtent;
    rpInfo.renderPass = m_renderPass;
    fglCmdBeginRenderPass(cmdbuf, &rpInfo);
    */

    backEnd.glState.faceCulling = -1;		// force face culling to set next time
    GL_State(GLS_CULL_FRONTSIDED);
}

void idRenderBackend::DrawView()
{
    drawSurf_t** drawSurfs;
    int			numDrawSurfs;

    RB_LogComment("---------- RB_STD_DrawView ----------\n");

    DebugMarker("DrawView");

    backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;

    drawSurfs = (drawSurf_t**)&backEnd.viewDef->drawSurfs[0];
    numDrawSurfs = backEnd.viewDef->numDrawSurfs;

    // clear the z buffer, set the projection matrix, etc
    BeginDrawingView();

    // decide how much overbrighting we are going to do
    RB_DetermineLightScale();

    // fill the depth buffer and clear color buffer to black except on
    // subviews
    SurfacesFillDepthBuffer(drawSurfs, numDrawSurfs);

    // main light renderer
    DrawInteractions();

    // disable stencil shadow test
    //qglStencilFunc(GL_ALWAYS, 128, 255);

    // uplight the entire screen to crutch up not having better blending range
    DrawLightScale();

    // now draw any non-light dependent shading passes
    int	processed = SurfacesDrawShaderPasses(drawSurfs, numDrawSurfs);

    // fob and blend lights
    //RB_STD_FogAllLights();

    // now draw any post-processing effects using _currentRender
    if (processed < numDrawSurfs) {
        SurfacesDrawShaderPasses(drawSurfs + processed, numDrawSurfs - processed);
    }

    //RB_RenderDebugTools(drawSurfs, numDrawSurfs);

    //FinishDrawingView();
}

void idRenderBackend::SetScissor()
{
    FglRect2D scissor{};
    scissor.offset.x = backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1;
    scissor.offset.y = backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1;
    scissor.extent.width = backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1;
    scissor.extent.height = backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1;
    fglCmdSetScissor(fglcontext.cmdbuf, &scissor);
}

static void Transpose(const float* pSrc, float* pDst)
{
    pDst[0] = pSrc[0];  pDst[1] = pSrc[4];  pDst[2] = pSrc[8];  pDst[3] = pSrc[12];
    pDst[4] = pSrc[1];  pDst[5] = pSrc[5];  pDst[6] = pSrc[9];  pDst[7] = pSrc[13];
    pDst[8] = pSrc[2];  pDst[9] = pSrc[6];  pDst[10] = pSrc[10];  pDst[11] = pSrc[14];
    pDst[12] = pSrc[3];  pDst[13] = pSrc[7];  pDst[14] = pSrc[11];  pDst[15] = pSrc[15];
};

/*
==================
RB_T_FillDepthBuffer
==================
*/
void idRenderBackend::FillDepthBuffer(const drawSurf_t* surf)
{
    int			stage;
    const idMaterial* shader;
    const shaderStage_t* pStage;
    const float* regs;
    float		color[4];
    const srfTriangles_t* tri;

    tri = surf->geo;
    shader = surf->material;

    // update the clip plane if needed
    /*
    if (backEnd.viewDef->numClipPlanes && surf->space != backEnd.currentSpace) {
        GL_SelectTexture(1);

        idPlane	plane;

        R_GlobalPlaneToLocal(surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane);
        plane[3] += 0.5;	// the notch is in the middle
        qglTexGenfv(GL_S, GL_OBJECT_PLANE, plane.ToFloatPtr());
        GL_SelectTexture(0);
    }
    */

    if (!shader->IsDrawn()) {
        return;
    }

    // some deforms may disable themselves by setting numIndexes = 0
    if (!tri->numIndexes) {
        return;
    }

    // translucent surfaces don't put anything in the depth buffer and don't
    // test against it, which makes them fail the mirror clip plane operation
    if (shader->Coverage() == MC_TRANSLUCENT) {
        return;
    }

    if (!tri->ambientCache) {
        common->Printf("RB_T_FillDepthBuffer: !tri->ambientCache\n");
        return;
    }

    // get the expressions for conditionals / color / texcoords
    regs = surf->shaderRegisters;

    // if all stages of a material have been conditioned off, don't do anything
    for (stage = 0; stage < shader->GetNumStages(); stage++) {
        pStage = shader->GetStage(stage);
        // check the stage enable condition
        if (regs[pStage->conditionRegister] != 0) {
            break;
        }
    }
    if (stage == shader->GetNumStages()) {
        return;
    }

    //GL_State(GLS_DEFAULT);

    int surfGLState = 0;

    // set polygon offset if necessary
    /*
    if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
        surfGLState |= GLS_POLYGON_OFFSET;
        qglEnable(GL_POLYGON_OFFSET_FILL);
        qglPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
    }
    */

    DebugMarker("FillDepthBuffer %s", shader->GetName());

    // subviews will just down-modulate the color buffer by overbright
    if (shader->GetSort() == SS_SUBVIEW) {
        surfGLState |= (GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS);
        color[0] =
            color[1] =
            color[2] = (1.0 / backEnd.overBright);
        color[3] = 1;
    }
    else {
        // others just draw black
        color[0] = 0;
        color[1] = 0;
        color[2] = 0;
        color[3] = 1;
    }

    bool drawSolid = false;

    if (shader->Coverage() == MC_OPAQUE) {
        drawSolid = true;
    }

    // we may have multiple alpha tested stages
    if (shader->Coverage() == MC_PERFORATED) {
        // if the only alpha tested stages are condition register omitted,
        // draw a normal opaque surface
        bool	didDraw = false;

        //qglEnable(GL_ALPHA_TEST);
        // perforated surfaces may have multiple alpha tested stages
        for (stage = 0; stage < shader->GetNumStages(); stage++) {
            pStage = shader->GetStage(stage);

            if (!pStage->hasAlphaTest) {
                continue;
            }

            // check the stage enable condition
            if (regs[pStage->conditionRegister] == 0) {
                continue;
            }

            //GL_State(GLS_DEFAULT);

            // if we at least tried to draw an alpha tested stage,
            // we won't draw the opaque surface
            didDraw = true;

            // set the alpha modulate
            color[3] = regs[pStage->color.registers[3]];

            // skip the entire stage if alpha would be black
            if (color[3] <= 0) {
                continue;
            }

            int stageGLState = surfGLState;

            GL_State(stageGLState);

            renderProgManager.SetRenderParm(RENDERPARM_COLOR, color);
            const float alphaTestValue = regs[pStage->alphaTestRegister];
            const float alphaTest[4] = { alphaTestValue, alphaTestValue, alphaTestValue, alphaTestValue };
            renderProgManager.SetRenderParm(RENDERPARM_ALPHA_TEST, alphaTest);

            renderProgManager.BindProgram(BUILTIN_TEXTURE_VERTEXCOLOR_ALPHATEST);

            SetVertexColorParams(SVC_IGNORE);

            // bind the texture
            pStage->texture.image->Bind(0);

            // set texture matrix and texGens
            PrepareStageTexturing(pStage, surf);

            // draw it
            DrawElementsWithCounters(tri);
        }
        //qglDisable(GL_ALPHA_TEST);
        if (!didDraw) {
            drawSolid = true;
        }
    }

    // draw the entire surface solid
    if (drawSolid) {
        if (shader->GetSort() == SS_SUBVIEW)
        {
            renderProgManager.SetRenderParm(RENDERPARM_COLOR, color);
            renderProgManager.BindProgram(BUILTIN_COLOR);
            GL_State(surfGLState);
        }
        else
        {
            // TODO: if (jointcache) bind skinned
            renderProgManager.BindProgram(BUILTIN_DEPTH);
            GL_State(surfGLState | GLS_ALPHAMASK);
        }
        
        //globalImages->whiteImage->Bind(0);

        // draw it
        DrawElementsWithCounters(tri);
    }


    /*
    // reset polygon offset
    if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
        qglDisable(GL_POLYGON_OFFSET_FILL);
    }
    */

    // reset blending
    if (shader->GetSort() == SS_SUBVIEW) {
        GL_State(GLS_DEPTHFUNC_LESS);
    }
}

/*
=====================
RB_STD_FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void idRenderBackend::SurfacesFillDepthBuffer(drawSurf_t** drawSurfs, int numDrawSurfs)
{
    // if we are just doing 2D rendering, no need to fill the depth buffer
    if (!backEnd.viewDef->viewEntitys) {
        return;
    }

    RB_LogComment("---------- RB_STD_FillDepthBuffer ----------\n");
    DebugMarker("SurfacesFillDepthBuffer");

    // enable the second texture for mirror plane clipping if needed
    /*
    if (backEnd.viewDef->numClipPlanes) {
        GL_SelectTexture(1);
        globalImages->alphaNotchImage->Bind();
        qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
        qglEnable(GL_TEXTURE_GEN_S);
        qglTexCoord2f(1, 0.5);
    }
    */

    // the first texture will be used for alpha tested surfaces
    //GL_SelectTexture(0);
    //qglEnableClientState(GL_TEXTURE_COORD_ARRAY);

    // decal surfaces may enable polygon offset
    //qglPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat());

    GL_State(GLS_DEPTHFUNC_LESS);

    // Enable stencil test if we are going to be using it for shadows.
    // If we didn't do this, it would be legal behavior to get z fighting
    // from the ambient pass and the light passes.
    //qglEnable(GL_STENCIL_TEST);
    //qglStencilFunc(GL_ALWAYS, 1, 255);

    backEnd.currentSpace = NULL;

    for (int i = 0; i < numDrawSurfs; i++) {
        drawSurf_t* drawSurf = drawSurfs[i];

        // change the matrix if needed
        if (drawSurf->space != backEnd.currentSpace) {
            backEnd.currentSpace = drawSurf->space;

            SetProgramEnvironmentSpace();
        }

        if (drawSurf->space->weaponDepthHack) {
            //RB_EnterWeaponDepthHack();
        }

        if (drawSurf->space->modelDepthHack != 0.0f) {
            //RB_EnterModelDepthHack(drawSurf->space->modelDepthHack);
        }

        // change the scissor if needed
        if (r_useScissor.GetBool() && !backEnd.currentScissor.Equals(drawSurf->scissorRect)) {
            backEnd.currentScissor = drawSurf->scissorRect;

            SetScissor();
        }

        // render it
        FillDepthBuffer(drawSurf);

        if (drawSurf->space->weaponDepthHack || drawSurf->space->modelDepthHack != 0.0f) {
            //RB_LeaveDepthHack();
        }
    }

    /*
    if (backEnd.viewDef->numClipPlanes) {
        GL_SelectTexture(1);
        globalImages->BindNull();
        qglDisable(GL_TEXTURE_GEN_S);
        GL_SelectTexture(0);
    }
    */
}

void idRenderBackend::SetProgramEnvironment()
{
    float	parm[4];
    int		pot;

    auto smallestPowerOfTwo = [](int size)
    {
        int pot = 1;
        while (pot < size)
            pot <<= 1;
        return pot;
    };

    // screen power of two correction factor, assuming the copy to _currentRender
    // also copied an extra row and column for the bilerp
    int	 w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
    pot = smallestPowerOfTwo((int)globalImages->currentRenderImage->m_extent.width);
    parm[0] = (float)w / pot;

    int	 h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
    pot = smallestPowerOfTwo((int)globalImages->currentRenderImage->m_extent.height);
    parm[1] = (float)h / pot;

    parm[2] = 0;
    parm[3] = 1;
    renderProgManager.SetRenderParm(RENDERPARM_SCREENCORRECTIONFACTOR, parm);

    // window coord to 0.0 to 1.0 conversion
    parm[0] = 1.0 / w;
    parm[1] = 1.0 / h;
    parm[2] = 0;
    parm[3] = 1;
    renderProgManager.SetRenderParm(RENDERPARM_WINDOWCOORD, parm);

    //
    // set eye position in global space
    //
    parm[0] = backEnd.viewDef->renderView.vieworg[0];
    parm[1] = backEnd.viewDef->renderView.vieworg[1];
    parm[2] = backEnd.viewDef->renderView.vieworg[2];
    parm[3] = 1.0;
    renderProgManager.SetRenderParm(RENDERPARM_VIEWORIGIN, parm);
}

void idRenderBackend::SetVertexColorParams(stageVertexColor_t svc)
{
    static const float zero[4] = { 0, 0, 0, 0 };
    static const float one[4] = { 1, 1, 1, 1 };
    static const float negOne[4] = { -1, -1, -1, -1 };

    switch (svc)
    {
    case SVC_IGNORE:
        renderProgManager.SetRenderParm(RENDERPARM_VERTEXCOLOR_MODULATE, zero);
        renderProgManager.SetRenderParm(RENDERPARM_VERTEXCOLOR_ADD, one);
        break;
    case SVC_MODULATE:
        renderProgManager.SetRenderParm(RENDERPARM_VERTEXCOLOR_MODULATE, one);
        renderProgManager.SetRenderParm(RENDERPARM_VERTEXCOLOR_ADD, zero);
        break;
    case SVC_INVERSE_MODULATE:
        renderProgManager.SetRenderParm(RENDERPARM_VERTEXCOLOR_MODULATE, negOne);
        renderProgManager.SetRenderParm(RENDERPARM_VERTEXCOLOR_ADD, one);
        break;
    }
}

/*
==================
RB_SetProgramEnvironmentSpace

Sets variables related to the current space that can be used by all vertex programs
==================
*/
void idRenderBackend::SetProgramEnvironmentSpace()
{
    const struct viewEntity_s* space = backEnd.currentSpace;
    float	parm[4];

    // set eye position in local space
    R_GlobalPointToLocal(space->modelMatrix, backEnd.viewDef->renderView.vieworg, *(idVec3*)parm);
    parm[3] = 1.0;
    renderProgManager.SetRenderParm(RENDERPARM_LOCALVIEWORIGIN, parm);

    // All matrices need to be transposed due to some fuckery in the shader compiler;
    // the different rows are passed in as individual vectors, and the shader compiler
    // assumes that they can just be treated as such. However, when passing data in
    // as a full float4x4, it gets loaded from memory transposed
    
    float transpose[16];

    // we need the model matrix without it being combined with the view matrix
    // so we can transform local vectors to global coordinates
    Transpose(space->modelMatrix, transpose);
    renderProgManager.SetRenderParms(RENDERPARM_MODELMATRIX_X, transpose, 4);

    float	mat[16];
    myGlMultMatrix(space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat);
    Transpose(mat, transpose);
    renderProgManager.SetRenderParms(RENDERPARM_MVPMATRIX_X, transpose, 4);

    Transpose(space->modelViewMatrix, transpose);
    renderProgManager.SetRenderParms(RENDERPARM_MODELVIEWMATRIX_X, transpose, 4);

    Transpose(backEnd.viewDef->projectionMatrix, transpose);
    renderProgManager.SetRenderParms(RENDERPARM_PROJMATRIX_X, transpose, 4);
}

/*
======================
RB_BindVariableStageImage

Handles generating a cinematic frame if needed
======================
*/
void idRenderBackend::BindVariableStageImage(const textureStage_t* texture, const float* shaderRegisters)
{
    if (texture->cinematic) {
        cinData_t	cin;

        if (r_skipDynamicTextures.GetBool()) {
            globalImages->defaultImage->Bind(0);
            return;
        }

        // offset time by shaderParm[7] (FIXME: make the time offset a parameter of the shader?)
        // We make no attempt to optimize for multiple identical cinematics being in view, or
        // for cinematics going at a lower framerate than the renderer.
        cin = texture->cinematic->ImageForTime((int)(1000 * (backEnd.viewDef->floatTime + backEnd.viewDef->renderView.shaderParms[11])));

        if (cin.image) {
            globalImages->cinematicImage->UploadScratch(cin.image, cin.imageWidth, cin.imageHeight);
            globalImages->cinematicImage->Bind(0);
        }
        else {
            globalImages->blackImage->Bind(0);
        }
    }
    else {
        //FIXME: see why image is invalid
        if (texture->image) {
            texture->image->Bind(0);
        }
    }
}

void idRenderBackend::LoadShaderTextureMatrix(const float* shaderRegisters, const textureStage_t* texture)
{
    float texS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float texT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

    if (texture->hasMatrix) {
        float matrix[16];
        RB_GetShaderTextureMatrix(shaderRegisters, texture, matrix);
        texS[0] = matrix[0 * 4 + 0];
        texS[1] = matrix[1 * 4 + 0];
        texS[2] = matrix[2 * 4 + 0];
        texS[3] = matrix[3 * 4 + 0];

        texT[0] = matrix[0 * 4 + 1];
        texT[1] = matrix[1 * 4 + 1];
        texT[2] = matrix[2 * 4 + 1];
        texT[3] = matrix[3 * 4 + 1];
    }

    renderProgManager.SetRenderParm(RENDERPARM_TEXTUREMATRIX_S, texS);
    renderProgManager.SetRenderParm(RENDERPARM_TEXTUREMATRIX_T, texT);
}

void idRenderBackend::PrepareStageTexturing(const shaderStage_t* pStage, const drawSurf_t* surf)
{
    // set privatePolygonOffset if necessary
    /*
    if (pStage->privatePolygonOffset) {
        qglEnable(GL_POLYGON_OFFSET_FILL);
        qglPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset);
    }
    */

    // set the texture matrix if needed
    LoadShaderTextureMatrix(surf->shaderRegisters, &pStage->texture);

    // texgens
    if (pStage->texture.texgen == TG_SCREEN) {

        float	mat[16], plane[4];
        myGlMultMatrix(surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat);

        plane[0] = mat[0];
        plane[1] = mat[4];
        plane[2] = mat[8];
        plane[3] = mat[12];
        renderProgManager.SetRenderParm(RENDERPARM_TEXGEN_0_S, plane);

        plane[0] = mat[1];
        plane[1] = mat[5];
        plane[2] = mat[9];
        plane[3] = mat[13];
        renderProgManager.SetRenderParm(RENDERPARM_TEXGEN_0_T, plane);

        plane[0] = mat[3];
        plane[1] = mat[7];
        plane[2] = mat[11];
        plane[3] = mat[15];
        renderProgManager.SetRenderParm(RENDERPARM_TEXGEN_0_Q, plane);
    }

    if (pStage->texture.texgen == TG_SCREEN2) {

        float	mat[16], plane[4];
        myGlMultMatrix(surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat);

        plane[0] = mat[0];
        plane[1] = mat[4];
        plane[2] = mat[8];
        plane[3] = mat[12];
        renderProgManager.SetRenderParm(RENDERPARM_TEXGEN_0_S, plane);

        plane[0] = mat[1];
        plane[1] = mat[5];
        plane[2] = mat[9];
        plane[3] = mat[13];
        renderProgManager.SetRenderParm(RENDERPARM_TEXGEN_0_T, plane);

        plane[0] = mat[3];
        plane[1] = mat[7];
        plane[2] = mat[11];
        plane[3] = mat[15];
        renderProgManager.SetRenderParm(RENDERPARM_TEXGEN_0_Q, plane);
    }

    /*
    if (pStage->texture.texgen == TG_GLASSWARP) {

        qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, FPROG_GLASSWARP);
        qglEnable(GL_FRAGMENT_PROGRAM_ARB);

        GL_SelectTexture(2);
        globalImages->scratchImage->Bind();

        GL_SelectTexture(1);
        globalImages->scratchImage2->Bind();

        qglEnable(GL_TEXTURE_GEN_S);
        qglEnable(GL_TEXTURE_GEN_T);
        qglEnable(GL_TEXTURE_GEN_Q);

        float	mat[16], plane[4];
        myGlMultMatrix(surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat);

        plane[0] = mat[0];
        plane[1] = mat[4];
        plane[2] = mat[8];
        plane[3] = mat[12];
        qglTexGenfv(GL_S, GL_OBJECT_PLANE, plane);

        plane[0] = mat[1];
        plane[1] = mat[5];
        plane[2] = mat[9];
        plane[3] = mat[13];
        qglTexGenfv(GL_T, GL_OBJECT_PLANE, plane);

        plane[0] = mat[3];
        plane[1] = mat[7];
        plane[2] = mat[11];
        plane[3] = mat[15];
        qglTexGenfv(GL_Q, GL_OBJECT_PLANE, plane);

        GL_SelectTexture(0);

    }
    */

    /*
    if (pStage->texture.texgen == TG_REFLECT_CUBE) {

        // see if there is also a bump map specified
        const shaderStage_t* bumpStage = surf->material->GetBumpStage();
        if (bumpStage) {
            // per-pixel reflection mapping with bump mapping
            bumpStage->texture.image->Bind(1);

            // Program env 5, 6, 7, 8 have been set in RB_SetProgramEnvironmentSpace

            qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT);
            qglEnable(GL_FRAGMENT_PROGRAM_ARB);
            qglBindProgramARB(GL_VERTEX_PROGRAM_ARB, VPROG_BUMPY_ENVIRONMENT);
            qglEnable(GL_VERTEX_PROGRAM_ARB);
        }
        else {
            // per-pixel reflection mapping without a normal map

            qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, FPROG_ENVIRONMENT);
            qglEnable(GL_FRAGMENT_PROGRAM_ARB);
            qglBindProgramARB(GL_VERTEX_PROGRAM_ARB, VPROG_ENVIRONMENT);
            qglEnable(GL_VERTEX_PROGRAM_ARB);
        }
    }
    */
}

/*
================
RB_DrawElementsWithCounters
================
*/
void idRenderBackend::DrawElementsWithCounters(const srfTriangles_t* tri)
{
    backEnd.pc.c_drawElements++;
    backEnd.pc.c_drawIndexes += tri->numIndexes;
    backEnd.pc.c_drawVertexes += tri->numVerts;

    if (tri->ambientSurface != NULL) {
        if (tri->indexes == tri->ambientSurface->indexes) {
            backEnd.pc.c_drawRefIndexes += tri->numIndexes;
        }
        if (tri->verts == tri->ambientSurface->verts) {
            backEnd.pc.c_drawRefVertexes += tri->numVerts;
        }
    }

    const vertCacheHandle_t vbHandle = tri->ambientCache;
    idVertexBuffer* vertexBuffer;
    if (vertexCache.CacheIsStatic(vbHandle)) {
        vertexBuffer = &vertexCache.m_staticData.vertexBuffer;
    }
    else {
        const uint64_t frameNum = (int)(vbHandle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
        if (frameNum != ((vertexCache.m_currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
            common->Warning("idRenderBackend::DrawElementsWithCounters, vertexBuffer == NULL");
            return;
        }
        vertexBuffer = &vertexCache.m_frameData[vertexCache.m_drawListNum].vertexBuffer;
    }
    int vertOffset = (int)(vbHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;

    const vertCacheHandle_t ibHandle = tri->indexCache;
    idIndexBuffer* indexBuffer;
    if (vertexCache.CacheIsStatic(ibHandle)) {
        indexBuffer = &vertexCache.m_staticData.indexBuffer;
    }
    else {
        const uint64_t frameNum = (int)(ibHandle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
        if (frameNum != ((vertexCache.m_currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
            common->Warning("idRenderBackend::DrawElementsWithCounters, indexBuffer == NULL");
            return;
        }
        indexBuffer = &vertexCache.m_frameData[vertexCache.m_drawListNum].indexBuffer;
    }
    int indexOffset = (int)(ibHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;


    fglcontext.jointCacheHandle = 0;

    renderProgManager.CommitCurrent(m_glStateBits, fglcontext.cmdbuf);

    {
        FglBuffer buffer = indexBuffer->GetAPIObject();
        FglDeviceSize offset = indexBuffer->GetOffset();// +indexOffset;
        fglCmdBindIndexBuffer(fglcontext.cmdbuf, buffer, offset, FGL_INDEX_TYPE_UINT32);
    }

    {
        FglBuffer buffer = vertexBuffer->GetAPIObject();
        FglDeviceSize offset = vertexBuffer->GetOffset();// +vertOffset;
        fglCmdBindVertexBuffers(fglcontext.cmdbuf, 1, &buffer, &offset);
    }

    fglCmdDrawIndexed(fglcontext.cmdbuf, tri->numIndexes, 1, indexOffset / sizeof(glIndex_t), vertOffset / sizeof(idDrawVert), 0);
    backEnd.pc.c_vboIndexes += tri->numIndexes;
}

void idRenderBackend::DrawShadowElementsWithCounters(const srfTriangles_t* tri, int numIndexes, float zmin, float zmax)
{
    backEnd.pc.c_shadowElements++;
    backEnd.pc.c_shadowIndexes += numIndexes;
    backEnd.pc.c_shadowVertexes += tri->numVerts;

    const vertCacheHandle_t vbHandle = tri->shadowCache;
    idVertexBuffer* vertexBuffer;
    if (vertexCache.CacheIsStatic(vbHandle)) {
        vertexBuffer = &vertexCache.m_staticData.vertexBuffer;
    }
    else {
        const uint64_t frameNum = (int)(vbHandle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
        if (frameNum != ((vertexCache.m_currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
            common->Warning("idRenderBackend::DrawShadowElementsWithCounters, vertexBuffer == NULL");
            return;
        }
        vertexBuffer = &vertexCache.m_frameData[vertexCache.m_drawListNum].vertexBuffer;
    }
    int vertOffset = (int)(vbHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;

    const vertCacheHandle_t ibHandle = tri->indexCache;
    idIndexBuffer* indexBuffer;
    if (vertexCache.CacheIsStatic(ibHandle)) {
        indexBuffer = &vertexCache.m_staticData.indexBuffer;
    }
    else {
        const uint64_t frameNum = (int)(ibHandle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
        if (frameNum != ((vertexCache.m_currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
            common->Warning("idRenderBackend::DrawShadowElementsWithCounters, indexBuffer == NULL");
            return;
        }
        indexBuffer = &vertexCache.m_frameData[vertexCache.m_drawListNum].indexBuffer;
    }
    int indexOffset = (int)(ibHandle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;

    fglcontext.jointCacheHandle = 0;

    renderProgManager.CommitCurrent(m_glStateBits, fglcontext.cmdbuf);

    // If we're using the depth bounds test, set the range after committing the pipeline
    if (m_glStateBits & GLS_DEPTH_BOUNDS_TEST)
    {
        fglCmdSetDepthBounds(fglcontext.cmdbuf, zmin, zmax);
    }

    {
        FglBuffer buffer = indexBuffer->GetAPIObject();
        FglDeviceSize offset = indexBuffer->GetOffset();// +indexOffset;
        fglCmdBindIndexBuffer(fglcontext.cmdbuf, buffer, offset, FGL_INDEX_TYPE_UINT32);
    }

    {
        FglBuffer buffer = vertexBuffer->GetAPIObject();
        FglDeviceSize offset = vertexBuffer->GetOffset();// +vertOffset;
        fglCmdBindVertexBuffers(fglcontext.cmdbuf, 1, &buffer, &offset);
    }

    fglCmdDrawIndexed(fglcontext.cmdbuf, numIndexes, 1, indexOffset / sizeof(glIndex_t), vertOffset / sizeof(shadowCache_t), 0);
    backEnd.pc.c_vboIndexes += numIndexes;
}

int idRenderBackend::SurfacesDrawShaderPasses(drawSurf_t** drawSurfs, int numDrawSurfs) {

    int i;

    // only obey skipAmbient if we are rendering a view
    if (backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool()) {
        return numDrawSurfs;
    }

    RB_LogComment("---------- RB_STD_DrawShaderPasses ----------\n");

    DebugMarker("SurfacesDrawShaderPasses %d surfs", numDrawSurfs);

    // if we are about to draw the first surface that needs
    // the rendering in a texture, copy it over
    if (drawSurfs[0]->material->GetSort() >= SS_POST_PROCESS) {
        if (r_skipPostProcess.GetBool()) {
            return 0;
        }

        CopyFramebuffer(globalImages->currentRenderImage,
            backEnd.viewDef->viewport.x1, backEnd.viewDef->viewport.y1,
            backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
            backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1);
        backEnd.currentRenderCopied = true;
    }

    SetProgramEnvironment();

    // we don't use RB_RenderDrawSurfListWithFunction()
    // because we want to defer the matrix load because many
    // surfaces won't draw any ambient passes
    backEnd.currentSpace = NULL;
    for (i = 0; i < numDrawSurfs; i++) {
        if (drawSurfs[i]->material->SuppressInSubview()) {
            continue;
        }

        if (backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef) {
            if (drawSurfs[i]->space->entityDef->parms.xrayIndex != 2) {
                continue;
            }
        }

        // we need to draw the post process shaders after we have drawn the fog lights
        if (drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS
            && !backEnd.currentRenderCopied) {
            break;
        }

        RenderShaderPasses(drawSurfs[i]);
    }

    GL_State(GLS_CULL_FRONTSIDED);

    return i;
}

/*
==================
RB_STD_T_RenderShaderPasses

This is also called for the generated 2D rendering
==================
*/
void idRenderBackend::RenderShaderPasses(const drawSurf_t* surf)
{
    int			stage;
    const idMaterial* shader;
    const shaderStage_t* pStage;
    const float* regs;
    float		color[4];
    const srfTriangles_t* tri;

    tri = surf->geo;
    shader = surf->material;

    if (!shader->HasAmbient()) {
        return;
    }

    if (shader->IsPortalSky()) {
        return;
    }

    // change the matrix if needed
    if (surf->space != backEnd.currentSpace)
    {
        backEnd.currentSpace = surf->space;
        SetProgramEnvironmentSpace();
    }

    // change the scissor if needed
    if (r_useScissor.GetBool() && !backEnd.currentScissor.Equals(surf->scissorRect))
    {
        backEnd.currentScissor = surf->scissorRect;

        FglRect2D scissor{};
        scissor.offset.x = backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1;
        scissor.offset.y = backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1;
        scissor.extent.width = backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1;
        scissor.extent.height = backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1;
        fglCmdSetScissor(fglcontext.cmdbuf, &scissor);
    }

    // some deforms may disable themselves by setting numIndexes = 0
    if (!tri->numIndexes) {
        return;
    }

    if (!tri->ambientCache) {
        common->Printf("RB_T_RenderShaderPasses: !tri->ambientCache\n");
        return;
    }

    DebugMarker("RenderShaderPasses %s", shader->GetName());

    // get the expressions for conditionals / color / texcoords
    regs = surf->shaderRegisters;

    // set face culling appropriately
    uint64_t cullMode = 0;
    switch (shader->GetCullType())
    {
    case CT_FRONT_SIDED: cullMode = GLS_CULL_FRONTSIDED; break;
    case CT_BACK_SIDED:  cullMode = GLS_CULL_BACKSIDED; break;
    case CT_TWO_SIDED:   cullMode = GLS_CULL_TWOSIDED; break;
    }

    // set polygon offset if necessary
    if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
        //qglEnable(GL_POLYGON_OFFSET_FILL);
        //qglPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
    }

    if (surf->space->weaponDepthHack) {
        //RB_EnterWeaponDepthHack();
    }

    if (surf->space->modelDepthHack != 0.0f) {
        //RB_EnterModelDepthHack(surf->space->modelDepthHack);
    }

    for (stage = 0; stage < shader->GetNumStages(); stage++)
    {
        pStage = shader->GetStage(stage);

        // check the enable condition
        if (regs[pStage->conditionRegister] == 0) {
            continue;
        }

        // skip the stages involved in lighting
        if (pStage->lighting != SL_AMBIENT) {
            continue;
        }

        // skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
        if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE)) {
            continue;
        }

        // see if we are a new-style stage
        newShaderStage_t* newStage = pStage->newStage;
        if (newStage) {
            //--------------------------
            //
            // new style stages
            //
            //--------------------------

            if (r_skipNewAmbient.GetBool()) {
                continue;
            }

            GL_State(pStage->drawStateBits | cullMode);

            renderProgManager.BindProgram(newStage->shaderProgram);

            /*
            // megaTextures bind a lot of images and set a lot of parameters
            if (newStage->megaTexture) {
                newStage->megaTexture->SetMappingForSurface(tri);
                idVec3	localViewer;
                R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewer);
                newStage->megaTexture->BindForViewOrigin(localViewer);
            }
            */

            for (int i = 0; i < newStage->numVertexParms; i++) {
                float	parm[4];
                parm[0] = regs[newStage->vertexParms[i][0]];
                parm[1] = regs[newStage->vertexParms[i][1]];
                parm[2] = regs[newStage->vertexParms[i][2]];
                parm[3] = regs[newStage->vertexParms[i][3]];

                renderProgManager.SetRenderParm((renderParm_t)(RENDERPARM_USER0 + i), parm);
            }

            for (int i = 0; i < newStage->numFragmentProgramImages; i++) {
                if (newStage->fragmentProgramImages[i]) {
                    newStage->fragmentProgramImages[i]->Bind(i);
                }
            }

            // draw it
            DrawElementsWithCounters(tri);

            /*
            if (newStage->megaTexture) {
                newStage->megaTexture->Unbind();
            }
            */

            continue;
        }

        //--------------------------
        //
        // old style stages
        //
        //--------------------------

        // set the color
        color[0] = regs[pStage->color.registers[0]];
        color[1] = regs[pStage->color.registers[1]];
        color[2] = regs[pStage->color.registers[2]];
        color[3] = regs[pStage->color.registers[3]];

        // skip the entire stage if an add would be black
        if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE)
            && color[0] <= 0 && color[1] <= 0 && color[2] <= 0) {
            continue;
        }

        // skip the entire stage if a blend would be completely transparent
        if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)
            && color[3] <= 0) {
            continue;
        }

        SetVertexColorParams(pStage->vertexColor);

        renderProgManager.SetRenderParm(RENDERPARM_COLOR, color);

        renderProgManager.BindProgram(BUILTIN_TEXTURE_VERTEXCOLOR);

        // bind the texture
        BindVariableStageImage(&pStage->texture, regs);

        // set the state
        GL_State(pStage->drawStateBits | cullMode);

        // draw it
        DrawElementsWithCounters(tri);
    }

    // reset polygon offset
    if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
        //qglDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f) {
        //RB_LeaveDepthHack();
    }
}

/*
==================
RB_STD_LightScale

Perform extra blending passes to multiply the entire buffer by
a floating point value
==================
*/
void idRenderBackend::DrawLightScale()
{
    float	v, f;

    if (backEnd.overBright == 1.0f) {
        return;
    }

    if (r_skipLightScale.GetBool()) {
        return;
    }

    RB_LogComment("---------- RB_STD_LightScale ----------\n");
    DebugMarker("DrawLightScale");

    // the scissor may be smaller than the viewport for subviews
    if (r_useScissor.GetBool())
    {
        backEnd.currentScissor = backEnd.viewDef->scissor;
        SetScissor();
    }

    // full screen blends
    {
        float ortho[16];
        ortho[ 0] = 2.0f; ortho[ 1] = 0.0f;  ortho[ 2] = 0.0f; ortho[ 3] = -1.0f;
        ortho[ 4] = 0.0f; ortho[ 5] = -2.0f; ortho[ 6] = 0.0f; ortho[ 7] = 1.0f;
        ortho[ 8] = 0.0f; ortho[ 9] = 0.0f;  ortho[10] = 1.0f; ortho[11] = 0.0f;
        ortho[12] = 0.0f; ortho[13] = 0.0f;  ortho[14] = 0.0f; ortho[15] = 1.0f;
        renderProgManager.SetRenderParms(RENDERPARM_PROJMATRIX_X, ortho, 4);
    }

    GL_State(GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_SRC_COLOR | GLS_CULL_TWOSIDED);
    //GL_State(GLS_CULL_TWOSIDED);	// so mirror views also get it
    //globalImages->BindNull();
    //qglDisable(GL_DEPTH_TEST);
    //qglDisable(GL_STENCIL_TEST);

    renderProgManager.BindProgram(BUILTIN_COLOR);

    v = 1;
    while (idMath::Fabs(v - backEnd.overBright) > 0.01) {	// a little extra slop
        f = backEnd.overBright / v;
        f /= 2;
        if (f > 1) {
            f = 1;
        }
        float color[4] = { f, f, f, 1.0f };
        renderProgManager.SetRenderParm(RENDERPARM_COLOR, color);
        v = v * f * 2;

        DrawElementsWithCounters(&m_fullscreenTri);
    }

    GL_State(GLS_CULL_FRONTSIDED);
}

/*
==================
R_SetDrawInteractions
==================
*/
void idRenderBackend::SetDrawInteraction(const shaderStage_t* surfaceStage, const float* surfaceRegs, idImage** image, idVec4 matrix[2], float color[4])
{
    *image = surfaceStage->texture.image;
    if (surfaceStage->texture.hasMatrix) {
        matrix[0][0] = surfaceRegs[surfaceStage->texture.matrix[0][0]];
        matrix[0][1] = surfaceRegs[surfaceStage->texture.matrix[0][1]];
        matrix[0][2] = 0;
        matrix[0][3] = surfaceRegs[surfaceStage->texture.matrix[0][2]];

        matrix[1][0] = surfaceRegs[surfaceStage->texture.matrix[1][0]];
        matrix[1][1] = surfaceRegs[surfaceStage->texture.matrix[1][1]];
        matrix[1][2] = 0;
        matrix[1][3] = surfaceRegs[surfaceStage->texture.matrix[1][2]];

        // we attempt to keep scrolls from generating incredibly large texture values, but
        // center rotations and center scales can still generate offsets that need to be > 1
        if (matrix[0][3] < -40 || matrix[0][3] > 40) {
            matrix[0][3] -= (int)matrix[0][3];
        }
        if (matrix[1][3] < -40 || matrix[1][3] > 40) {
            matrix[1][3] -= (int)matrix[1][3];
        }
    }
    else {
        matrix[0][0] = 1;
        matrix[0][1] = 0;
        matrix[0][2] = 0;
        matrix[0][3] = 0;

        matrix[1][0] = 0;
        matrix[1][1] = 1;
        matrix[1][2] = 0;
        matrix[1][3] = 0;
    }

    if (color) {
        for (int i = 0; i < 4; i++) {
            color[i] = surfaceRegs[surfaceStage->color.registers[i]];
            // clamp here, so card with greater range don't look different.
            // we could perform overbrighting like we do for lights, but
            // it doesn't currently look worth it.
            if (color[i] < 0) {
                color[i] = 0;
            }
            else if (color[i] > 1.0) {
                color[i] = 1.0;
            }
        }
    }
}

void idRenderBackend::SubmitInteraction(drawInteraction_t* din, std::function<void(const drawInteraction_t*)> DrawInteraction)
{
    if (!din->bumpImage) {
        return;
    }

    if (!din->diffuseImage || r_skipDiffuse.GetBool()) {
        din->diffuseImage = globalImages->blackImage;
    }
    if (!din->specularImage || r_skipSpecular.GetBool() || din->ambientLight) {
        din->specularImage = globalImages->blackImage;
    }
    if (!din->bumpImage || r_skipBump.GetBool()) {
        din->bumpImage = globalImages->flatNormalMap;
    }

    // if we wouldn't draw anything, don't call the Draw function
    if (
        ((din->diffuseColor[0] > 0 ||
            din->diffuseColor[1] > 0 ||
            din->diffuseColor[2] > 0) && din->diffuseImage != globalImages->blackImage)
        || ((din->specularColor[0] > 0 ||
            din->specularColor[1] > 0 ||
            din->specularColor[2] > 0) && din->specularImage != globalImages->blackImage)) {
        DrawInteraction(din);
    }
}

void idRenderBackend::CreateSingleDrawInteractions(const drawSurf_t* surf, std::function<void(const drawInteraction_t*)> DrawInteraction)
{
    const idMaterial* surfaceShader = surf->material;
    const float* surfaceRegs = surf->shaderRegisters;
    const viewLight_t* vLight = backEnd.vLight;
    const idMaterial* lightShader = vLight->lightShader;
    const float* lightRegs = vLight->shaderRegisters;
    drawInteraction_t	inter;

    if (r_skipInteractions.GetBool() || !surf->geo || !surf->geo->ambientCache) {
        return;
    }

    DebugMarker("CreateSingleDrawInteractions %s on %s", lightShader->GetName(), surfaceShader->GetName());

    if (tr.logFile) {
        RB_LogComment("---------- RB_CreateSingleDrawInteractions %s on %s ----------\n", lightShader->GetName(), surfaceShader->GetName());
    }

    // change the matrix and light projection vectors if needed
    if (surf->space != backEnd.currentSpace) {
        backEnd.currentSpace = surf->space;

        SetProgramEnvironmentSpace();
    }

    // change the scissor if needed
    if (r_useScissor.GetBool() && !backEnd.currentScissor.Equals(surf->scissorRect)) {
        backEnd.currentScissor = surf->scissorRect;

        SetScissor();
    }

    // hack depth range if needed
    if (surf->space->weaponDepthHack) {
        //RB_EnterWeaponDepthHack();
    }

    if (surf->space->modelDepthHack) {
        //RB_EnterModelDepthHack(surf->space->modelDepthHack);
    }

    inter.surf = surf;
    inter.lightFalloffImage = vLight->falloffImage;

    R_GlobalPointToLocal(surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3());
    R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3());
    inter.localLightOrigin[3] = 0;
    inter.localViewOrigin[3] = 1;
    inter.ambientLight = lightShader->IsAmbientLight();

    // the base projections may be modified by texture matrix on light stages
    idPlane lightProject[4];
    for (int i = 0; i < 4; i++) {
        R_GlobalPlaneToLocal(surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i]);
    }

    for (int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++) {
        const shaderStage_t* lightStage = lightShader->GetStage(lightStageNum);

        // ignore stages that fail the condition
        if (!lightRegs[lightStage->conditionRegister]) {
            continue;
        }

        inter.lightImage = lightStage->texture.image;

        memcpy(inter.lightProjection, lightProject, sizeof(inter.lightProjection));
        // now multiply the texgen by the light texture matrix
        if (lightStage->texture.hasMatrix) {
            RB_GetShaderTextureMatrix(lightRegs, &lightStage->texture, backEnd.lightTextureMatrix);
            RB_BakeTextureMatrixIntoTexgen(reinterpret_cast<class idPlane*>(inter.lightProjection), backEnd.lightTextureMatrix);
        }

        inter.bumpImage = NULL;
        inter.specularImage = NULL;
        inter.diffuseImage = NULL;
        inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
        inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;

        float lightColor[4];

        // backEnd.lightScale is calculated so that lightColor[] will never exceed
        // tr.backEndRendererMaxLight
        lightColor[0] = backEnd.lightScale * lightRegs[lightStage->color.registers[0]];
        lightColor[1] = backEnd.lightScale * lightRegs[lightStage->color.registers[1]];
        lightColor[2] = backEnd.lightScale * lightRegs[lightStage->color.registers[2]];
        lightColor[3] = lightRegs[lightStage->color.registers[3]];

        // go through the individual stages
        for (int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++) {
            const shaderStage_t* surfaceStage = surfaceShader->GetStage(surfaceStageNum);

            switch (surfaceStage->lighting) {
            case SL_AMBIENT: {
                // ignore ambient stages while drawing interactions
                break;
            }
            case SL_BUMP: {
                // ignore stage that fails the condition
                if (!surfaceRegs[surfaceStage->conditionRegister]) {
                    break;
                }
                // draw any previous interaction
                SubmitInteraction(&inter, DrawInteraction);
                inter.diffuseImage = NULL;
                inter.specularImage = NULL;
                SetDrawInteraction(surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL);
                break;
            }
            case SL_DIFFUSE: {
                // ignore stage that fails the condition
                if (!surfaceRegs[surfaceStage->conditionRegister]) {
                    break;
                }
                if (inter.diffuseImage) {
                    SubmitInteraction(&inter, DrawInteraction);
                }
                SetDrawInteraction(surfaceStage, surfaceRegs, &inter.diffuseImage, inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr());
                inter.diffuseColor[0] *= lightColor[0];
                inter.diffuseColor[1] *= lightColor[1];
                inter.diffuseColor[2] *= lightColor[2];
                inter.diffuseColor[3] *= lightColor[3];
                inter.vertexColor = surfaceStage->vertexColor;
                break;
            }
            case SL_SPECULAR: {
                // ignore stage that fails the condition
                if (!surfaceRegs[surfaceStage->conditionRegister]) {
                    break;
                }
                if (inter.specularImage) {
                    SubmitInteraction(&inter, DrawInteraction);
                }
                SetDrawInteraction(surfaceStage, surfaceRegs, &inter.specularImage, inter.specularMatrix, inter.specularColor.ToFloatPtr());
                inter.specularColor[0] *= lightColor[0];
                inter.specularColor[1] *= lightColor[1];
                inter.specularColor[2] *= lightColor[2];
                inter.specularColor[3] *= lightColor[3];
                inter.vertexColor = surfaceStage->vertexColor;
                break;
            }
            }
        }

        // draw the final interaction
        SubmitInteraction(&inter, DrawInteraction);
    }

    // unhack depth range if needed
    if (surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f) {
        //RB_LeaveDepthHack();
    }
}

void idRenderBackend::CreateDrawInteractions(const drawSurf_t* surf, uint64_t depthFunc, bool stencilTest)
{
    if (!surf) {
        return;
    }

    // perform setup here that will be constant for all interactions
    if (stencilTest)
    {
        GL_State(
            GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | 
            GLS_DEPTHMASK | depthFunc |
            GLS_STENCIL_FUNC_EQUAL |
            GLS_STENCIL_MAKE_REF(0x80) |
            GLS_STENCIL_MAKE_MASK(0xFF));
    }
    else
    {
        GL_State(
            GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE |
            GLS_DEPTHMASK | depthFunc | 
            GLS_STENCIL_FUNC_ALWAYS);
    }

    // TODO: Handle skinned geometry
    if (backEnd.vLight->lightShader->IsAmbientLight())
    {
        //renderProgManager.BindProgram(BUILTIN_INTERACTION_AMBIENT);
        renderProgManager.BindProgram(BUILTIN_INTERACTION);
    }
    else
    {
        renderProgManager.BindProgram(BUILTIN_INTERACTION);
    }

    /*
    // texture 0 is the normalization cube map for the vector towards the light
    if (backEnd.vLight->lightShader->IsAmbientLight()) {
        globalImages->ambientNormalMap->Bind(0);
    }
    else {
        globalImages->normalCubeMapImage->Bind(0);
    }
    */

    // texture 6 is the specular lookup table
    if (r_testARBProgram.GetBool()) {
        globalImages->specular2DTableImage->Bind(5);	// variable specularity in alpha channel
    }
    else {
        globalImages->specularTableImage->Bind(5);
    }


    for (; surf; surf = surf->nextOnLight) {
        // perform setup here that will not change over multiple interaction passes

        // this may cause RB_ARB2_DrawInteraction to be exacuted multiple
        // times with different colors and images if the surface or light have multiple layers
        CreateSingleDrawInteractions(surf, [this](const drawInteraction_t* din)
            {
                // load all the vertex program parameters
                renderProgManager.SetRenderParm(RENDERPARM_LOCALLIGHTORIGIN, din->localLightOrigin.ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_LOCALVIEWORIGIN, din->localViewOrigin.ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_LIGHTPROJECTION_S, din->lightProjection[0].ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_LIGHTPROJECTION_T, din->lightProjection[1].ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_LIGHTPROJECTION_Q, din->lightProjection[2].ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_LIGHTFALLOFF_S, din->lightProjection[3].ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_BUMPMATRIX_S, din->bumpMatrix[0].ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_BUMPMATRIX_T, din->bumpMatrix[1].ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_DIFFUSEMATRIX_S, din->diffuseMatrix[0].ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_DIFFUSEMATRIX_T, din->diffuseMatrix[1].ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_SPECULARMATRIX_S, din->specularMatrix[0].ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_SPECULARMATRIX_T, din->specularMatrix[1].ToFloatPtr());

                SetVertexColorParams(din->vertexColor);

                // set the constant colors
                renderProgManager.SetRenderParm(RENDERPARM_DIFFUSEMODIFIER, din->diffuseColor.ToFloatPtr());
                renderProgManager.SetRenderParm(RENDERPARM_SPECULARMODIFIER, din->specularColor.ToFloatPtr());

                // set the textures

                // texture 1 will be the per-surface bump map
                din->bumpImage->Bind(0);

                // texture 2 will be the light falloff texture
                din->lightFalloffImage->Bind(1);

                // texture 3 will be the light projection texture
                din->lightImage->Bind(2);

                // texture 4 is the per-surface diffuse map
                din->diffuseImage->Bind(3);

                // texture 5 is the per-surface specular map
                din->specularImage->Bind(4);

                // draw it
                DrawElementsWithCounters(din->surf->geo);
            });
    }
}

void idRenderBackend::DrawInteractions()
{
    viewLight_t* vLight;
    const idMaterial* lightShader;

    //
    // for each light, perform adding and shadowing
    //
    for (vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next) {
        backEnd.vLight = vLight;

        // do fogging later
        if (vLight->lightShader->IsFogLight()) {
            continue;
        }
        if (vLight->lightShader->IsBlendLight()) {
            continue;
        }

        if (!vLight->localInteractions && !vLight->globalInteractions && !vLight->translucentInteractions) {
            continue;
        }

        lightShader = vLight->lightShader;

        DebugMarker("DrawInteractions %s", lightShader->GetName());

        bool stencilTest = false;

        // clear the stencil buffer if needed
        if (vLight->globalShadows || vLight->localShadows) {
            backEnd.currentScissor = vLight->scissorRect;

            SetScissor();

            FglClearAttachment clearAttachment{};
            clearAttachment.aspectMask = FGL_IMAGE_ASPECT_STENCIL_BIT;
            clearAttachment.clearValue.depthStencil.depth = 0.0f;
            clearAttachment.clearValue.depthStencil.stencil = 128;
            fglCmdClearAttachments(fglcontext.cmdbuf, 1, &clearAttachment, 0, nullptr);

            stencilTest = true;
        }

        StencilShadowPass(vLight->globalShadows);
        CreateDrawInteractions(vLight->localInteractions, GLS_DEPTHFUNC_EQUAL, stencilTest);
        StencilShadowPass(vLight->localShadows);
        CreateDrawInteractions(vLight->globalInteractions, GLS_DEPTHFUNC_EQUAL, stencilTest);

        // translucent surfaces never get stencil shadowed
        if (r_skipTranslucent.GetBool()) {
            continue;
        }

        CreateDrawInteractions(vLight->translucentInteractions, GLS_DEPTHFUNC_LESS, false);
    }
}

void idRenderBackend::StencilShadowPass(const drawSurf_t* drawSurfs)
{
    if (!r_shadows.GetBool()) {
        return;
    }

    if (!drawSurfs) {
        return;
    }

    RB_LogComment("---------- RB_StencilShadowPass ----------\n");

    /*
    if (r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat()) {
        qglPolygonOffset(r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat());
        qglEnable(GL_POLYGON_OFFSET_FILL);
    }

    if (r_useDepthBoundsTest.GetBool()) {
        qglEnable(GL_DEPTH_BOUNDS_TEST_EXT);
    }
    */

    DrawSurfChainWithFunction(drawSurfs, [this](const drawSurf_t* surf) { DrawShadow(surf); });

    /*
    if (r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat()) {
        qglDisable(GL_POLYGON_OFFSET_FILL);
    }

    if (r_useDepthBoundsTest.GetBool()) {
        qglDisable(GL_DEPTH_BOUNDS_TEST_EXT);
    }
    */
}

void idRenderBackend::DrawShadow(const drawSurf_t* surf)
{
    const srfTriangles_t* tri;

    tri = surf->geo;

    if (!tri->shadowCache) {
        return;
    }

    // set the light position if we are using a vertex program to project the rear surfaces
    //if (surf->space != backEnd.currentSpace)
    {
        idVec4 localLight;

        R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3());
        localLight.w = 0.0f;
        renderProgManager.SetRenderParm(RENDERPARM_LOCALLIGHTORIGIN, localLight.ToFloatPtr());
    }

    // we always draw the sil planes, but we may not need to draw the front or rear caps
    int	numIndexes;
    bool external = false;

    if (!r_useExternalShadows.GetInteger()) {
        numIndexes = tri->numIndexes;
    }
    else if (r_useExternalShadows.GetInteger() == 2) { // force to no caps for testing
        numIndexes = tri->numShadowIndexesNoCaps;
    }
    else if (!(surf->dsFlags & DSF_VIEW_INSIDE_SHADOW)) {
        // if we aren't inside the shadow projection, no caps are ever needed needed
        numIndexes = tri->numShadowIndexesNoCaps;
        external = true;
    }
    else if (!backEnd.vLight->viewInsideLight && !(surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE)) {
        // if we are inside the shadow projection, but outside the light, and drawing
        // a non-infinite shadow, we can skip some caps
        if (backEnd.vLight->viewSeesShadowPlaneBits & surf->geo->shadowCapPlaneBits) {
            // we can see through a rear cap, so we need to draw it, but we can skip the
            // caps on the actual surface
            numIndexes = tri->numShadowIndexesNoFrontCaps;
        }
        else {
            // we don't need to draw any caps
            numIndexes = tri->numShadowIndexesNoCaps;
        }
        external = true;
    }
    else {
        // must draw everything
        numIndexes = tri->numIndexes;
    }

    uint64_t drawState = 0;

    // for visualizing the shadows
    if (r_showShadows.GetInteger()) {
        if (r_showShadows.GetInteger() == 1) {
            // draw filled in
            drawState = GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS;
        }
        else {
            // draw as lines, filling the depth buffer
            drawState = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS;
        }
    }
    else {
        // don't write to the color buffer, just the stencil buffer
        drawState = GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS;
    }

    // set depth bounds
    if (r_useDepthBoundsTest.GetBool()) {
        // TODO: Doesn't seem to work properly?
        //drawState |= GLS_DEPTH_BOUNDS_TEST;
    }

    // debug visualization
    
    if (r_showShadows.GetInteger()) {
        float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

        if (r_showShadows.GetInteger() == 3) {
            if (external) {
                color[0] = 0.1f / backEnd.overBright;
                color[1] = 0.1f / backEnd.overBright;
                color[2] = 0.1f / backEnd.overBright;
            }
            else {
                // these are the surfaces that require the reverse
                color[0] = 1.0f / backEnd.overBright;
                color[1] = 0.1f / backEnd.overBright;
                color[2] = 0.1f / backEnd.overBright;
            }
        }
        else {
            // draw different color for turboshadows
            if (surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE) {
                if (numIndexes == tri->numIndexes) {
                    color[0] = 1.0f / backEnd.overBright;
                    color[1] = 0.1f / backEnd.overBright;
                    color[2] = 0.1f / backEnd.overBright;
                }
                else {
                    color[0] = 1.0f / backEnd.overBright;
                    color[1] = 0.4f / backEnd.overBright;
                    color[2] = 0.1f / backEnd.overBright;
                }
            }
            else {
                if (numIndexes == tri->numIndexes) {
                    color[0] = 0.1f / backEnd.overBright;
                    color[1] = 1.0f / backEnd.overBright;
                    color[2] = 0.1f / backEnd.overBright;
                }
                else if (numIndexes == tri->numShadowIndexesNoFrontCaps) {
                    color[0] = 0.1f / backEnd.overBright;
                    color[1] = 1.0f / backEnd.overBright;
                    color[2] = 0.6f / backEnd.overBright;
                }
                else {
                    color[0] = 0.6f / backEnd.overBright;
                    color[1] = 1.0f / backEnd.overBright;
                    color[2] = 0.1f / backEnd.overBright;
                }
            }
        }

        renderProgManager.BindProgram(BUILTIN_SHADOW_DEBUG);

        renderProgManager.SetRenderParm(RENDERPARM_COLOR, color);

        GL_State(drawState | GLS_CULL_TWOSIDED);
        DrawShadowElementsWithCounters(tri, numIndexes, surf->scissorRect.zmin, surf->scissorRect.zmax);
        return;
    }

    DebugMarker("DrawShadow %s", external ? "external" : "internal");

    renderProgManager.BindProgram(BUILTIN_SHADOW);

    // patented depth-fail stencil shadows
    if (!external)
    {
        GL_State(drawState | GLS_CULL_TWOSIDED |
            GLS_STENCIL_MAKE_REF(1) | GLS_STENCIL_MAKE_MASK(0xFF) |
            (GLS_STENCIL_FUNC_ALWAYS | GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_DECR | GLS_STENCIL_OP_PASS_KEEP) |
            (GLS_BACK_STENCIL_FUNC_ALWAYS | GLS_BACK_STENCIL_OP_FAIL_KEEP | GLS_BACK_STENCIL_OP_ZFAIL_INCR | GLS_BACK_STENCIL_OP_PASS_KEEP));

        DrawShadowElementsWithCounters(tri, numIndexes, surf->scissorRect.zmin, surf->scissorRect.zmax);
    }
    // traditional depth-pass stencil shadows
    else
    {
        GL_State(drawState | GLS_CULL_TWOSIDED |
            GLS_STENCIL_MAKE_REF(1) | GLS_STENCIL_MAKE_MASK(0xFF) |
            (GLS_STENCIL_FUNC_ALWAYS | GLS_STENCIL_OP_FAIL_KEEP | GLS_STENCIL_OP_ZFAIL_KEEP | GLS_STENCIL_OP_PASS_INCR) |
            (GLS_BACK_STENCIL_FUNC_ALWAYS | GLS_BACK_STENCIL_OP_FAIL_KEEP | GLS_BACK_STENCIL_OP_ZFAIL_KEEP | GLS_BACK_STENCIL_OP_PASS_DECR));

        DrawShadowElementsWithCounters(tri, numIndexes, surf->scissorRect.zmin, surf->scissorRect.zmax);
    }
}

void idRenderBackend::DrawSurfChainWithFunction(const drawSurf_t* drawSurfs, std::function<void(const drawSurf_t*)> DrawFunc)
{
    const drawSurf_t* drawSurf;

    backEnd.currentSpace = NULL;

    for (drawSurf = drawSurfs; drawSurf; drawSurf = drawSurf->nextOnLight)
    {
        // change the matrix if needed
        if (drawSurf->space != backEnd.currentSpace) {
            backEnd.currentSpace = drawSurf->space;
            SetProgramEnvironmentSpace();
        }

        if (drawSurf->space->weaponDepthHack) {
            //RB_EnterWeaponDepthHack();
        }

        if (drawSurf->space->modelDepthHack) {
            //RB_EnterModelDepthHack(drawSurf->space->modelDepthHack);
        }

        // change the scissor if needed
        if (r_useScissor.GetBool() && !backEnd.currentScissor.Equals(drawSurf->scissorRect)) {
            backEnd.currentScissor = drawSurf->scissorRect;
            SetScissor();
        }

        // render it
        DrawFunc(drawSurf);

        if (drawSurf->space->weaponDepthHack || drawSurf->space->modelDepthHack != 0.0f) {
            //RB_LeaveDepthHack();
        }
    }
}