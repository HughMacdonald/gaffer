//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2015, Image Engine Design Inc. All rights reserved.
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
//      * Neither the name of Image Engine Design nor the names of
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

#include "Gaffer/StringPlug.h"

#include "GafferImage/Resize.h"
#include "GafferImage/Sampler.h"
#include "GafferImage/Resample.h"

using namespace Imath;
using namespace Gaffer;
using namespace GafferImage;

IE_CORE_DEFINERUNTIMETYPED( Resize );

size_t Resize::g_firstPlugIndex = 0;

Resize::Resize( const std::string &name )
	:   FlatImageProcessor( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );

	addChild( new FormatPlug( "format" ) );
	addChild( new IntPlug( "fitMode", Plug::In, Horizontal, Horizontal, Distort ) );
	addChild( new StringPlug( "filter" ) );
	addChild( new M33fPlug( "__matrix", Plug::Out ) );
	addChild( new ImagePlug( "__resampledIn", Plug::In, Plug::Default & ~Plug::Serialisable ) );

	// We don't really do much work ourselves - we just
	// defer to an internal Resample node to do the hard
	// work of filtering everything into the right place.

	ResamplePtr resample = new Resample( "__resample" );
	addChild( resample );

	resample->inPlug()->setInput( inPlug() );

	resample->filterPlug()->setInput( filterPlug() );
	resample->matrixPlug()->setInput( matrixPlug() );
	resample->boundingModePlug()->setValue( Sampler::Clamp );

	resampledInPlug()->setInput( resample->outPlug() );

	outPlug()->metadataPlug()->setInput( inPlug()->metadataPlug() );
	outPlug()->channelNamesPlug()->setInput( inPlug()->channelNamesPlug() );
}

Resize::~Resize()
{
}

GafferImage::FormatPlug *Resize::formatPlug()
{
	return getChild<FormatPlug>( g_firstPlugIndex );
}

const GafferImage::FormatPlug *Resize::formatPlug() const
{
	return getChild<FormatPlug>( g_firstPlugIndex );
}

Gaffer::IntPlug *Resize::fitModePlug()
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

const Gaffer::IntPlug *Resize::fitModePlug() const
{
	return getChild<IntPlug>( g_firstPlugIndex + 1 );
}

Gaffer::StringPlug *Resize::filterPlug()
{
	return getChild<StringPlug>( g_firstPlugIndex + 2 );
}

const Gaffer::StringPlug *Resize::filterPlug() const
{
	return getChild<StringPlug>( g_firstPlugIndex + 2 );
}

Gaffer::M33fPlug *Resize::matrixPlug()
{
	return getChild<M33fPlug>( g_firstPlugIndex + 3 );
}

const Gaffer::M33fPlug *Resize::matrixPlug() const
{
	return getChild<M33fPlug>( g_firstPlugIndex + 3 );
}

ImagePlug *Resize::resampledInPlug()
{
	return getChild<ImagePlug>( g_firstPlugIndex + 4 );
}

const ImagePlug *Resize::resampledInPlug() const
{
	return getChild<ImagePlug>( g_firstPlugIndex + 4 );
}

void Resize::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	FlatImageProcessor::affects( input, outputs );

	if(
		formatPlug()->isAncestorOf( input ) ||
		input == fitModePlug() ||
		input == inPlug()->formatPlug() ||
		input == inPlug()->dataWindowPlug()
	)
	{
		outputs.push_back( matrixPlug() );
	}

	if( formatPlug()->isAncestorOf( input ) )
	{
		outputs.push_back( outPlug()->formatPlug() );
	}

	if(
		input == inPlug()->dataWindowPlug() ||
		input == resampledInPlug()->dataWindowPlug()
	)
	{
		outputs.push_back( outPlug()->dataWindowPlug() );
	}

	if(
		input == inPlug()->channelDataPlug() ||
		input == resampledInPlug()->channelDataPlug()
	)
	{
		outputs.push_back( outPlug()->channelDataPlug() );
	}
}

void Resize::hash( const ValuePlug *output, const Context *context, IECore::MurmurHash &h ) const
{
	FlatImageProcessor::hash( output, context, h );

	if( output == matrixPlug() )
	{
		formatPlug()->hash( h );
		fitModePlug()->hash( h );
		inPlug()->formatPlug()->hash( h );
		inPlug()->dataWindowPlug()->hash( h );
	}
}

void Resize::compute( ValuePlug *output, const Context *context ) const
{
	if( output == matrixPlug() )
	{
		const Format inFormat = inPlug()->formatPlug()->getValue();
		const Format outFormat = formatPlug()->getValue();

		const V2f inSize( inFormat.width(), inFormat.height() );
		const V2f outSize( outFormat.width(), outFormat.height() );
		const V2f formatScale = outSize / inSize;

		V2f scale( 1 );
		switch( (FitMode)fitModePlug()->getValue() )
		{
			case Horizontal :
				scale = V2f( formatScale.x );
				break;
			case Vertical :
				scale = V2f( formatScale.y );
				break;
			case Fit :
				scale = V2f( std::min( formatScale.x, formatScale.y ) );
				break;
			case Fill :
				scale = V2f( std::max( formatScale.x, formatScale.y ) );
				break;
			case Distort :
			default :
				scale = formatScale;
				break;
		}

		const V2f translate = ( outSize - ( inSize * scale ) ) / 2.0f;

		M33f matrix;
		matrix.translate( translate );
		matrix.scale( scale );

		static_cast<M33fPlug *>( output )->setValue( matrix );
	}

	FlatImageProcessor::compute( output, context );
}

void Resize::hashFlatFormat( const GafferImage::ImagePlug *parent, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h = formatPlug()->hash();
}

GafferImage::Format Resize::computeFlatFormat( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return formatPlug()->getValue();
}

void Resize::hashFlatDataWindow( const GafferImage::ImagePlug *parent, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h = source()->dataWindowPlug()->hash();
}

Imath::Box2i Resize::computeFlatDataWindow( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return source()->dataWindowPlug()->getValue();
}

void Resize::hashFlatChannelData( const GafferImage::ImagePlug *parent, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h = source()->channelDataPlug()->hash();
}

IECore::ConstFloatVectorDataPtr Resize::computeFlatChannelData( const std::string &channelName, const Imath::V2i &tileOrigin, const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return source()->channelDataPlug()->getValue();
}

const ImagePlug *Resize::source() const
{
	ImagePlug::GlobalScope c( Context::current() );
	if( formatPlug()->getValue().getDisplayWindow() == inPlug()->formatPlug()->getValue().getDisplayWindow() )
	{
		return inPlug();
	}
	else
	{
		return resampledInPlug();
	}
}
