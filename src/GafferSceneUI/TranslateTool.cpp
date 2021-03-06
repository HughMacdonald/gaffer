//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2014-2016, John Haddon. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "GafferSceneUI/TranslateTool.h"

#include "GafferSceneUI/SceneView.h"

#include "GafferScene/SceneAlgo.h"

#include "GafferUI/TranslateHandle.h"

#include "Gaffer/MetadataAlgo.h"
#include "Gaffer/ScriptNode.h"
#include "Gaffer/UndoScope.h"

#include "OpenEXR/ImathMatrixAlgo.h"

#include "boost/bind.hpp"

using namespace std;
using namespace Imath;
using namespace IECore;
using namespace Gaffer;
using namespace GafferUI;
using namespace GafferScene;
using namespace GafferSceneUI;

IE_CORE_DEFINERUNTIMETYPED( TranslateTool );

TranslateTool::ToolDescription<TranslateTool, SceneView> TranslateTool::g_toolDescription;

size_t TranslateTool::g_firstPlugIndex = 0;

TranslateTool::TranslateTool( SceneView *view, const std::string &name )
	:	TransformTool( view, name )
{

	static Style::Axes axes[] = { Style::X, Style::Y, Style::Z, Style::XY, Style::XZ, Style::YZ, Style::XYZ };
	static const char *handleNames[] = { "x", "y", "z", "xy", "xz", "yz", "xyz" };

	for( int i = 0; i < 7; ++i )
	{
		HandlePtr handle = new TranslateHandle( axes[i] );
		handle->setRasterScale( 75 );
		handle->setVisibleOnHover( axes[i] == Style::XY || axes[i] == Style::XZ || axes[i] == Style::YZ );
		handles()->setChild( handleNames[i], handle );
		// connect with group 0, so we get called before the Handle's slot does.
		handle->dragBeginSignal().connect( 0, boost::bind( &TranslateTool::dragBegin, this ) );
		handle->dragMoveSignal().connect( boost::bind( &TranslateTool::dragMove, this, ::_1, ::_2 ) );
		handle->dragEndSignal().connect( boost::bind( &TranslateTool::dragEnd, this ) );
	}

	storeIndexOfNextChild( g_firstPlugIndex );

	addChild( new IntPlug( "orientation", Plug::In, Parent, Local, World ) );
}

TranslateTool::~TranslateTool()
{
}

Gaffer::IntPlug *TranslateTool::orientationPlug()
{
	return getChild<IntPlug>( g_firstPlugIndex );
}

const Gaffer::IntPlug *TranslateTool::orientationPlug() const
{
	return getChild<IntPlug>( g_firstPlugIndex );
}

bool TranslateTool::affectsHandles( const Gaffer::Plug *input ) const
{
	if( TransformTool::affectsHandles( input ) )
	{
		return true;
	}

	return
		input == orientationPlug() ||
		input == scenePlug()->transformPlug();
}

void TranslateTool::updateHandles()
{
	handles()->setTransform(
		orientedTransform( static_cast<Orientation>( orientationPlug()->getValue() ) )
	);

	// Because we provide multiple orientations, the handles
	// may well not be aligned with the axes of the transform
	// space. So any given handle might affect several components
	// of the target translation. For each handle, check to see
	// if each of the plugs it effects are settable, and if not,
	// disable the handle.
	Translation translation( this );
	for( TranslateHandleIterator it( handles() ); !it.done(); ++it )
	{
		(*it)->setEnabled( translation.canApply( (*it)->axisMask() ) );
	}
}

void TranslateTool::translate( const Imath::V3f &offset )
{
	if( !selection().transformPlug )
	{
		return;
	}

	Translation( this ).apply( offset );
}

IECore::RunTimeTypedPtr TranslateTool::dragBegin()
{
	m_drag = Translation( this );
	TransformTool::dragBegin();
	return nullptr; // let the handle start the drag with the event system
}

bool TranslateTool::dragMove( const GafferUI::Gadget *gadget, const GafferUI::DragDropEvent &event )
{
	UndoScope undoScope( selection().transformPlug->ancestor<ScriptNode>(), UndoScope::Enabled, undoMergeGroup() );
	m_drag.apply( static_cast<const TranslateHandle *>( gadget )->translation( event ) );
	return true;
}

bool TranslateTool::dragEnd()
{
	TransformTool::dragEnd();
	return false;
}

//////////////////////////////////////////////////////////////////////////
// TranslateTool::Translation
//////////////////////////////////////////////////////////////////////////

TranslateTool::Translation::Translation( const TranslateTool *tool )
{
	Context::Scope scopedContext( tool->view()->getContext() );

	const Selection &selection = tool->selection();
	m_plug = selection.transformPlug->translatePlug();
	m_origin = m_plug->getValue();

	const M44f handlesTransform = tool->orientedTransform( static_cast<Orientation>( tool->orientationPlug()->getValue() ) );
	m_gadgetToTransform = handlesTransform * selection.sceneToTransformSpace();
}

bool TranslateTool::Translation::canApply( const Imath::V3f &offset ) const
{
	V3f offsetInTransformSpace;
	m_gadgetToTransform.multDirMatrix( offset, offsetInTransformSpace );

	for( int i = 0; i < 3; ++i )
	{
		if( offsetInTransformSpace[i] != 0.0f )
		{
			const ValuePlug *plug = m_plug->getChild( i );
			if( !plug->settable() || MetadataAlgo::readOnly( plug ) )
			{
				return false;
			}
		}
	}

	return true;
}

void TranslateTool::Translation::apply( const Imath::V3f &offset ) const
{
	V3f offsetInTransformSpace;
	m_gadgetToTransform.multDirMatrix( offset, offsetInTransformSpace );
	for( int i = 0; i < 3; ++i )
	{
		if( offsetInTransformSpace[i] != 0.0f )
		{
			m_plug->getChild( i )->setValue(
				m_origin[i] + offsetInTransformSpace[i]
			);
		}
	}
}
