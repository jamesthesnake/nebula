#pragma once
//------------------------------------------------------------------------------
/**
	An observer is some entity which is used to resolve visible objects in the scene.

	The most obvious type of Observer is a camera, which resolves what is to be rendered on the screen.
	A shadow casting light is also a type of observer.

	Whenever the scene has to be rendered from a view, the observer will contain a list of all entities
	visible from this view.
	
	(C) 2017 Individual contributors, see AUTHORS file
*/
//------------------------------------------------------------------------------
#include "core/refcounted.h"
#include "graphics/graphicsentity.h"
#include "math/bbox.h"
#include "visibility.h"
namespace Visibility
{
class Observer : public Core::RefCounted
{
	__DeclareClass(Observer);
public:
	/// constructor
	Observer();
	/// destructor
	virtual ~Observer();

	/// notify that an entity has been added for visibility
	void OnVisibilityDatabaseChanged();
	/// get projection (orthographic or perspective) matrix representation
	Math::matrix44 GetProjectionMatrix() const;
	/// get bounding box representation
	Math::bbox GetBoundingBox() const;
	/// get type of observer
	const ObserverType GetType() const;

	/// get back original entity
	const Ptr<Graphics::GraphicsEntity>& GetEntity() const;
private:

	ObserverType type;
	Ptr<Graphics::GraphicsEntity> entity;
	Ptr<VisibilityContainer> container;

	union
	{
		Math::bbox box;
		Math::matrix44 matrix;
	} representation;
};

//------------------------------------------------------------------------------
/**
*/
const Ptr<Graphics::GraphicsEntity>&
Observer::GetEntity() const
{
	return this->entity;
}
//------------------------------------------------------------------------------
/**
*/
inline Math::matrix44
Observer::GetProjectionMatrix() const
{
	n_assert(this->type != ObserverType::BoundingBox);
	return this->representation.matrix;
}

//------------------------------------------------------------------------------
/**
*/
inline Math::bbox
Observer::GetBoundingBox() const
{
	n_assert(this->type == ObserverType::BoundingBox);
	return this->representation.box;
}

//------------------------------------------------------------------------------
/**
*/
inline const Visibility::ObserverType
Observer::GetType() const
{
	return this->type;
}

} // namespace Visibility