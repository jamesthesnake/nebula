//------------------------------------------------------------------------------
// framescript.cc
// (C) 2016-2020 Individual contributors, see AUTHORS file
//------------------------------------------------------------------------------
#include "render/stdneb.h"
#include "framescript.h"
#include "frameserver.h"
#include "coregraphics/displaydevice.h"
#include "coregraphics/drawthread.h"
#include "coregraphics/graphicsdevice.h"
#include "profiling/profiling.h"

namespace Frame
{

__ImplementClass(Frame::FrameScript, 'FRSC', Core::RefCounted);
//------------------------------------------------------------------------------
/**
*/
FrameScript::FrameScript() :
    window(Ids::InvalidId32),
    frameOpCounter(0),
    subScript(false)
{
    // empty
}

//------------------------------------------------------------------------------
/**
*/
FrameScript::~FrameScript()
{
}

//------------------------------------------------------------------------------
/**
*/
void
FrameScript::AddTexture(const Util::StringAtom& name, const CoreGraphics::TextureId tex)
{
    IndexT i = this->texturesByName.FindIndex(name);
    n_assert(i == InvalidIndex);
    this->texturesByName.Add(name, tex);
    this->textures.Append(tex);
}

//------------------------------------------------------------------------------
/**
*/
void
FrameScript::AddBuffer(const Util::StringAtom& name, const CoreGraphics::BufferId buf)
{
    n_assert(!this->buffersByName.Contains(name));
    this->buffersByName.Add(name, buf);
    this->buffers.Append(buf);
}

//------------------------------------------------------------------------------
/**
*/
void
FrameScript::AddOp(Frame::FrameOp* op)
{
    op->index = this->frameOpCounter;
    this->frameOpCounter++;
    this->ops.Append(op);
    this->opsByName.Add(op->name, op);
}

//------------------------------------------------------------------------------
/**
*/
Frame::FrameOp*
FrameScript::GetOp(const Util::String& search)
{
    Util::Array<Util::String> paths = search.Tokenize("/");
    Util::Dictionary<Util::StringAtom, FrameOp*>* list = &this->opsByName;
    Frame::FrameOp* op = nullptr;
    while (!paths.IsEmpty())
    {
        IndexT i = list->FindIndex(paths.Front());
        if (i == InvalidIndex)
        {
            n_warning("Could not find operation using path '%s'", search.AsCharPtr());
            return nullptr;
        }

        // get operation for next iteration of loop
        op = list->ValueAtIndex(i);
        list = &op->childrenByName;

        // remove element we just used
        paths.EraseFront();
    }
    return op;
}

//------------------------------------------------------------------------------
/**
*/
void 
FrameScript::Setup()
{
#if NEBULA_ENABLE_MT_DRAW
    this->drawThread = CoreGraphics::CreateDrawThread();
    Util::String scriptName = this->resId.Value();
    scriptName.StripFileExtension();
    Util::String threadName = Util::String::Sprintf("FrameScript %s Draw Thread", scriptName.AsCharPtr());
    this->drawThread->SetName(threadName);
    this->drawThread->SetThreadAffinity(System::Cpu::Core5);
    this->drawThread->Start();
    CoreGraphics::CommandBufferPoolCreateInfo poolInfo =
    {
        CoreGraphics::GraphicsQueueType,
        true,
        true
    };
    this->drawThreadCommandPool = CoreGraphics::CreateCommandBufferPool(poolInfo);
#endif
}

//------------------------------------------------------------------------------
/**
*/
void
FrameScript::Discard()
{
    // unload ourselves, this is only for convenience
    FrameServer::Instance()->UnloadFrameScript(this->resId);

#if NEBULA_ENABLE_MT_DRAW
    this->drawThread->Stop();
    CoreGraphics::DestroyCommandBufferPool(this->drawThreadCommandPool);
#endif

    this->buildAllocator.Release();
    this->allocator.Release();
}

//------------------------------------------------------------------------------
/**
*/
void 
FrameScript::RunJobs(const IndexT frameIndex, const IndexT bufferIndex)
{
#if NEBULA_ENABLE_MT_DRAW
    // tell graphics to start using our draw thread
    CoreGraphics::SetDrawThread(this->drawThread);
    //this->drawThread->Lock();

    IndexT i;
    for (i = 0; i < this->compiled.Size(); i++)
    {
        this->compiled[i]->RunJobs(frameIndex, bufferIndex);
    }

    // tell graphics to stop using our thread
    CoreGraphics::SetDrawThread(nullptr);

    // make sure to add a sync at the end
    this->drawThread->Signal(&this->drawThreadEvent);
    //this->drawThread->Unlock();
#endif
}

//------------------------------------------------------------------------------
/**
*/
void
FrameScript::Run(const IndexT frameIndex, const IndexT bufferIndex)
{
#if NEBULA_ENABLE_MT_DRAW
    N_MARKER_BEGIN(WaitForRecord, Graphics);

    // wait for draw thread to finish before executing buffers
    this->drawThreadEvent.Wait();

    N_MARKER_END();
#endif

    IndexT i;
    for (i = 0; i < this->compiled.Size(); i++)
    {
        this->compiled[i]->Run(frameIndex, bufferIndex);
    }
}

//------------------------------------------------------------------------------
/**
*/
void
FrameScript::Build()
{
    IndexT i;
    
    // go through and discard all compiled (dunno if needed)
    if (!this->compiled.IsEmpty())
    {
        for (i = 0; i < this->compiled.Size(); i++)
        {
            this->compiled[i]->Discard();
        }
        this->compiled.Clear();
    }

    for (i = 0; i < this->resourceResetBarriers.Size(); i++)
        DestroyBarrier(this->resourceResetBarriers[i]);
    this->resourceResetBarriers.Clear();

    for (i = 0; i < this->events.Size(); i++)
        DestroyEvent(this->events[i]);
    this->events.Clear();

    for (i = 0; i < this->barriers.Size(); i++)
        DestroyBarrier(this->barriers[i]);
    this->barriers.Clear();

    // clear old compiled result
    this->buildAllocator.Release();
     
    Util::Dictionary<CoreGraphics::BufferId, Util::Array<FrameOp::BufferDependency>> rwBuffers;
    Util::Dictionary<CoreGraphics::TextureId, Util::Array<FrameOp::TextureDependency>> textures;

    // get window texture
    CoreGraphics::TextureId window = FrameServer::Instance()->GetWindowTexture();

    // give every resource an initial dependency
    for (i = 0; i < this->textures.Size(); i++)
    {
        CoreGraphics::TextureId tex = this->textures[i];
        bool isDepth = CoreGraphics::PixelFormat::IsDepthFormat(CoreGraphics::TextureGetPixelFormat(tex));
        CoreGraphics::ImageLayout layout = CoreGraphics::TextureGetDefaultLayout(tex);
        auto& arr = textures.AddUnique(tex);

        uint layers = CoreGraphics::TextureGetNumLayers(tex);
        uint mips = CoreGraphics::TextureGetNumMips(tex);

        CoreGraphics::ImageSubresourceInfo subres;
        subres.aspect = isDepth ? (CoreGraphics::ImageAspect::DepthBits | CoreGraphics::ImageAspect::StencilBits) : CoreGraphics::ImageAspect::ColorBits;
        subres.layer = 0;
        subres.layerCount = layers;
        subres.mip = 0;
        subres.mipCount = mips;
        if (tex == window)
            arr.Append(FrameOp::TextureDependency{ nullptr, CoreGraphics::QueueType::GraphicsQueueType, layout, CoreGraphics::BarrierStage::Transfer, CoreGraphics::BarrierAccess::TransferRead, DependencyIntent::Read, InvalidIndex, subres });
        else
            arr.Append(FrameOp::TextureDependency{ nullptr, CoreGraphics::QueueType::GraphicsQueueType, layout, CoreGraphics::BarrierStage::AllGraphicsShaders | CoreGraphics::BarrierStage::ComputeShader, CoreGraphics::BarrierAccess::ShaderRead, DependencyIntent::Read, InvalidIndex, subres });
    }

    // build ops
    for (i = 0; i < this->ops.Size(); i++)
    {
        this->ops[i]->Build(this->buildAllocator, this->compiled, this->events, this->barriers, rwBuffers, textures, this->drawThreadCommandPool);
    }

    // setup a post-frame barrier to reset the resource state of all resources back to their created original (ShaderRead for RenderTexture, General for RWTexture
    Util::Array<CoreGraphics::TextureBarrier> texturesBarr;

    for (i = 0; i < textures.Size(); i++)
    {
        const CoreGraphics::TextureId& res = textures.KeyAtIndex(i);
        CoreGraphics::ImageLayout layout = CoreGraphics::TextureGetDefaultLayout(res);
        const Util::Array<FrameOp::TextureDependency>& deps = textures.ValueAtIndex(i);

        const FrameOp::TextureDependency& dep = deps.Back();
        const CoreGraphics::ImageSubresourceInfo& info = dep.subres;
        CoreGraphics::BarrierAccess outAccess = dep.layout == CoreGraphics::ImageLayout::Present ? CoreGraphics::BarrierAccess::TransferRead : CoreGraphics::BarrierAccess::ShaderRead;
        CoreGraphics::BarrierStage outStage = outAccess == CoreGraphics::BarrierAccess::TransferRead ? CoreGraphics::BarrierStage::Transfer : CoreGraphics::BarrierStage::AllGraphicsShaders;

        // render textures are created as shader read
        if (dep.layout != layout)
        {
            CoreGraphics::BarrierCreateInfo inf =
            {
                Util::String::Sprintf("End of Frame Texture Reset Transition %d", res.resourceId),
                CoreGraphics::BarrierDomain::Global,
                dep.stage,
                outStage,
                { CoreGraphics::TextureBarrier{ res, info, dep.layout, layout, dep.access, outAccess } }, nullptr
            };
            this->resourceResetBarriers.Append(CoreGraphics::CreateBarrier(inf));
        }
    }
}

//------------------------------------------------------------------------------
/**
*/
void
FrameScript::Cleanup()
{
    IndexT i;
    for (i = 0; i < this->textures.Size(); i++) DestroyTexture(this->textures[i]);
    this->textures.Clear();
    this->texturesByName.Clear();

    for (i = 0; i < this->buffers.Size(); i++) DestroyBuffer(this->buffers[i]);
    this->buffers.Clear();
    this->buffersByName.Clear();

    for (i = 0; i < this->events.Size(); i++) DestroyEvent(this->events[i]);
    this->events.Clear();

    for (i = 0; i < this->ops.Size(); i++) this->ops[i]->Discard();
    this->ops.Clear();
}

//------------------------------------------------------------------------------
/**
*/
void
FrameScript::OnWindowResized()
{
    // only do this if we actually use the window
    if (this->window != Ids::InvalidId32)
    {
        CoreGraphics::WindowId prev = CoreGraphics::DisplayDevice::Instance()->GetCurrentWindow();

        // make this window current
        WindowMakeCurrent(this->window);

        IndexT i;
        for (i = 0; i < this->textures.Size(); i++)
            TextureWindowResized(this->textures[i]);
        for (i = 0; i < this->ops.Size(); i++)
            this->ops[i]->OnWindowResized();

        Build();

        // reset old window
        WindowMakeCurrent(prev);
    }

}

} // namespace Frame2
