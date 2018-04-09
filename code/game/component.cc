//------------------------------------------------------------------------------
//  component.cc
//  (C) 2018 Individual contributors, see AUTHORS file
//------------------------------------------------------------------------------
#include "stdneb.h"
#include "component.h"

// #define ASSURE_VALIDITY

namespace Game {

//------------------------------------------------------------------------------
/**
*/
ComponentContainer::ComponentContainer()
{
	// empty
}

//------------------------------------------------------------------------------
/**
*/
ComponentContainer::~ComponentContainer()
{
	// empty	
}

//------------------------------------------------------------------------------
/**
*/
void
ComponentContainer::RegisterEntity(const Entity& entity)
{
	this->componentData.RegisterEntity(entity);
}

//------------------------------------------------------------------------------
/**
*/
void
ComponentContainer::UnregisterEntity(const Entity& entity)
{
	this->componentData.DeregisterEntity(entity);
}

//------------------------------------------------------------------------------
/**
*/
uint32_t
ComponentContainer::GetInstance(const Entity& entity) const
{
	return this->componentData.GetInstance(entity);
}

//------------------------------------------------------------------------------
/**
*/
Component*
ComponentContainer::GetInstanceData(const uint32_t& instance)
{
	return &this->componentData[instance];
}

//------------------------------------------------------------------------------
/**
*/
Entity
ComponentContainer::GetOwner(const uint32_t& instance) const
{
	return this->componentData[instance].owner;
}

//------------------------------------------------------------------------------
/**
*/
bool
ComponentContainer::IsActive(const uint32_t& instance) const
{
	return this->componentData[instance].isActive;
}

//------------------------------------------------------------------------------
/**
*/
ComponentPlugin*
ComponentContainer::Plugin()
{
	return static_cast<ComponentPlugin*>(&this->plugin);
}

//------------------------------------------------------------------------------
/**
*/
void
ComponentContainer::OnBeginFrame()
{
#ifdef ASSURE_VALIDITY
	Ptr<EntityManager> entityManager = EntityManager::Instance();
#endif

	// Run garbage collection and pack array.
	this->componentData.Optimize();

	SizeT i = 0;
	Component* instance;
	while (i < this->componentData.data.Size())
	{
		instance = &this->componentData.data[i];

#ifdef ASSURE_VALIDITY
		// This assures that a entity is alive before running the frame.
		// Useful where it's imperative that the component is removed immediately
		// after the entity is destroyed.
		if (!entityManager->IsAlive(instance->owner))
		{
			// This will swap move the last element into this instances position
			this->componentData.DeregisterEntityImmediate(instance->owner);
			// Just run the same index again.
			continue;
		}
#endif
		this->plugin.OnBeginFrame(static_cast<void*>(instance));
		// Go to next instance
		i++;
	}
}

//------------------------------------------------------------------------------
/**
*/
void
ComponentContainer::SetDescriptor(const uint32_t& instance, const Util::String & str)
{
	this->componentData[instance].descriptor = str;
}

//------------------------------------------------------------------------------
/**
*/
Util::String
ComponentContainer::GetDescriptor(const uint32_t& instance) const
{
	return this->componentData[instance].descriptor;
}

//------------------------------------------------------------------------------
/**
*/
void
ComponentContainer::SetMass(const uint32_t& instance, const Math::scalar& newMass)
{
	this->componentData[instance].mass = newMass;
}

//------------------------------------------------------------------------------
/**
*/
Math::scalar
ComponentContainer::GetMass(const uint32_t& instance) const
{
	return this->componentData[instance].mass;
}

//------------------------------------------------------------------------------
/**
*/
void
ComponentContainer::SetVelocity(const uint32_t& instance, const Math::float4& vel)
{
	this->componentData[instance].vel = vel;
}

//------------------------------------------------------------------------------
/**
*/
Math::float4
ComponentContainer::GetVelocity(const uint32_t& instance) const
{
	return this->componentData[instance].vel;
}

} // namespace Game