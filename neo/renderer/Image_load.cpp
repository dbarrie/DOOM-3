/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"
#include <furygl_util.h>

#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

/*
PROBLEM: compressed textures may break the zero clamp rule!
*/

static bool FormatIsDXT( int internalFormat ) {
	if ( internalFormat < GL_COMPRESSED_RGB_S3TC_DXT1_EXT 
	|| internalFormat > GL_COMPRESSED_RGBA_S3TC_DXT5_EXT ) {
		return false;
	}
	return true;
}

int MakePowerOfTwo( int num ) {
	int		pot;
	for (pot = 1 ; pot < num ; pot<<=1) {
	}
	return pot;
}

/*
================
BitsForInternalFormat

Used for determining memory utilization
================
*/
int idImage::BitsForInternalFormat( FglFormat internalFormat ) const {
	switch ( internalFormat ) {
	case FGL_FORMAT_R8G8B8A8_UNORM:
		return 32;
	default:
		common->Error( "R_BitsForInternalFormat: BAD FORMAT:%i", internalFormat );
	}
	return 0;
}

//=======================================================================


static byte	mipBlendColors[16][4] = {
	{0,0,0,0},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
	{255,0,0,128},
	{0,255,0,128},
	{0,0,255,128},
};

/*
================
idImage::Downsize
helper function that takes the current width/height and might make them smaller
================
*/
void idImage::GetDownsize( int &scaled_width, int &scaled_height ) const {
	int size = 0;

	// perform optional picmip operation to save texture memory
	if ( depth == TD_SPECULAR && globalImages->image_downSizeSpecular.GetInteger() ) {
		size = globalImages->image_downSizeSpecularLimit.GetInteger();
		if ( size == 0 ) {
			size = 64;
		}
	} else if ( depth == TD_BUMP && globalImages->image_downSizeBump.GetInteger() ) {
		size = globalImages->image_downSizeBumpLimit.GetInteger();
		if ( size == 0 ) {
			size = 64;
		}
	} else if ( ( allowDownSize || globalImages->image_forceDownSize.GetBool() ) && globalImages->image_downSize.GetInteger() ) {
		size = globalImages->image_downSizeLimit.GetInteger();
		if ( size == 0 ) {
			size = 256;
		}
	}

	if ( size > 0 ) {
		while ( scaled_width > size || scaled_height > size ) {
			if ( scaled_width > 1 ) {
				scaled_width >>= 1;
			}
			if ( scaled_height > 1 ) {
				scaled_height >>= 1;
			}
		}
	}

	// clamp to minimum size
	if ( scaled_width < 1 ) {
		scaled_width = 1;
	}
	if ( scaled_height < 1 ) {
		scaled_height = 1;
	}

	// clamp size to the hardware specific upper limit
	// scale both axis down equally so we don't have to
	// deal with a half mip resampling
	// This causes a 512*256 texture to sample down to
	// 256*128 on a voodoo3, even though it could be 256*256
	while ( scaled_width > glConfig.maxTextureSize
		|| scaled_height > glConfig.maxTextureSize ) {
		scaled_width >>= 1;
		scaled_height >>= 1;
	}
}

/*
================
GenerateImage

The alpha channel bytes should be 255 if you don't
want the channel.

We need a material characteristic to ask for specific texture modes.

Designed limitations of flexibility:

No support for texture borders.

No support for texture border color.

No support for texture environment colors or GL_BLEND or GL_DECAL
texture environments, because the automatic optimization to single
or dual component textures makes those modes potentially undefined.

No non-power-of-two images.

No palettized textures.

There is no way to specify separate wrap/clamp values for S and T

There is no way to specify explicit mip map levels

================
*/
void idImage::GenerateImage( const byte *pic, int width, int height, 
					   textureFilter_t filterParm, bool allowDownSizeParm, 
					   textureRepeat_t repeatParm, textureDepth_t depthParm ) {
	bool	preserveBorder;
	byte		*scaledBuffer;
	int			scaled_width, scaled_height;
	byte		*shrunk;

	PurgeImage();

	filter = filterParm;
	allowDownSize = allowDownSizeParm;
	repeat = repeatParm;
	depth = depthParm;

	// if we don't have a rendering context, just return after we
	// have filled in the parms.  We must have the values set, or
	// an image match from a shader before OpenGL starts would miss
	// the generated texture
	if ( !glConfig.isInitialized ) {
		return;
	}

	// don't let mip mapping smear the texture into the clamped border
	if ( repeat == TR_CLAMP_TO_ZERO ) {
		preserveBorder = true;
	} else {
		preserveBorder = false;
	}

	// make sure it is a power of 2
	scaled_width = MakePowerOfTwo( width );
	scaled_height = MakePowerOfTwo( height );

	if ( scaled_width != width || scaled_height != height ) {
		common->Error( "R_CreateImage: not a power of 2 image" );
	}

	// Optionally modify our width/height based on options/hardware
	//GetDownsize( scaled_width, scaled_height );

	scaledBuffer = NULL;

	// select proper internal format before we resample
	internalFormat = FGL_FORMAT_R8G8B8A8_UNORM;

	// copy or resample data as appropriate for first MIP level
	if ( ( scaled_width == width ) && ( scaled_height == height ) ) {
		// we must copy even if unchanged, because the border zeroing
		// would otherwise modify const data
		scaledBuffer = (byte *)R_StaticAlloc( sizeof( uint32_t ) * scaled_width * scaled_height );
		memcpy (scaledBuffer, pic, width*height*4);
	} else {
		// resample down as needed (FIXME: this doesn't seem like it resamples anymore!)
		// scaledBuffer = R_ResampleTexture( pic, width, height, width >>= 1, height >>= 1 );
		scaledBuffer = R_MipMap( pic, width, height, preserveBorder );
		width >>= 1;
		height >>= 1;
		if ( width < 1 ) {
			width = 1;
		}
		if ( height < 1 ) {
			height = 1;
		}

		while ( width > scaled_width || height > scaled_height ) {
			shrunk = R_MipMap( scaledBuffer, width, height, preserveBorder );
			R_StaticFree( scaledBuffer );
			scaledBuffer = shrunk;

			width >>= 1;
			height >>= 1;
			if ( width < 1 ) {
				width = 1;
			}
			if ( height < 1 ) {
				height = 1;
			}
		}

		// one might have shrunk down below the target size
		scaled_width = width;
		scaled_height = height;
	}

	uploadHeight = scaled_height;
	uploadWidth = scaled_width;
	type = TT_2D;

	// zero the border if desired, allowing clamped projection textures
	// even after picmip resampling or careless artists.
	if ( repeat == TR_CLAMP_TO_ZERO ) {
		byte	rgba[4];

		rgba[0] = rgba[1] = rgba[2] = 0;
		rgba[3] = 255;
		R_SetBorderTexels( (byte *)scaledBuffer, width, height, rgba );
	}
	if ( repeat == TR_CLAMP_TO_ZERO_ALPHA ) {
		byte	rgba[4];

		rgba[0] = rgba[1] = rgba[2] = 255;
		rgba[3] = 0;
		R_SetBorderTexels( (byte *)scaledBuffer, width, height, rgba );
	}

	if ( generatorFunction == NULL && ( depth == TD_BUMP && globalImages->image_writeNormalTGA.GetBool() || depth != TD_BUMP && globalImages->image_writeTGA.GetBool() ) ) {
		// Optionally write out the texture to a .tga
		char filename[MAX_IMAGE_NAME];
		ImageProgramStringToCompressedFileName( imgName, filename );
		char *ext = strrchr(filename, '.');
		if ( ext ) {
			strcpy( ext, ".tga" );
			// swap the red/alpha for the write
			/*
			if ( depth == TD_BUMP ) {
				for ( int i = 0; i < scaled_width * scaled_height * 4; i += 4 ) {
					scaledBuffer[ i ] = scaledBuffer[ i + 3 ];
					scaledBuffer[ i + 3 ] = 0;
				}
			}
			*/
			R_WriteTGA( filename, scaledBuffer, scaled_width, scaled_height, false );

			// put it back
			/*
			if ( depth == TD_BUMP ) {
				for ( int i = 0; i < scaled_width * scaled_height * 4; i += 4 ) {
					scaledBuffer[ i + 3 ] = scaledBuffer[ i ];
					scaledBuffer[ i ] = 0;
				}
			}
			*/
		}
	}

	// swap the red and alpha for rxgb support
	// do this even on tga normal maps so we only have to use
	// one fragment program
	// if the image is precompressed ( either in palletized mode or true rxgb mode )
	// then it is loaded above and the swap never happens here
	if ( depth == TD_BUMP && globalImages->image_useNormalCompression.GetInteger() != 1 ) {
		for ( int i = 0; i < scaled_width * scaled_height * 4; i += 4 ) {
			scaledBuffer[ i + 3 ] = scaledBuffer[ i ];
			scaledBuffer[ i ] = 0;
		}
	}

	auto numMipLevels = [](int width, int height)
	{
		int size = width > height ? width : height;
		uint32_t mips = 0;
		while (size)
		{
			++mips;
			size >>= 1;
		}
		return mips;
	};

	AllocImage(scaled_width, scaled_height, numMipLevels(scaled_width, scaled_height), 1, FGL_FORMAT_R8G8B8A8_UNORM, FGL_IMAGE_VIEW_TYPE_2D, FGL_IMAGE_TILING_LINEAR);

	FglConvertImageInfo convertInfo{ FGL_FORMAT_R8G8B8A8_UNORM, 0, 0, 1, (void**)&scaledBuffer };
	ID_FGL_CHECK(fglConvertImageData(fglcontext.device, m_image, &convertInfo, nullptr));

	CreateSampler();

	if ( scaledBuffer != 0 ) {
		R_StaticFree( scaledBuffer );
	}
}

/*
====================
GenerateCubeImage

Non-square cube sides are not allowed
====================
*/
void idImage::GenerateCubeImage( const byte *pic[6], int size, 
					   textureFilter_t filterParm, bool allowDownSizeParm, 
					   textureDepth_t depthParm ) {
	int			scaled_width, scaled_height;
	int			width, height;

	PurgeImage();

	filter = filterParm;
	repeat = TR_CLAMP;
	allowDownSize = allowDownSizeParm;
	depth = depthParm;

	type = TT_CUBIC;

	// if we don't have a rendering context, just return after we
	// have filled in the parms.  We must have the values set, or
	// an image match from a shader before OpenGL starts would miss
	// the generated texture
	if ( !glConfig.isInitialized ) {
		return;
	}

	width = height = size;

	// select proper internal format before we resample
	internalFormat = FGL_FORMAT_R8G8B8A8_UNORM;

	// don't bother with downsample for now
	scaled_width = width;
	scaled_height = height;

	uploadHeight = scaled_height;
	uploadWidth = scaled_width;

	auto numMipLevels = [](int width, int height)
	{
		int size = width > height ? width : height;
		uint32_t mips = 0;
		while (size)
		{
			++mips;
			size >>= 1;
		}
		return mips;
	};

	AllocImage(uploadWidth, uploadHeight, numMipLevels(uploadWidth, uploadHeight), 6, FGL_FORMAT_R8G8B8A8_UNORM, FGL_IMAGE_VIEW_TYPE_CUBEMAP, FGL_IMAGE_TILING_LINEAR);

	FglConvertImageInfo convertInfo{ FGL_FORMAT_R8G8B8A8_UNORM, 0, 0, 1, (void**)pic };
	ID_FGL_CHECK(fglConvertImageData(fglcontext.device, m_image, &convertInfo, nullptr));

	CreateSampler();
}

void idImage::CreateSampler()
{
	FglSamplerCreateInfo samplerInfo{};
	
	// set the minimize / maximize filtering
	switch (filter)
	{
	case TF_DEFAULT:
		samplerInfo.minFilter = globalImages->textureMinFilter;
		samplerInfo.magFilter = globalImages->textureMaxFilter;
		break;
	case TF_LINEAR:
		samplerInfo.minFilter = FGL_FILTER_LINEAR;
		samplerInfo.magFilter = FGL_FILTER_LINEAR;
		break;
	case TF_NEAREST:
		samplerInfo.minFilter = FGL_FILTER_NEAREST;
		samplerInfo.magFilter = FGL_FILTER_NEAREST;
		break;
	default:
		common->FatalError("R_CreateImage: bad texture filter");
	}

	/*
	if (glConfig.anisotropicAvailable) {
		// only do aniso filtering on mip mapped images
		if (filter == TF_DEFAULT) {
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, globalImages->textureAnisotropy);
		}
		else {
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);
		}
	}
	if (glConfig.textureLODBiasAvailable) {
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS_EXT, globalImages->textureLODBias);
	}
	*/

	// set the wrap/clamp modes
	switch (repeat)
	{
	case TR_REPEAT:
		samplerInfo.addressModeU = FGL_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = FGL_SAMPLER_ADDRESS_MODE_REPEAT;
		break;
	case TR_CLAMP_TO_BORDER:
	case TR_CLAMP_TO_ZERO:
	case TR_CLAMP_TO_ZERO_ALPHA:
	case TR_CLAMP:
		samplerInfo.addressModeU = FGL_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = FGL_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		break;
	default:
		common->FatalError("R_CreateImage: bad texture repeat");
	}

	// Free existing sampler if we have one
	if (m_sampler)
		fglDestroySampler(fglcontext.device, m_sampler, nullptr);

	ID_FGL_CHECK(fglCreateSampler(fglcontext.device, &samplerInfo, nullptr, &m_sampler));
}

/*
================
ImageProgramStringToFileCompressedFileName
================
*/
void idImage::ImageProgramStringToCompressedFileName( const char *imageProg, char *fileName ) const {
	const char	*s;
	char	*f;

	strcpy( fileName, "dds/" );
	f = fileName + strlen( fileName );

	int depth = 0;

	// convert all illegal characters to underscores
	// this could conceivably produce a duplicated mapping, but we aren't going to worry about it
	for ( s = imageProg ; *s ; s++ ) {
		if ( *s == '/' || *s == '\\' || *s == '(') {
			if ( depth < 4 ) {
				*f = '/';
				depth ++;
			} else {
				*f = ' ';
			}
			f++;
		} else if ( *s == '<' || *s == '>' || *s == ':' || *s == '|' || *s == '"' || *s == '.' ) {
			*f = '_';
			f++;
		} else if ( *s == ' ' && *(f-1) == '/' ) {	// ignore a space right after a slash
		} else if ( *s == ')' || *s == ',' ) {		// always ignore these
		} else {
			*f = *s;
			f++;
		}
	}
	*f++ = 0;
	strcat( fileName, ".dds" );
}

/*
==================
NumLevelsForImageSize
==================
*/
int	idImage::NumLevelsForImageSize( int width, int height ) const {
	int	numLevels = 1;

	while ( width > 1 || height > 1 ) {
		numLevels++;
		width >>= 1;
		height >>= 1;
	}

	return numLevels;
}

/*
================
WritePrecompressedImage

When we are happy with our source data, we can write out precompressed
versions of everything to speed future load times.
================
*/
/*
void idImage::WritePrecompressedImage() {

	// Always write the precompressed image if we're making a build
	if ( !com_makingBuild.GetBool() ) {
		if ( !globalImages->image_writePrecompressedTextures.GetBool() || !globalImages->image_usePrecompressedTextures.GetBool() ) {
			return;
		}
	}

	if ( !glConfig.isInitialized ) {
		return;
	}

	char filename[MAX_IMAGE_NAME];
	ImageProgramStringToCompressedFileName( imgName, filename );



	int numLevels = NumLevelsForImageSize( uploadWidth, uploadHeight );
	if ( numLevels > MAX_TEXTURE_LEVELS ) {
		common->Warning( "R_WritePrecompressedImage: level > MAX_TEXTURE_LEVELS for image %s", filename );
		return;
	}

	// glGetTexImage only supports a small subset of all the available internal formats
	// We have to use BGRA because DDS is a windows based format
	int altInternalFormat = 0;
	int bitSize = 0;
	switch ( internalFormat ) {
		case GL_COLOR_INDEX8_EXT:
		case GL_COLOR_INDEX:
			// this will not work with dds viewers but we need it in this format to save disk
			// load speed ( i.e. size ) 
			altInternalFormat = GL_COLOR_INDEX;
			bitSize = 24;
		break;
		case 1:
		case GL_INTENSITY8:
		case GL_LUMINANCE8:
		case 3:
		case GL_RGB8:
			altInternalFormat = GL_BGR_EXT;
			bitSize = 24;
		break;
		case GL_LUMINANCE8_ALPHA8:
		case 4:
		case GL_RGBA8:
			altInternalFormat = GL_BGRA_EXT;
			bitSize = 32;
		break;
		case GL_ALPHA8:
			altInternalFormat = GL_ALPHA;
			bitSize = 8;
		break;
		default:
			if ( FormatIsDXT( internalFormat ) ) {
				altInternalFormat = internalFormat;
			} else {
				common->Warning("Unknown or unsupported format for %s", filename);
				return;
			}
	}

	if ( globalImages->image_useOffLineCompression.GetBool() && FormatIsDXT( altInternalFormat ) ) {
		idStr outFile = fileSystem->RelativePathToOSPath( filename, "fs_basepath" );
		idStr inFile = outFile;
		inFile.StripFileExtension();
		inFile.SetFileExtension( "tga" );
		idStr format;
		if ( depth == TD_BUMP ) {
			format = "RXGB +red 0.0 +green 0.5 +blue 0.5";
		} else {
			switch ( altInternalFormat ) {
				case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
					format = "DXT1";
					break;
				case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
					format = "DXT1 -alpha_threshold";
					break;
				case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
					format = "DXT3";
					break;
				case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
					format = "DXT5";
					break;
			}
		}
		globalImages->AddDDSCommand( va( "z:/d3xp/compressonator/thecompressonator -convert \"%s\" \"%s\" %s -mipmaps\n", inFile.c_str(), outFile.c_str(), format.c_str() ) );
		return;
	}


	ddsFileHeader_t header;
	memset( &header, 0, sizeof(header) );
	header.dwSize = sizeof(header);
	header.dwFlags = DDSF_CAPS | DDSF_PIXELFORMAT | DDSF_WIDTH | DDSF_HEIGHT;
	header.dwHeight = uploadHeight;
	header.dwWidth = uploadWidth;

	if ( FormatIsDXT( altInternalFormat ) ) {
		// size (in bytes) of the compressed base image
		header.dwFlags |= DDSF_LINEARSIZE;
		header.dwPitchOrLinearSize = ( ( uploadWidth + 3 ) / 4 ) * ( ( uploadHeight + 3 ) / 4 )*
			(altInternalFormat <= GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 8 : 16);
	}
	else {
		// 4 Byte aligned line width (from nv_dds)
		header.dwFlags |= DDSF_PITCH;
		header.dwPitchOrLinearSize = ( ( uploadWidth * bitSize + 31 ) & -32 ) >> 3;
	}

	header.dwCaps1 = DDSF_TEXTURE;

	if ( numLevels > 1 ) {
		header.dwMipMapCount = numLevels;
		header.dwFlags |= DDSF_MIPMAPCOUNT;
		header.dwCaps1 |= DDSF_MIPMAP | DDSF_COMPLEX;
	}

	header.ddspf.dwSize = sizeof(header.ddspf);
	if ( FormatIsDXT( altInternalFormat ) ) {
		header.ddspf.dwFlags = DDSF_FOURCC;
		switch ( altInternalFormat ) {
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
			header.ddspf.dwFourCC = DDS_MAKEFOURCC('D','X','T','1');
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
			header.ddspf.dwFlags |= DDSF_ALPHAPIXELS;
			header.ddspf.dwFourCC = DDS_MAKEFOURCC('D','X','T','1');
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
			header.ddspf.dwFourCC = DDS_MAKEFOURCC('D','X','T','3');
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			header.ddspf.dwFourCC = DDS_MAKEFOURCC('D','X','T','5');
			break;
		}
	} else {
		header.ddspf.dwFlags = ( internalFormat == GL_COLOR_INDEX8_EXT ) ? DDSF_RGB | DDSF_ID_INDEXCOLOR : DDSF_RGB;
		header.ddspf.dwRGBBitCount = bitSize;
		switch ( altInternalFormat ) {
		case GL_BGRA_EXT:
		case GL_LUMINANCE_ALPHA:
			header.ddspf.dwFlags |= DDSF_ALPHAPIXELS;
			header.ddspf.dwABitMask = 0xFF000000;
			// Fall through
		case GL_BGR_EXT:
		case GL_LUMINANCE:
		case GL_COLOR_INDEX:
			header.ddspf.dwRBitMask = 0x00FF0000;
			header.ddspf.dwGBitMask = 0x0000FF00;
			header.ddspf.dwBBitMask = 0x000000FF;
			break;
		case GL_ALPHA:
			header.ddspf.dwFlags = DDSF_ALPHAPIXELS;
			header.ddspf.dwABitMask = 0xFF000000;
			break;
		default:
			common->Warning( "Unknown or unsupported format for %s", filename );
			return;
		}
	}

	idFile *f = fileSystem->OpenFileWrite( filename );
	if ( f == NULL ) {
		common->Warning( "Could not open %s trying to write precompressed image", filename );
		return;
	}
	common->Printf( "Writing precompressed image: %s\n", filename );

	f->Write( "DDS ", 4 );
	f->Write( &header, sizeof(header) );

	// bind to the image so we can read back the contents
	Bind();

	qglPixelStorei( GL_PACK_ALIGNMENT, 1 );	// otherwise small rows get padded to 32 bits

	int uw = uploadWidth;
	int uh = uploadHeight;

	// Will be allocated first time through the loop
	byte *data = NULL;

	for ( int level = 0 ; level < numLevels ; level++ ) {

		int size = 0;
		if ( FormatIsDXT( altInternalFormat ) ) {
			size = ( ( uw + 3 ) / 4 ) * ( ( uh + 3 ) / 4 ) *
				(altInternalFormat <= GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 8 : 16);
		} else {
			size = uw * uh * (bitSize / 8);
		}

		if (data == NULL) {
			data = (byte *)R_StaticAlloc( size );
		}

		if ( FormatIsDXT( altInternalFormat ) ) {
			qglGetCompressedTexImageARB( GL_TEXTURE_2D, level, data );
		} else {
			qglGetTexImage( GL_TEXTURE_2D, level, altInternalFormat, GL_UNSIGNED_BYTE, data );
		}

		f->Write( data, size );

		uw /= 2;
		uh /= 2;
		if (uw < 1) {
			uw = 1;
		}
		if (uh < 1) {
			uh = 1;
		}
	}

	if (data != NULL) {
		R_StaticFree( data );
	}

	fileSystem->CloseFile( f );
}
*/

/*
================
ShouldImageBePartialCached

Returns true if there is a precompressed image, and it is large enough
to be worth caching
================
*/
bool idImage::ShouldImageBePartialCached() {
	if ( !glConfig.textureCompressionAvailable ) {
		return false;
	}

	if ( !globalImages->image_useCache.GetBool() ) {
		return false;
	}

	// the allowDownSize flag does double-duty as don't-partial-load
	if ( !allowDownSize ) {
		return false;
	}

	if ( globalImages->image_cacheMinK.GetInteger() <= 0 ) {
		return false;
	}

	// if we are doing a copyFiles, make sure the original images are referenced
	if ( fileSystem->PerformingCopyFiles() ) {
		return false;
	}

	char	filename[MAX_IMAGE_NAME];
	ImageProgramStringToCompressedFileName( imgName, filename );

	// get the file timestamp
	fileSystem->ReadFile( filename, NULL, &timestamp );

	if ( timestamp == FILE_NOT_FOUND_TIMESTAMP ) {
		return false;
	}

	// open it and get the file size
	idFile *f;

	f = fileSystem->OpenFileRead( filename );
	if ( !f ) {
		return false;
	}

	int	len = f->Length();
	fileSystem->CloseFile( f );

	if ( len <= globalImages->image_cacheMinK.GetInteger() * 1024 ) {
		return false;
	}

	// we do want to do a partial load
	return true;
}

/*
================
CheckPrecompressedImage

If fullLoad is false, only the small mip levels of the image will be loaded
================
*/
bool idImage::CheckPrecompressedImage( bool fullLoad ) {
	if ( !glConfig.isInitialized || !glConfig.textureCompressionAvailable ) {
		return false;
	}

#if 1 // ( _D3XP had disabled ) - Allow grabbing of DDS's from original Doom pak files
	// if we are doing a copyFiles, make sure the original images are referenced
	if ( fileSystem->PerformingCopyFiles() ) {
		return false;
	}
#endif

	if ( depth == TD_BUMP && globalImages->image_useNormalCompression.GetInteger() != 2 ) {
		return false;
	}

	// god i love last minute hacks :-)
	if ( com_machineSpec.GetInteger() >= 1 && com_videoRam.GetInteger() >= 128 && imgName.Icmpn( "lights/", 7 ) == 0 ) {
		return false;
	}

	char filename[MAX_IMAGE_NAME];
	ImageProgramStringToCompressedFileName( imgName, filename );

	// get the file timestamp
	ID_TIME_T precompTimestamp;
	fileSystem->ReadFile( filename, NULL, &precompTimestamp );


	if ( precompTimestamp == FILE_NOT_FOUND_TIMESTAMP ) {
		return false;
	}

	if ( !generatorFunction && timestamp != FILE_NOT_FOUND_TIMESTAMP ) {
		if ( precompTimestamp < timestamp ) {
			// The image has changed after being precompressed
			return false;
		}
	}

	timestamp = precompTimestamp;

	// open it and just read the header
	idFile *f;

	f = fileSystem->OpenFileRead( filename );
	if ( !f ) {
		return false;
	}

	int	len = f->Length();
	if ( len < sizeof( ddsFileHeader_t ) ) {
		fileSystem->CloseFile( f );
		return false;
	}

	if ( !fullLoad && len > globalImages->image_cacheMinK.GetInteger() * 1024 ) {
		len = globalImages->image_cacheMinK.GetInteger() * 1024;
	}

	byte *data = (byte *)R_StaticAlloc( len );

	f->Read( data, len );

	fileSystem->CloseFile( f );

	unsigned long magic = LittleLong( *(unsigned long *)data );
	ddsFileHeader_t	*_header = (ddsFileHeader_t *)(data + 4);
	int ddspf_dwFlags = LittleLong( _header->ddspf.dwFlags );

	if ( magic != DDS_MAKEFOURCC('D', 'D', 'S', ' ')) {
		common->Printf( "CheckPrecompressedImage( %s ): magic != 'DDS '\n", imgName.c_str() );
		R_StaticFree( data );
		return false;
	}

	// if we don't support color index textures, we must load the full image
	// should we just expand the 256 color image to 32 bit for upload?
	if ( ddspf_dwFlags & DDSF_ID_INDEXCOLOR ) {
		R_StaticFree( data );
		return false;
	}

	// upload all the levels
	UploadPrecompressedImage( data, len );

	R_StaticFree( data );

	return true;
}

/*
===================
UploadPrecompressedImage

This can be called by the front end during nromal loading,
or by the backend after a background read of the file
has completed
===================
*/
void idImage::UploadPrecompressedImage( byte *data, int len ) {
	ddsFileHeader_t	*header = (ddsFileHeader_t *)(data + 4);

	// ( not byte swapping dwReserved1 dwReserved2 )
	header->dwSize = LittleLong( header->dwSize );
	header->dwFlags = LittleLong( header->dwFlags );
	header->dwHeight = LittleLong( header->dwHeight );
	header->dwWidth = LittleLong( header->dwWidth );
	header->dwPitchOrLinearSize = LittleLong( header->dwPitchOrLinearSize );
	header->dwDepth = LittleLong( header->dwDepth );
	header->dwMipMapCount = LittleLong( header->dwMipMapCount );
	header->dwCaps1 = LittleLong( header->dwCaps1 );
	header->dwCaps2 = LittleLong( header->dwCaps2 );

	header->ddspf.dwSize = LittleLong( header->ddspf.dwSize );
	header->ddspf.dwFlags = LittleLong( header->ddspf.dwFlags );
	header->ddspf.dwFourCC = LittleLong( header->ddspf.dwFourCC );
	header->ddspf.dwRGBBitCount = LittleLong( header->ddspf.dwRGBBitCount );
	header->ddspf.dwRBitMask = LittleLong( header->ddspf.dwRBitMask );
	header->ddspf.dwGBitMask = LittleLong( header->ddspf.dwGBitMask );
	header->ddspf.dwBBitMask = LittleLong( header->ddspf.dwBBitMask );
	header->ddspf.dwABitMask = LittleLong( header->ddspf.dwABitMask );

	type = TT_2D;			// FIXME: we may want to support pre-compressed cube maps in the future

	uploadWidth = header->dwWidth;
	uploadHeight = header->dwHeight;

	int numMipmaps = 1;
	if (header->dwFlags & DDSF_MIPMAPCOUNT) {
		numMipmaps = header->dwMipMapCount;
	}

	// Create all the FuryGL objects...
	FglFormat texfmt = FGL_FORMAT_R8G8B8A8_UNORM;
	if (header->ddspf.dwRGBBitCount == 8)
		texfmt = FGL_FORMAT_R8_UNORM;

	FglExtent3D extent{ (uint32_t)uploadWidth, (uint32_t)uploadHeight, 1 };
	FglImageCreateInfo createInfo{ FGL_IMAGE_TYPE_2D, texfmt, FGL_IMAGE_TILING_LINEAR, extent, (uint32_t)numMipmaps, 1 };
	ID_FGL_CHECK(fglCreateImage(fglcontext.device, &createInfo, nullptr, &m_image));

	FglMemoryRequirements memoryReqs;
	fglGetImageMemoryRequirements(fglcontext.device, m_image, &memoryReqs);

	ID_FGL_CHECK(fglAllocateMemory(fglcontext.device, memoryReqs.size, nullptr, &m_imageMemory));

	ID_FGL_CHECK(fglBindImageMemory(fglcontext.device, m_image, m_imageMemory, 0));

	FglImageViewCreateInfo viewInfo{ m_image, FGL_IMAGE_VIEW_TYPE_2D, texfmt };
	ID_FGL_CHECK(fglCreateImageView(fglcontext.device, &viewInfo, nullptr, &m_imageView));
	

	byte* tempBuffer = nullptr;
	const byte* src = nullptr;

	byte* imagedata = data + sizeof(ddsFileHeader_t) + 4;

	precompressedFile = true;

	int uw = uploadWidth;
	int uh = uploadHeight;
	int blockSize = 0;
	
    if ( header->ddspf.dwFlags & DDSF_FOURCC )
	{
        switch ( header->ddspf.dwFourCC )
		{
		case DDS_MAKEFOURCC('D', 'X', 'T', '1'):
		case DDS_MAKEFOURCC('D', 'X', 'T', '3'):
		case DDS_MAKEFOURCC('D', 'X', 'T', '5'):
		case DDS_MAKEFOURCC('R', 'X', 'G', 'B'):
			tempBuffer = new byte[uploadWidth * uploadHeight * 4];

			switch (header->ddspf.dwFourCC)
			{
			case DDS_MAKEFOURCC('D', 'X', 'T', '1'):
				blockSize = BCDEC_BC1_BLOCK_SIZE;
				break;
			case DDS_MAKEFOURCC('D', 'X', 'T', '3'):
				blockSize = BCDEC_BC2_BLOCK_SIZE;
				break;
			case DDS_MAKEFOURCC('D', 'X', 'T', '5'):
			case DDS_MAKEFOURCC('R', 'X', 'G', 'B'):
				blockSize = BCDEC_BC3_BLOCK_SIZE;
				break;
			}

			for (int mip = 0; mip < numMipmaps; ++mip)
			{
				int size = ((uw + 3) / 4) * ((uh + 3) / 4) * blockSize;

				src = imagedata;

				for (int i = 0; i < uh; i += 4)
				{
					for (int j = 0; j < uw; j += 4)
					{
						byte* dst = tempBuffer + (i * uw + j) * 4;
						switch (header->ddspf.dwFourCC)
						{
						case DDS_MAKEFOURCC('D', 'X', 'T', '1'):
							bcdec_bc1(src, dst, uw * 4);
							break;
						case DDS_MAKEFOURCC('D', 'X', 'T', '3'):
							bcdec_bc2(src, dst, uw * 4);
							break;
						case DDS_MAKEFOURCC('D', 'X', 'T', '5'):
						case DDS_MAKEFOURCC('R', 'X', 'G', 'B'):
							bcdec_bc3(src, dst, uw * 4);
							break;
						}
						src += blockSize;
					}
				}

				FglUploadImageDataInfo uploadInfo{ FGL_FORMAT_R8G8B8A8_UNORM, 0, (uint32_t)mip, tempBuffer };
				ID_FGL_CHECK(fglUploadImageData(fglcontext.device, m_image, &uploadInfo, nullptr));

				imagedata += size;
				uw /= 2;
				uh /= 2;
				if (uw < 1) {
					uw = 1;
				}
				if (uh < 1) {
					uh = 1;
				}
			}

			delete[] tempBuffer;
			break;
        default:
            common->Warning( "Invalid compressed internal format\n" );
            return;
        }
    }
	else if ( ((header->ddspf.dwFlags & DDSF_RGBA) || (header->ddspf.dwFlags & DDSF_RGB)) && header->ddspf.dwRGBBitCount == 32 )
	{
		tempBuffer = new byte[uploadWidth * uploadHeight * 4];

		for (int mip = 0; mip < numMipmaps; ++mip)
		{
			int size = uw * uh * (header->ddspf.dwRGBBitCount / 8);

			src = imagedata;
			for (int i = 0; i < uh; ++i)
			{
				for (int j = 0; j < uw; ++j, src += 4)
				{
					byte* dst = tempBuffer + (i * uw + j) * 4;

					dst[0] = src[2];
					dst[1] = src[1];
					dst[2] = src[0];
					dst[3] = src[3];
				}
			}

			FglUploadImageDataInfo uploadInfo{ FGL_FORMAT_R8G8B8A8_UNORM, 0, (uint32_t)mip, tempBuffer };
			ID_FGL_CHECK(fglUploadImageData(fglcontext.device, m_image, &uploadInfo, nullptr));

			imagedata += size;
			uw /= 2;
			uh /= 2;
			if (uw < 1) {
				uw = 1;
			}
			if (uh < 1) {
				uh = 1;
			}
		}

		delete[] tempBuffer;
    }
	else if ( ( header->ddspf.dwFlags & DDSF_RGB ) && header->ddspf.dwRGBBitCount == 24 )
	{
		if ( header->ddspf.dwFlags & DDSF_ID_INDEXCOLOR )
		{ 
			common->Warning("Unhandled indexed color format\n");
			return;
		}
		else
		{
			tempBuffer = new byte[uploadWidth * uploadHeight * 4];

			for (int mip = 0; mip < numMipmaps; ++mip)
			{
				int size = uw * uh * (header->ddspf.dwRGBBitCount / 8);

				src = imagedata;
				for (int i = 0; i < uh; ++i)
				{
					for (int j = 0; j < uw; ++j, src += 3)
					{
						byte* dst = tempBuffer + (i * uw + j) * 4;

						dst[0] = src[2];
						dst[1] = src[1];
						dst[2] = src[0];
						dst[3] = 0xFF;
					}
				}

				FglUploadImageDataInfo uploadInfo{ FGL_FORMAT_R8G8B8A8_UNORM, 0, (uint32_t)mip, tempBuffer };
				ID_FGL_CHECK(fglUploadImageData(fglcontext.device, m_image, &uploadInfo, nullptr));

				imagedata += size;
				uw /= 2;
				uh /= 2;
				if (uw < 1) {
					uw = 1;
				}
				if (uh < 1) {
					uh = 1;
				}
			}

			delete[] tempBuffer;
		}
	}
	else if ( header->ddspf.dwRGBBitCount == 8 )
	{
		for (int mip = 0; mip < numMipmaps; ++mip)
		{
			int size = uw * uh;

			FglUploadImageDataInfo uploadInfo{ FGL_FORMAT_R8_UNORM, 0, (uint32_t)mip, imagedata };
			ID_FGL_CHECK(fglUploadImageData(fglcontext.device, m_image, &uploadInfo, nullptr));

			imagedata += size;
			uw /= 2;
			uh /= 2;
			if (uw < 1) {
				uw = 1;
			}
			if (uh < 1) {
				uh = 1;
			}
		}
	}
	else
{
		common->Warning( "Invalid uncompressed internal format\n" );
		return;
	}

	CreateSampler();
}

/*
===============
ActuallyLoadImage

Absolutely every image goes through this path
On exit, the idImage will have a valid OpenGL texture number that can be bound
===============
*/
void	idImage::ActuallyLoadImage( bool checkForPrecompressed, bool fromBackEnd ) {
	int		width, height;
	byte	*pic;

	// this is the ONLY place generatorFunction will ever be called
	if ( generatorFunction ) {
		generatorFunction( this );
		return;
	}

	// if we are a partial image, we are only going to load from a compressed file
	if ( isPartialImage ) {
		if ( CheckPrecompressedImage( false ) ) {
			return;
		}
		// this is an error -- the partial image failed to load
		MakeDefault();
		return;
	}

	//
	// load the image from disk
	//
	if ( cubeFiles != CF_2D ) {
		byte	*pics[6];

		// we don't check for pre-compressed cube images currently
		R_LoadCubeImages( imgName, cubeFiles, pics, &width, &timestamp );

		if ( pics[0] == NULL ) {
			common->Warning( "Couldn't load cube image: %s", imgName.c_str() );
			MakeDefault();
			return;
		}

		GenerateCubeImage( (const byte **)pics, width, filter, allowDownSize, depth );
		precompressedFile = false;

		for ( int i = 0 ; i < 6 ; i++ ) {
			if ( pics[i] ) {
				R_StaticFree( pics[i] );
			}
		}
	} else {
		// see if we have a pre-generated image file that is
		// already image processed and compressed
		if ( checkForPrecompressed && globalImages->image_usePrecompressedTextures.GetBool() ) {
			if ( CheckPrecompressedImage( true ) ) {
				// we got the precompressed image
				return;
			}
			// fall through to load the normal image
		}

		R_LoadImageProgram( imgName, &pic, &width, &height, &timestamp, &depth );

		if ( pic == NULL ) {
			common->Warning( "Couldn't load image: %s", imgName.c_str() );
			MakeDefault();
			return;
		}
/*
		// swap the red and alpha for rxgb support
		// do this even on tga normal maps so we only have to use
		// one fragment program
		// if the image is precompressed ( either in palletized mode or true rxgb mode )
		// then it is loaded above and the swap never happens here
		if ( depth == TD_BUMP && globalImages->image_useNormalCompression.GetInteger() != 1 ) {
			for ( int i = 0; i < width * height * 4; i += 4 ) {
				pic[ i + 3 ] = pic[ i ];
				pic[ i ] = 0;
			}
		}
*/
		// build a hash for checking duplicate image files
		// NOTE: takes about 10% of image load times (SD)
		// may not be strictly necessary, but some code uses it, so let's leave it in
		imageHash = MD4_BlockChecksum( pic, width * height * 4 );

		GenerateImage( pic, width, height, filter, allowDownSize, repeat, depth );
		timestamp = timestamp;
		precompressedFile = false;

		R_StaticFree( pic );

		// write out the precompressed version of this file if needed
		//WritePrecompressedImage();
	}
}

//=========================================================================================================

/*
===============
PurgeImage
===============
*/
void idImage::PurgeImage() {
	if (IsLoaded()) {
		fglDestroyImageView(fglcontext.device, m_imageView, nullptr);
		fglDestroyImage(fglcontext.device, m_image, nullptr);
		fglFreeMemory(fglcontext.device, m_imageMemory, nullptr);

		fglDestroySampler(fglcontext.device, m_sampler, nullptr);

		m_image = FGL_NULL_HANDLE;
		m_imageView = FGL_NULL_HANDLE;
		m_imageMemory = FGL_NULL_HANDLE;
		m_sampler = FGL_NULL_HANDLE;
	}

	// clear all the current binding caches, so the next bind will do a real one
	for ( int i = 0 ; i < MAX_MULTITEXTURE_UNITS ; i++ ) {
		backEnd.glState.tmu[i].current2DMap = -1;
		backEnd.glState.tmu[i].current3DMap = -1;
		backEnd.glState.tmu[i].currentCubeMap = -1;
	}
}

void idImage::AllocImage(int width, int height, int numMips, int numLayers, FglFormat format, FglImageViewType viewType, FglImageTiling tiling)
{
	FglExtent3D extent{ (uint32_t)width, (uint32_t)height, 1 };

	FglImageCreateInfo createInfo{ FGL_IMAGE_TYPE_2D, format, tiling, extent, (uint32_t)numMips, (uint32_t)numLayers };
	ID_FGL_CHECK(fglCreateImage(fglcontext.device, &createInfo, nullptr, &m_image));

	FglMemoryRequirements memoryReqs;
	fglGetImageMemoryRequirements(fglcontext.device, m_image, &memoryReqs);

	ID_FGL_CHECK(fglAllocateMemory(fglcontext.device, memoryReqs.size, nullptr, &m_imageMemory));

	ID_FGL_CHECK(fglBindImageMemory(fglcontext.device, m_image, m_imageMemory, 0));

	FglImageViewCreateInfo viewInfo{ m_image, viewType, format };
	ID_FGL_CHECK(fglCreateImageView(fglcontext.device, &viewInfo, nullptr, &m_imageView));

	m_extent = extent;
}

/*
==============
Bind

Automatically enables 2D mapping, cube mapping, or 3D texturing if needed
==============
*/
void idImage::Bind(int slot) {
	if ( tr.logFile ) {
		RB_LogComment( "idImage::Bind( %s, %d )\n", imgName.c_str(), slot );
	}

	// if this is an image that we are caching, move it to the front of the LRU chain
	if ( partialImage ) {
		if ( cacheUsageNext ) {
			// unlink from old position
			cacheUsageNext->cacheUsagePrev = cacheUsagePrev;
			cacheUsagePrev->cacheUsageNext = cacheUsageNext;
		}
		// link in at the head of the list
		cacheUsageNext = globalImages->cacheLRU.cacheUsageNext;
		cacheUsagePrev = &globalImages->cacheLRU;

		cacheUsageNext->cacheUsagePrev = this;
		cacheUsagePrev->cacheUsageNext = this;
	}

	// load the image if necessary (FIXME: not SMP safe!)
	if ( !IsLoaded()) {
		if ( partialImage ) {
			// if we have a partial image, go ahead and use that
			this->partialImage->Bind(slot);

			// start a background load of the full thing if it isn't already in the queue
			if ( !backgroundLoadInProgress ) {
				StartBackgroundImageLoad();
			}
			return;
		}

		// load the image on demand here, which isn't our normal game operating mode
		ActuallyLoadImage( true, true );	// check for precompressed, load is from back end
	}


	// bump our statistic counters
	frameUsed = backEnd.frameCount;
	bindCount++;

	fglcontext.imageParms[slot] = this;
}

/*
====================
CopyFramebuffer
====================
*/
void idImage::CopyFramebuffer(FglImage src, int imageWidth, int imageHeight)
{
	// Need the copy to be a power of two for sampling
	auto nextPot = [](int size)
	{
		int pot = 1;
		while (pot < size)
			pot <<= 1;
		return pot;
	};

	int potWidth = nextPot(imageWidth);
	int potHeight = nextPot(imageHeight);

	if (m_extent.width != potWidth || m_extent.height != potHeight)
	{
		PurgeImage();
		AllocImage(potWidth, potHeight, 1, 1, FGL_FORMAT_R8G8B8A8_UNORM, FGL_IMAGE_VIEW_TYPE_2D, FGL_IMAGE_TILING_FRAMEBUFFER);
		CreateSampler();
	}

	FglImageCopy region{};
	region.srcSubresource.aspectMask = FGL_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = 1;
	region.dstSubresource.aspectMask = FGL_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.layerCount = 1;
	region.extent = FglExtent3D{ (uint32_t)imageWidth, (uint32_t)imageHeight, 1 };
	fglCmdCopyImage(fglcontext.cmdbuf, src, m_image, 1, &region);

	backEnd.c_copyFrameBuffer++;
}

/*
====================
CopyDepthbuffer

This should just be part of copyFramebuffer once we have a proper image type field
====================
*/
void idImage::CopyDepthbuffer( int x, int y, int imageWidth, int imageHeight ) {

	common->Warning("TODO: CopyDepthbuffer");
	return;

	//Bind();

	// if the size isn't a power of 2, the image must be increased in size
	int	potWidth, potHeight;

	potWidth = MakePowerOfTwo( imageWidth );
	potHeight = MakePowerOfTwo( imageHeight );

	if ( uploadWidth != potWidth || uploadHeight != potHeight ) {
		uploadWidth = potWidth;
		uploadHeight = potHeight;
		if ( potWidth == imageWidth && potHeight == imageHeight ) {
			qglCopyTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, x, y, imageWidth, imageHeight, 0 );
		} else {
			// we need to create a dummy image with power of two dimensions,
			// then do a qglCopyTexSubImage2D of the data we want
			qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, potWidth, potHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL );
			qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, x, y, imageWidth, imageHeight );
		}
	} else {
		// otherwise, just subimage upload it so that drivers can tell we are going to be changing
		// it and don't try and do a texture compression or some other silliness
		qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, x, y, imageWidth, imageHeight );
	}

//	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
//	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
}

/*
=============
RB_UploadScratchImage

if rows = cols * 6, assume it is a cube map animation
=============
*/
void idImage::UploadScratch( const byte *data, int cols, int rows ) {
	int			i;

	// Always purge the texture and build it from scratch
	PurgeImage();

	auto numMipLevels = [](int width, int height)
	{
		int size = width > height ? width : height;
		uint32_t mips = 0;
		while (size)
		{
			++mips;
			size >>= 1;
		}
		return mips;
	};

	// if rows = cols * 6, assume it is a cube map animation
	if ( rows == cols * 6 ) {
		if ( type != TT_CUBIC ) {
			type = TT_CUBIC;
			uploadWidth = -1;	// for a non-sub upload
		}

		rows /= 6;

		uploadWidth = cols;
		uploadHeight = rows;

		AllocImage(uploadWidth, uploadHeight, numMipLevels(uploadWidth, uploadHeight), 6, FGL_FORMAT_R8G8B8A8_UNORM, FGL_IMAGE_VIEW_TYPE_CUBEMAP, FGL_IMAGE_TILING_LINEAR);

		const byte* ppData[6];
		for (i = 0; i < 6; ++i)
			ppData[i] = data + cols * rows * 4 * i;

		FglConvertImageInfo convertInfo{ FGL_FORMAT_R8G8B8A8_UNORM, 0, 0, 1, (void**)ppData };
		ID_FGL_CHECK(fglConvertImageData(fglcontext.device, m_image, &convertInfo, nullptr));

	} else {
		// otherwise, it is a 2D image
		if ( type != TT_2D ) {
			type = TT_2D;
			uploadWidth = -1;	// for a non-sub upload
		}

		uploadWidth = cols;
		uploadHeight = rows;

		AllocImage(uploadWidth, uploadHeight, numMipLevels(uploadWidth, uploadHeight), 1, FGL_FORMAT_R8G8B8A8_UNORM, FGL_IMAGE_VIEW_TYPE_2D, FGL_IMAGE_TILING_LINEAR);

		FglConvertImageInfo convertInfo{ FGL_FORMAT_R8G8B8A8_UNORM, 0, 0, 1, (void**)&data };
		ID_FGL_CHECK(fglConvertImageData(fglcontext.device, m_image, &convertInfo, nullptr));
	}

	CreateSampler();
}


void idImage::SetClassification( int tag ) {
	classification = tag;
}

/*
==================
StorageSize
==================
*/
int idImage::StorageSize() const {
	int		baseSize;

	if ( !m_image ) {
		return 0;
	}

	switch ( type ) {
	default:
	case TT_2D:
		baseSize = uploadWidth*uploadHeight;
		break;
	case TT_CUBIC:
		baseSize = 6 * uploadWidth*uploadHeight;
		break;
	}

	baseSize *= BitsForInternalFormat( internalFormat );

	baseSize /= 8;

	// account for mip mapping
	baseSize = baseSize * 4 / 3;

	return baseSize;
}

/*
==================
Print
==================
*/
void idImage::Print() const {
	if ( precompressedFile ) {
		common->Printf( "P" );
	} else if ( generatorFunction ) {
		common->Printf( "F" );
	} else {
		common->Printf( " " );
	}

	switch ( type ) {
	case TT_2D:
		common->Printf( " " );
		break;
	case TT_CUBIC:
		common->Printf( "C" );
		break;
	default:
		common->Printf( "<BAD TYPE:%i>", type );
		break;
	}

	common->Printf( "%4i %4i ",	uploadWidth, uploadHeight );

	switch( filter ) {
	case TF_DEFAULT:
		common->Printf( "dflt " );
		break;
	case TF_LINEAR:
		common->Printf( "linr " );
		break;
	case TF_NEAREST:
		common->Printf( "nrst " );
		break;
	default:
		common->Printf( "<BAD FILTER:%i>", filter );
		break;
	}

	switch ( internalFormat ) {
	case FGL_FORMAT_R8G8B8A8_UNORM:
		common->Printf( "RGBA  " );
		break;
	default:
		common->Printf( "<BAD FORMAT:%i>", internalFormat );
		break;
	}

	switch ( repeat ) {
	case TR_REPEAT:
		common->Printf( "rept " );
		break;
	case TR_CLAMP_TO_ZERO:
		common->Printf( "zero " );
		break;
	case TR_CLAMP_TO_ZERO_ALPHA:
		common->Printf( "azro " );
		break;
	case TR_CLAMP:
		common->Printf( "clmp " );
		break;
	default:
		common->Printf( "<BAD REPEAT:%i>", repeat );
		break;
	}
	
	common->Printf( "%4ik ", StorageSize() / 1024 );

	common->Printf( " %s\n", imgName.c_str() );
}
