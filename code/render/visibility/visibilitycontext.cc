//------------------------------------------------------------------------------
//  visobservercontext.cc
//  (C) 2018 Individual contributors, see AUTHORS file
//------------------------------------------------------------------------------
#include "stdneb.h"
#include "visibilitycontext.h"
#include "graphics/graphicsserver.h"

#include "graphics/cameracontext.h"
#include "graphics/lightcontext.h"
#include "graphics/lightprobecontext.h"
#include "models/modelcontext.h"

#include "systems/boxsystem.h"
#include "systems/octreesystem.h"
#include "systems/portalsystem.h"
#include "systems/quadtreesystem.h"
#include "systems/bruteforcesystem.h"

#include "system/cpu.h"

namespace Visibility
{

ObserverContext::ObserverAllocator ObserverContext::observerAllocator;
ObservableContext::ObserveeAllocator ObservableContext::observeeAllocator;

Util::Array<VisibilitySystem*> ObserverContext::systems;
Jobs::JobPortId ObserverContext::jobPort;
Threading::SafeQueue<Jobs::JobId> ObserverContext::runningJobs;

extern void VisibilitySortJob(const Jobs::JobFuncContext& ctx);

ImplementContext(ObserverContext);
//------------------------------------------------------------------------------
/**
*/
void
ObserverContext::Setup(const Graphics::GraphicsEntityId id, VisibilityEntityType entityType)
{
	const Graphics::ContextEntityId cid = GetContextId(id);
	observerAllocator.Get<2>(cid.id) = entityType;
	observerAllocator.Get<1>(cid.id) = id;

	// go through observerable objects and allocate a slot for the object, and set it to the default visible state
	const Util::Array<Graphics::GraphicsEntityId>& ids = ObservableContext::observeeAllocator.GetArray<1>();
	for (IndexT i = 0; i < ids.Size(); i++)
	{
		
		Ids::Id32 res = observerAllocator.Get<3>(cid.id).AllocObject();
		Graphics::ContextEntityId cid2 = ObservableContext::__state.entitySliceMap[ids[i].id];
		n_assert(res == cid2.id);
		observerAllocator.Get<3>(cid.id).Get<0>(res) = true;

		if (entityType == Model)
		{
			const Util::Array<Models::ModelNode::Instance*>& nodes = Models::ModelContext::GetModelNodeInstances(id);
		}
	}
}

//------------------------------------------------------------------------------
/**
*/
void 
ObserverContext::OnBeforeFrame(const IndexT frameIndex, const Timing::Time frameTime)
{
	const Util::Array<VisibilityEntityType>& observerTypes = observerAllocator.GetArray<2>();
	const Util::Array<VisibilityEntityType>& observeeTypes = ObservableContext::observeeAllocator.GetArray<2>();

	const Util::Array<Graphics::GraphicsEntityId>& observerIds = observerAllocator.GetArray<1>();
	const Util::Array<Graphics::GraphicsEntityId>& observeeIds = ObservableContext::observeeAllocator.GetArray<1>();

	Util::Array<Math::matrix44>& observerTransforms = observerAllocator.GetArray<0>();
	Util::Array<Math::matrix44>& observeeTransforms = ObservableContext::observeeAllocator.GetArray<0>();

	const Util::Array<VisibilityResultAllocator>& results = observerAllocator.GetArray<3>();

	IndexT i;
	for (i = 0; i < observeeIds.Size(); i++)
	{
		const Graphics::GraphicsEntityId id = observeeIds[i];
		const VisibilityEntityType type = observeeTypes[i];

		switch (type)
		{
		case Model:
			observeeTransforms[i] = Models::ModelContext::GetTransform(id);
			break;
		case Light:
			observeeTransforms[i] = Graphics::LightContext::GetTransform(id);
			break;
		case LightProbe:
			observeeTransforms[i] = Graphics::LightProbeContext::GetTransform(id);
			break;
		}
	}

	for (i = 0; i < observerIds.Size(); i++)
	{
		const Graphics::GraphicsEntityId id = observerIds[i];
		const VisibilityEntityType type = observerTypes[i];

		VisibilityResultAllocator& result = results[i];

		// fetch current context ids
		IndexT j;
		for (j = 0; j < observeeIds.Size(); j++)
		{
			const VisibilityEntityType type = observeeTypes[i];
			Util::Array<Graphics::ContextEntityId>& contextIds = result.GetArray<1>();

			switch (type)
			{
			case Model:
				contextIds[j] = Models::ModelContext::GetContextId(observeeIds[j]);
				break;
			case Light:
				contextIds[j] = Graphics::LightContext::GetContextId(observeeIds[j]);
				break;
			case LightProbe:
				contextIds[j] = Graphics::LightProbeContext::GetContextId(observeeIds[j]);
				break;
			}
		}

		switch (type)
		{
		case Camera:
			observerTransforms[i] = Graphics::CameraContext::GetViewProjection(id);
			break;
		case Light:
			observerTransforms[i] = Graphics::LightContext::GetTransform(id);
			break;
		case LightProbe:
			observerTransforms[i] = Graphics::LightProbeContext::GetTransform(id);
			break;
		}
	}

	// first step, go through list of visible entities and reset
	Util::Array<VisibilityResultAllocator>& vis = observerAllocator.GetArray<3>();

	// reset all lists to that all entities are visible
	for (i = 0; i < vis.Size(); i++)
	{
		VisibilityResultAllocator& list = vis[i];
		Util::Array<bool>& flags = list.GetArray<0>();

		for (IndexT j = 0; j < flags.Size(); j++)
		{
			flags[j] = true;
		}
	}

	// prepare visibility systems
	if (observerTransforms.Size() > 0) for (i = 0; i < ObserverContext::systems.Size(); i++)
	{
		VisibilitySystem* sys = ObserverContext::systems[i];
		Util::Array<bool>& flags = vis[i].GetArray<0>();
		sys->PrepareObservers(observerTransforms.Begin(), flags.Begin(), observerTransforms.Size());
	}

	// setup observerable entities
	const Util::Array<Graphics::GraphicsEntityId>& ids = ObservableContext::observeeAllocator.GetArray<1>();
	if (observeeTransforms.Size() > 0) for (i = 0; i < ObserverContext::systems.Size(); i++)
	{
		VisibilitySystem* sys = ObserverContext::systems[i];
		sys->PrepareEntities(observeeTransforms.Begin(), ids.Begin(), observeeTransforms.Size());
	}

	// run all visibility systems
	IndexT j;
	for (j = 0; j < ObserverContext::systems.Size(); j++)
	{
		VisibilitySystem* sys = ObserverContext::systems[j];
		sys->Run();
	}

	// put a sync point for the jobs
	Jobs::JobPortSync(ObserverContext::jobPort);

	if (vis.Size() > 0) for (i = 0; i < vis.Size(); i++)
	{
		Util::Array<bool>& flags = vis[i].GetArray<0>();
		Util::Array<Graphics::ContextEntityId>& entities = vis[i].GetArray<1>();
		VisibilityBuckets& visibilities = observerAllocator.GetArray<4>()[i];

		// then execute sort job, which only runs the function once
		Jobs::JobContext ctx;
		ctx.uniform.numBuffers = 0;
		ctx.input.numBuffers = 2;
		ctx.output.numBuffers = 1;

		ctx.input.data[0] = flags.Begin();
		ctx.input.dataSize[0] = sizeof(bool) * flags.Size();
		ctx.input.sliceSize[0] = sizeof(bool) * flags.Size();
		
		ctx.input.data[1] = entities.Begin();
		ctx.input.dataSize[1] = sizeof(Graphics::ContextEntityId) * entities.Size();
		ctx.input.sliceSize[1] = sizeof(Graphics::ContextEntityId) * entities.Size();

		ctx.output.data[0] = &visibilities;
		ctx.output.dataSize[0] = sizeof(VisibilityBuckets);
		ctx.output.sliceSize[0] = sizeof(VisibilityBuckets);

		// schedule job
		Jobs::JobId job = Jobs::CreateJob({ VisibilitySortJob });
		Jobs::JobSchedule(job, ObserverContext::jobPort, ctx);

		// add to delete list
		ObserverContext::runningJobs.Enqueue(job);
	}

}

//------------------------------------------------------------------------------
/**
*/
void
ObserverContext::Create()
{
	__bundle.OnBeforeFrame = ObserverContext::OnBeforeFrame;
	__bundle.OnWaitForWork = ObserverContext::WaitForVisibility;
	__bundle.OnBeforeView = nullptr;
	__bundle.OnAfterView = nullptr;
	__bundle.OnAfterFrame = nullptr;
	Graphics::GraphicsServer::Instance()->RegisterGraphicsContext(&__bundle);

	Jobs::CreateJobPortInfo info =
	{
		"VisibilityJobPort",
		4,
		System::Cpu::Core1 | System::Cpu::Core2 | System::Cpu::Core3 | System::Cpu::Core4,
		UINT_MAX
	};
	ObserverContext::jobPort = Jobs::CreateJobPort(info);

	CreateContext();
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem*
ObserverContext::CreateBoxSystem(const BoxSystemLoadInfo& info)
{
	BoxSystem* system = n_new(BoxSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem*
ObserverContext::CreatePortalSystem(const PortalSystemLoadInfo& info)
{
	PortalSystem* system = n_new(PortalSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem*
ObserverContext::CreateOctreeSystem(const OctreeSystemLoadInfo& info)
{
	OctreeSystem* system = n_new(OctreeSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem*
ObserverContext::CreateQuadtreeSystem(const QuadtreeSystemLoadInfo & info)
{
	QuadtreeSystem* system = n_new(QuadtreeSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
VisibilitySystem* 
ObserverContext::CreateBruteforceSystem(const BruteforceSystemLoadInfo& info)
{
	BruteforceSystem* system = n_new(BruteforceSystem);
	system->Setup(info);
	ObserverContext::systems.Append(system);
	return system;
}

//------------------------------------------------------------------------------
/**
*/
void
ObserverContext::WaitForVisibility(const IndexT frameIndex, const Timing::Time frameTime)
{
	Util::Array<Jobs::JobId> jobs;
	ObserverContext::runningJobs.DequeueAll(jobs);

	// wait for all jobs to finish
	IndexT i;
	for (i = 0; i < jobs.Size(); i++)
	{
		Jobs::JobWait(jobs[i]);
		Jobs::DestroyJob(jobs[i]);
	}
}

//------------------------------------------------------------------------------
/**
*/
Graphics::ContextEntityId
ObserverContext::Alloc()
{
	return observerAllocator.AllocObject();
}

//------------------------------------------------------------------------------
/**
*/
void
ObserverContext::Dealloc(Graphics::ContextEntityId id)
{
	observerAllocator.DeallocObject(id.id);
}


ImplementContext(ObservableContext);
//------------------------------------------------------------------------------
/**
*/
void
ObservableContext::Setup(const Graphics::GraphicsEntityId id, VisibilityEntityType entityType)
{
	const Graphics::ContextEntityId cid = ObservableContext::GetContextId(id);
	observeeAllocator.Get<1>(cid.id) = id;
	observeeAllocator.Get<2>(cid.id) = entityType;

	// go through observers and allocate visibility slot for this object
	const Util::Array<ObserverContext::VisibilityResultAllocator>& visAllocators = ObserverContext::observerAllocator.GetArray<3>();
	for (IndexT i = 0; i < visAllocators.Size(); i++)
	{
		ObserverContext::VisibilityResultAllocator& alloc = visAllocators[i];
		Ids::Id32 obj = alloc.AllocObject();
		n_assert(cid == obj);
		alloc.Get<0>(obj) = true;
		alloc.Get<1>(obj) = Models::ModelContext::GetContextId(id); // get context Id since model can be loaded later...
	}
}

//------------------------------------------------------------------------------
/**
*/
void 
ObservableContext::Create()
{
	CreateContext();
}

//------------------------------------------------------------------------------
/**
*/
Graphics::ContextEntityId 
ObservableContext::Alloc()
{
	return observeeAllocator.AllocObject();
}

//------------------------------------------------------------------------------
/**
*/
void 
ObservableContext::Dealloc(Graphics::ContextEntityId id)
{
	observeeAllocator.DeallocObject(id.id);
}

} // namespace Visibility
