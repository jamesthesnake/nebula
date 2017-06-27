#pragma once
//------------------------------------------------------------------------------
/**
	The transform node contains just a hierarchical transform
	
	(C) 2017 Individual contributors, see AUTHORS file
*/
//------------------------------------------------------------------------------
#include "modelnode.h"
namespace Models
{
class TransformNode : public ModelNode
{
	__DeclareClass(TransformNode);
public:
	/// constructor
	TransformNode();
	/// destructor
	virtual ~TransformNode();
private:
};
} // namespace Models