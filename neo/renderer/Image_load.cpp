/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

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

#include "sys/platform.h"
#include "idlib/hashing/MD4.h"
#include "renderer/tr_local.h"
#include "renderer/Cinematic.h"
#include "framework/LoadTiming.h"

#include "renderer/Image.h"


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
int idImage::BitsForInternalFormat( int internalFormat ) const {
	switch ( internalFormat ) {
	case 1:
	case 2:
	case 3:
	case 4:
		return 32;
	case GL_RGBA4:
		return 16;
	case GL_RGB5_A1:
		return 16;
	default:
		common->Error( "R_BitsForInternalFormat: BAD FORMAT:%i", internalFormat );
	}
	return 0;
}

/*
==================
UploadCompressedNormalMap

Create a 256 color palette to be used by compressed normal maps
==================
*/
void idImage::UploadCompressedNormalMap( int width, int height, const byte *rgba, int mipLevel ) {
	byte	*normals;
	const byte	*in;
	byte	*out;
	int		i, j;
	int		x, y, z;
	int		row;

	// OpenGL's pixel packing rule
	row = width < 4 ? 4 : width;

	normals = (byte *)_alloca( row * height );
	if ( !normals ) {
		common->Error( "R_UploadCompressedNormalMap: _alloca failed" );
	}

	in = rgba;
	out = normals;
	for ( i = 0 ; i < height ; i++, out += row, in += width * 4 ) {
		for ( j = 0 ; j < width ; j++ ) {
			x = in[ j * 4 + 0 ];
			y = in[ j * 4 + 1 ];
			z = in[ j * 4 + 2 ];

			int c;
			if ( x == 128 && y == 128 && z == 128 ) {
				// the "nullnormal" color
				c = 255;
			} else {
				c = ( globalImages->originalToCompressed[x] << 4 ) | globalImages->originalToCompressed[y];
				if ( c == 255 ) {
					c = 254;	// don't use the nullnormal color
				}
			}
			out[j] = c;
		}
	}

	if ( mipLevel == 0 ) {
		// Optionally write out the paletized normal map to a .tga
		if ( globalImages->image_writeNormalTGAPalletized.GetBool() ) {
			char filename[MAX_IMAGE_NAME];
			ImageProgramStringToCompressedFileName( imgName, filename );
			char *ext = strrchr(filename, '.');
			if ( ext ) {
				strcpy(ext, "_pal.tga");
				R_WritePalTGA( filename, normals, globalImages->compressedPalette, width, height);
			}
		}
	}
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
==================
SetImageFilterAndRepeat
==================
*/
void idImage::SetImageFilterAndRepeat() const {
	// set the minimize / maximize filtering
	switch( filter ) {
	case TF_DEFAULT:
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, globalImages->textureMinFilter );
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, globalImages->textureMaxFilter );
		break;
	case TF_LINEAR:
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		break;
	case TF_NEAREST:
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		break;
	default:
		common->FatalError( "R_CreateImage: bad texture filter" );
	}

	if ( glConfig.anisotropicAvailable ) {
		// only do aniso filtering on mip mapped images
		if ( filter == TF_DEFAULT ) {
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, globalImages->textureAnisotropy );
		} else {
			qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );
		}
	}

	// set the wrap/clamp modes
	switch( repeat ) {
	case TR_REPEAT:
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		break;
	case TR_CLAMP_TO_BORDER:
		// GL_CLAMP_TO_BORDER not available
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		break;
	case TR_CLAMP_TO_ZERO:
	case TR_CLAMP_TO_ZERO_ALPHA:
	case TR_CLAMP:
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		break;
	default:
		common->FatalError( "R_CreateImage: bad texture repeat" );
	}
}

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


//Code from raspberrypi q3 for ETC image compression

static int isopaque(GLint width, GLint height, const GLvoid *pixels) {
	unsigned char const *cpixels = (unsigned char const *)pixels;
	int i;
	for (i = 0; i < width * height; i++) {
		if (cpixels[i*4+3] != 0xff)
			return 0;
	}
	return 1;
}

/*
===============================================================================

ETC cache, one file per image: <fs_savepath>/<game>/dds/<image>.etx

	etxHeader_t
	for each mip level, largest first, down to 1x1:
		int		size
		byte	data[size], zero padded to a multiple of 4

The file holds the complete mip chain of final GPU data (after the image
program, border fixups, mipmapping and ETC1/RGBA4444 conversion), like a
.dds file, always starting at the full source size (limited only by the
GPU's maximum texture size).

The file is checked in ActuallyLoadImage() before the source image is read.
The image_downSize* cvars only pick the level to start uploading from, and
only that level and the smaller ones are read from the file, so changing
the limits never needs a rebuild. The image is only built from the source
again (and the file rewritten) if the source or the image parameters
changed.

===============================================================================
*/

#define ETX_MAGIC		0x58544544	// "DETX"
#define ETX_VERSION		1
#define ETX_MAX_LEVELS	16

enum {
	ETX_ETC1		= 0,
	ETX_RGBA4444	= 1
};

typedef struct {
	int			magic;
	int			version;
	int			format;			// ETX_ETC1 or ETX_RGBA4444, same for all levels
	int			width;			// top level stored in the file (source size)
	int			height;
	int			numLevels;		// full chain, top level down to 1x1
	int			sourceWidth;	// source image size, before any downsizing
	int			sourceHeight;
	int			depthIn;		// image depth before the image program ran
	int			depthOut;		// image depth after the image program ran
	int			repeat;
	int			roundDown;		// image_roundDown changes the source size
	long long	timestamp;		// newest source file (0 for files inside .pk4)
} etxHeader_t;

// set by ActuallyLoadImage() around GenerateImage(), so only images that were
// really loaded from disk get cached (not generated or default images)
static bool			etxWantCache = false;
static int			etxDepthIn = 0;

// level data collected while GenerateImage() uploads the mip levels
static bool			etxBuilding = false;
static int			etxFormat = 0;
static int			etxLevels = 0;
static byte *		etxData = NULL;
static int			etxDataSize = 0;
static int			etxDataAlloced = 0;
static int			etxTopWidth = 0;
static int			etxTopHeight = 0;
static int			etxSourceWidth = 0;
static int			etxSourceHeight = 0;

static void EtxBegin( bool enable ) {
	etxBuilding = enable;
	etxFormat = 0;
	etxLevels = 0;
	etxDataSize = 0;
}

static void EtxAppendLevel( int format, const void *data, int size ) {
	if ( !etxBuilding ) {
		return;
	}
	if ( ( etxLevels > 0 && format != etxFormat ) || etxLevels >= ETX_MAX_LEVELS ) {
		etxBuilding = false;	// shouldn't happen, just don't cache this image
		return;
	}
	const int padded = ( size + 3 ) & ~3;
	const int needed = etxDataSize + 4 + padded;
	if ( needed > etxDataAlloced ) {
		int newSize = etxDataAlloced ? etxDataAlloced : 256 * 1024;
		while ( newSize < needed ) {
			newSize *= 2;
		}
		byte *newData = (byte *)realloc( etxData, newSize );
		if ( newData == NULL ) {
			etxBuilding = false;
			return;
		}
		etxData = newData;
		etxDataAlloced = newSize;
	}
	memcpy( etxData + etxDataSize, &size, 4 );
	memcpy( etxData + etxDataSize + 4, data, size );
	memset( etxData + etxDataSize + 4 + size, 0, padded - size );
	etxDataSize = needed;
	etxFormat = format;
	etxLevels++;
}

static void EtxFreeBuffer() {
	// keep the buffer around during level loads, but don't hold on to a
	// big one forever
	if ( etxDataAlloced > 4 * 1024 * 1024 ) {
		free( etxData );
		etxData = NULL;
		etxDataAlloced = 0;
	}
	etxDataSize = 0;
}

void rgba4444_convert_tex_image(
    char* cachefname,
    GLenum target,
    GLint level,
    GLenum internalformat,
    GLsizei width,
    GLsizei height,
    GLint border,
    GLenum format,
    GLenum type,
    const GLvoid *pixels) {
	unsigned char const *cpixels = (unsigned char const *)pixels;
	// aligned buffer for the driver (the original wrote 16-bit texels at an odd address)
	unsigned short *rgba4444data = (unsigned short *)malloc(2*width*height);
	int i;
	for (i = 0; i < width * height; i++) {
		unsigned char r,g,b,a;
		r = cpixels[4*i]>>4;
		g = cpixels[4*i+1]>>4;
		b = cpixels[4*i+2]>>4;
		a = cpixels[4*i+3]>>4;
		rgba4444data[i] = r << 12 | g << 8 | b << 4 | a;
	}
	// 16-bit texels: rows of 1-pixel-wide mip levels are only 2 bytes long
	qglPixelStorei(GL_UNPACK_ALIGNMENT, 2);
	qglTexImage2D(target, level, format, width, height,border,format,GL_UNSIGNED_SHORT_4_4_4_4,rgba4444data);
	qglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	if (cachefname!=0) {
		unsigned char *filedata = (unsigned char *)malloc(width*height*2+1);
		filedata[0]=1;
		memcpy(filedata+1, rgba4444data, width*height*2);
		fileSystem->WriteFile(cachefname, filedata, width*height*2+1);
		free(filedata);
	}
	free(rgba4444data);
}
//#define USE_RG_ETC1
#ifdef USE_RG_ETC1
#include "etc_rg_etc1.h"
#else
#include "etc1_android.h"
#endif

unsigned int etc1_data_size(unsigned int width, unsigned int height) {
	return (((width + 3) & ~3) * ((height + 3) & ~3)) >> 1;
}

void etc1_compress_tex_image(
    char* cachefname,
    GLenum target,
    GLint level,
    GLenum internalformat,
    GLsizei width,
    GLsizei height,
    GLint border,
    GLenum format,
    GLenum type,
    const GLvoid *pixels) {
	unsigned char const *cpixels = (unsigned char const *)pixels;
	unsigned char *etc1data;
	unsigned int size=etc1_data_size(width,height);
	etc1data = (unsigned char *)malloc(size+1);
	etc1data[0]=0;
	etc1data++;
#ifdef USE_RG_ETC1
	rg_etc1::etc1_encode_image(cpixels, width, height,
	                           4, width*4, etc1data);
#else
	etc1_encode_image(cpixels, width, height,
	                  4, width*4, etc1data);
#endif
	qglCompressedTexImage2D(
	    target,
	    level,
	    GL_ETC1_RGB8_OES,
	    width,
	    height,
	    0,
	    size,
	    etc1data);
	etc1data--;
	if (cachefname!=0) {
		fileSystem->WriteFile(cachefname, etc1data, size+1);
	}
	free(etc1data);
}

int etcavail(char* cachefname) {
	return (r_useETC1Cache.GetBool())&&(r_useETC1.GetBool())&&(cachefname!=0)&&(fileSystem->ReadFile(cachefname,0,0)!=-1);
}

int uploadetc(char* cachefname,GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type) {
	char* tmp;
	int failed=0;

	if( !etcavail( cachefname ) )
		return 1;

	int sz = fileSystem->ReadFile(cachefname,(void**)&tmp,0);

	if( sz == -1 )
		return 1;

	// The cache file is: 1 header byte (0 = ETC1, 1 = RGBA4444) + texel data.
	// The data starts at an odd offset, so copy it to an aligned buffer
	// before handing it to the driver.
	if (tmp[0]==0) {
		const int dataSize = etc1_data_size(width,height);
		if (sz==dataSize+1) {
			void *data = malloc(dataSize);
			memcpy(data, tmp + 1, dataSize);
			qglCompressedTexImage2D(target,level,GL_ETC1_RGB8_OES,width,height,0,dataSize,data);
			free(data);
		} else {
			failed=1;
		}
	} else {
		const int dataSize = width*height*2;
		if (sz==dataSize+1) {
			void *data = malloc(dataSize);
			memcpy(data, tmp + 1, dataSize);
			qglTexImage2D(target,level,format,width,height,border,format,GL_UNSIGNED_SHORT_4_4_4_4,data);
			free(data);
		} else {
			failed=1;
		}
	}

	// NB: the original code did "tmp--" here and freed the wrong pointer,
	// which crashed with "idHeap::Msize: invalid memory block" as soon as
	// any cached texture was loaded.
	fileSystem->FreeFile(tmp);
	return failed;
}

/*
================
EtxCompressLevel

Converts one mip level to ETC1 (opaque) or RGBA4444, adds it to the .etx
cache and, if glLevel >= 0, uploads it as that GL mip level of the bound
texture. Levels above the size currently used are only cached.
================
*/
static void EtxCompressLevel( const byte *rgba, int width, int height, bool opaque, int glLevel ) {
	if ( opaque ) {
		const int size = etc1_data_size( width, height );
		byte *etc1data = (byte *)malloc( size );
#ifdef USE_RG_ETC1
		rg_etc1::etc1_encode_image( rgba, width, height, 4, width * 4, etc1data );
#else
		etc1_encode_image( rgba, width, height, 4, width * 4, etc1data );
#endif
		if ( glLevel >= 0 ) {
			qglCompressedTexImage2D( GL_TEXTURE_2D, glLevel, GL_ETC1_RGB8_OES, width, height, 0, size, etc1data );
		}
		EtxAppendLevel( ETX_ETC1, etc1data, size );
		free( etc1data );
	} else {
		const int count = width * height;
		unsigned short *rgba4444data = (unsigned short *)malloc( count * 2 );
		for ( int i = 0; i < count; i++ ) {
			rgba4444data[i] = ( rgba[4*i] >> 4 ) << 12 | ( rgba[4*i+1] >> 4 ) << 8
							| ( rgba[4*i+2] >> 4 ) << 4 | ( rgba[4*i+3] >> 4 );
		}
		if ( glLevel >= 0 ) {
			// rows of 1-pixel-wide levels are only 2 bytes long
			qglPixelStorei( GL_UNPACK_ALIGNMENT, 2 );
			qglTexImage2D( GL_TEXTURE_2D, glLevel, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, rgba4444data );
			qglPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
		}
		EtxAppendLevel( ETX_RGBA4444, rgba4444data, count * 2 );
		free( rgba4444data );
	}
}

void myglTexImage2D(char* cachefname,GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
	static int opaque = 0;

	// LOGI("myglTexImage2D, name = %s", cachefname );

	if (r_useETC1.GetBool() && format == GL_RGBA && type == GL_UNSIGNED_BYTE) {

		if (level == 0)
			opaque = isopaque(width, height, pixels);

		// The old per-level cache files (.etc, .e01, .e02, ...) are no longer
		// used: they could only be checked after the source image had already
		// been loaded and processed. With r_useETC1cache, GenerateImage() uses
		// GenerateETCCacheLevels() and the single-file .etx cache instead, which
		// is read before the source image is touched (idImage::LoadETCCache).
		if (opaque)
			etc1_compress_tex_image(0,target, level, format, width, height, border, format, type, pixels);
		else
			rgba4444_convert_tex_image(0,target, level, format, width, height, border, format, type, pixels);
	} else {
		qglTexImage2D(target,level,internalformat,width,height,border,format,type,pixels);
	}
}

//end

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

	// collect the uploaded levels for the .etx cache
	EtxBegin( etxWantCache && r_useETC1.GetBool() && r_useETC1Cache.GetBool() );

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
	GetDownsize( scaled_width, scaled_height );

	scaledBuffer = NULL;

	// generate the texture number
	qglGenTextures( 1, &texnum );

	// select proper internal format before we resample
	internalFormat = GL_RGBA;

	// building the .etx cache: compress the whole mip chain (possibly from a
	// bigger size than the one used now) and upload the levels we need
	if ( etxBuilding ) {
		GenerateETCCacheLevels( pic, width, height, scaled_width, scaled_height, preserveBorder );
		GL_CheckErrors();
		return;
	}

	// copy or resample data as appropriate for first MIP level
	if ( ( scaled_width == width ) && ( scaled_height == height ) ) {
		// we must copy even if unchanged, because the border zeroing
		// would otherwise modify const data
		scaledBuffer = (byte *)R_StaticAlloc( sizeof( unsigned ) * scaled_width * scaled_height );
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

	if ( generatorFunction == NULL && ( (depth == TD_BUMP && globalImages->image_writeNormalTGA.GetBool()) || (depth != TD_BUMP && globalImages->image_writeTGA.GetBool()) ) ) {
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
	if ( depth == TD_BUMP ) {
		for ( int i = 0; i < scaled_width * scaled_height * 4; i += 4 ) {
			scaledBuffer[ i + 3 ] = scaledBuffer[ i ];
			scaledBuffer[ i ] = 0;
		}
	}
	// upload the main image level
	Bind();
	char filename[MAX_IMAGE_NAME];
	char*fptr=&filename[0];
	ImageProgramStringToCompressedFileName(imgName, filename);
	char *ext = strrchr(filename, '.');
	if (ext) {
		strcpy(ext, ".etc");
	} else
		fptr=0;

	myglTexImage2D(fptr,GL_TEXTURE_2D, 0, internalFormat, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaledBuffer);

	// create and upload the mip map levels, which we do in all cases, even if we don't think they are needed
	int		miplevel;

	miplevel = 0;
	while ( scaled_width > 1 || scaled_height > 1 ) {
		// preserve the border after mip map unless repeating
		shrunk = R_MipMap( scaledBuffer, scaled_width, scaled_height, preserveBorder );
		R_StaticFree( scaledBuffer );
		scaledBuffer = shrunk;

		scaled_width >>= 1;
		scaled_height >>= 1;
		if ( scaled_width < 1 ) {
			scaled_width = 1;
		}
		if ( scaled_height < 1 ) {
			scaled_height = 1;
		}
		miplevel++;

		// this is a visualization tool that shades each mip map
		// level with a different color so you can see the
		// rasterizer's texture level selection algorithm
		// Changing the color doesn't help with lumminance/alpha/intensity formats...
		if ( depth == TD_DIFFUSE && globalImages->image_colorMipLevels.GetBool() ) {
			R_BlendOverTexture( (byte *)scaledBuffer, scaled_width * scaled_height, mipBlendColors[miplevel] );
		}

		// upload the mip map
		char filename[MAX_IMAGE_NAME];
		char*fptr=&filename[0];
		ImageProgramStringToCompressedFileName(imgName, filename);
		char *ext = strrchr(filename, '.');
		if (ext) {
			strcpy(ext, ".e");
			ext[2]='0'+miplevel/10;
			ext[3]='0'+miplevel%10;
		} else
			fptr=0;

		myglTexImage2D(fptr,GL_TEXTURE_2D, miplevel, internalFormat, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaledBuffer);
	}

	if ( scaledBuffer != 0 ) {
		R_StaticFree( scaledBuffer );
	}

	SetImageFilterAndRepeat();

	// see if we messed anything up
	GL_CheckErrors();
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
	int			i;

	PurgeImage();

	filter = filterParm;
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

	// generate the texture number
	qglGenTextures( 1, &texnum );

	// select proper internal format before we resample
	internalFormat = GL_RGBA;

	// don't bother with downsample for now
	scaled_width = width;
	scaled_height = height;

	uploadHeight = scaled_height;
	uploadWidth = scaled_width;

	Bind();

	// no other clamp mode makes sense
	qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// set the minimize / maximize filtering
	switch( filter ) {
	case TF_DEFAULT:
		qglTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, globalImages->textureMinFilter );
		qglTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, globalImages->textureMaxFilter );
		break;
	case TF_LINEAR:
		qglTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		break;
	case TF_NEAREST:
		qglTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		qglTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		break;
	default:
		common->FatalError( "R_CreateImage: bad texture filter" );
	}

	// upload the base level
	// FIXME: support GL_COLOR_INDEX8_EXT?
	for ( i = 0 ; i < 6 ; i++ ) {
		qglTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X+i, 0, internalFormat, scaled_width, scaled_height, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, pic[i] );
	}


	// create and upload the mip map levels
	int		miplevel;
	byte	*shrunk[6];

	for ( i = 0 ; i < 6 ; i++ ) {
		shrunk[i] = R_MipMap( pic[i], scaled_width, scaled_height, false );
	}

	miplevel = 1;
	while ( scaled_width > 1 ) {
		for ( i = 0 ; i < 6 ; i++ ) {
			byte	*shrunken;

			qglTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X+i, miplevel, internalFormat,
				scaled_width / 2, scaled_height / 2, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, shrunk[i] );

			if ( scaled_width > 2 ) {
				shrunken = R_MipMap( shrunk[i], scaled_width/2, scaled_height/2, false );
			} else {
				shrunken = NULL;
			}

			R_StaticFree( shrunk[i] );
			shrunk[i] = shrunken;
		}

		scaled_width >>= 1;
		scaled_height >>= 1;
		miplevel++;
	}

	// see if we messed anything up
	GL_CheckErrors();
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
================
EtxBorderClass

What the repeat mode does to the cached data: TR_REPEAT and TR_CLAMP leave
the texels alone and give identical files, the zero clamps write a border.
================
*/
static int EtxBorderClass( int repeat ) {
	if ( repeat == TR_CLAMP_TO_ZERO ) {
		return 1;
	}
	if ( repeat == TR_CLAMP_TO_ZERO_ALPHA ) {
		return 2;
	}
	return 0;
}

/*
================
EtxVariantMatches

Whether a file with this header holds the data that loading the image with
this repeat mode and requested depth would produce. Of the depth, only
"is it a bump map" changes the data (red/alpha swap); the rest only picks
the downsize limit, which is applied when loading. resultDepth gets the
depth the image ends up with (image programs like heightmap turn any
request into a bump map).
================
*/
static bool EtxVariantMatches( const etxHeader_t &h, int repeat, int depthIn, int &resultDepth ) {
	if ( EtxBorderClass( h.repeat ) != EtxBorderClass( repeat ) ) {
		return false;
	}
	bool resultBump;
	if ( depthIn == TD_BUMP ) {
		resultBump = true;
	} else if ( h.depthIn == depthIn || h.depthIn != TD_BUMP ) {
		// same request, or the file was made from another non-bump request:
		// the image program gives the same answer for this one
		resultBump = ( h.depthOut == TD_BUMP );
	} else {
		// made from a bump request, can't tell what this request gives
		return false;
	}
	if ( resultBump != ( h.depthOut == TD_BUMP ) ) {
		return false;
	}
	resultDepth = resultBump ? TD_BUMP : depthIn;
	return true;
}

/*
================
EtxReadHeader
================
*/
static bool EtxReadHeader( const char *filename, etxHeader_t &header ) {
	idFile *f = fileSystem->OpenFileRead( filename );
	if ( f == NULL ) {
		return false;
	}
	const bool ok = f->Read( &header, sizeof( header ) ) == (int)sizeof( header )
		&& header.magic == ETX_MAGIC && header.version == ETX_VERSION;
	fileSystem->CloseFile( f );
	return ok;
}

/*
================
ETCCacheFileName

dds/<image program as path>.etx

Variants: the same image name can be used with settings that give different
data (see EtxVariantMatches). The plain name belongs to whichever variant
was cached first; any other variant uses
dds/<image program as path>~<border><depth>.etx
Pass variantDepth = -1 for the plain name, or the depth requested for the
image to get its variant name.
================
*/
bool idImage::ETCCacheFileName( char *fileName, int variantDepth ) const {
	ImageProgramStringToCompressedFileName( imgName, fileName );
	char *ext = strrchr( fileName, '.' );
	if ( ext == NULL ) {
		return false;
	}
	if ( variantDepth < 0 ) {
		strcpy( ext, ".etx" );	// same length as ".dds"
	} else {
		static const char borderNames[3] = { 'n', 'z', 'a' };
		sprintf( ext, "~%c%d.etx", borderNames[EtxBorderClass( repeat )], variantDepth );
	}
	return true;
}

/*
================
GenerateETCCacheLevels

GenerateImage() for r_useETC1cache: builds the complete mip chain from the
full source size down to 1x1, compresses every level into the .etx cache and
uploads the levels from scaledWidth x scaledHeight down. Having every level
cached means the image_downSize* cvars can be changed later without a rebuild.
================
*/
void idImage::GenerateETCCacheLevels( const byte *pic, int width, int height,
									  int scaledWidth, int scaledHeight, bool preserveBorder ) {
	// the cache starts at the source size, only limited by what the GPU can
	// take; every size here is on the same mip chain (both sides halved,
	// clamped to 1), like GetDownsize() does
	int topWidth = width;
	int topHeight = height;
	while ( topWidth > glConfig.maxTextureSize || topHeight > glConfig.maxTextureSize ) {
		topWidth = Max( topWidth >> 1, 1 );
		topHeight = Max( topHeight >> 1, 1 );
	}

	// number of levels between the cache top and the first uploaded level
	int uploadStart = 0;
	{
		int w = topWidth;
		int h = topHeight;
		while ( ( w > scaledWidth || h > scaledHeight ) && ( w > 1 || h > 1 ) ) {
			w = Max( w >> 1, 1 );
			h = Max( h >> 1, 1 );
			uploadStart++;
		}
		if ( w != scaledWidth || h != scaledHeight ) {
			// shouldn't happen; upload from the cache top and don't cache
			uploadStart = 0;
			scaledWidth = topWidth;
			scaledHeight = topHeight;
			etxBuilding = false;
		}
	}

	// shrink the source to the cache top level
	byte *buffer = (byte *)R_StaticAlloc( width * height * 4 );
	memcpy( buffer, pic, width * height * 4 );
	int w = width;
	int h = height;
	while ( w > topWidth || h > topHeight ) {
		byte *shrunk = R_MipMap( buffer, w, h, preserveBorder );
		R_StaticFree( buffer );
		buffer = shrunk;
		w = Max( w >> 1, 1 );
		h = Max( h >> 1, 1 );
	}

	byte *swapped = NULL;
	if ( depth == TD_BUMP ) {
		swapped = (byte *)R_StaticAlloc( w * h * 4 );
	}

	Bind();

	bool opaque = false;
	for ( int level = 0; ; level++ ) {
		// zero the border on every level, any of them may be the top level
		// that gets uploaded later
		if ( repeat == TR_CLAMP_TO_ZERO ) {
			byte rgba[4] = { 0, 0, 0, 255 };
			R_SetBorderTexels( buffer, w, h, rgba );
		} else if ( repeat == TR_CLAMP_TO_ZERO_ALPHA ) {
			byte rgba[4] = { 255, 255, 255, 0 };
			R_SetBorderTexels( buffer, w, h, rgba );
		}

		// swap the red and alpha for rxgb normal maps, see GenerateImage()
		const byte *levelData = buffer;
		if ( swapped ) {
			for ( int i = 0; i < w * h * 4; i += 4 ) {
				swapped[i + 0] = 0;
				swapped[i + 1] = buffer[i + 1];
				swapped[i + 2] = buffer[i + 2];
				swapped[i + 3] = buffer[i + 0];
			}
			levelData = swapped;
		}

		// one format for the whole chain, decided on the top level
		if ( level == 0 ) {
			opaque = isopaque( w, h, levelData ) != 0;
		}

		EtxCompressLevel( levelData, w, h, opaque, level >= uploadStart ? level - uploadStart : -1 );

		if ( w == 1 && h == 1 ) {
			break;
		}
		byte *shrunk = R_MipMap( buffer, w, h, preserveBorder );
		R_StaticFree( buffer );
		buffer = shrunk;
		w = Max( w >> 1, 1 );
		h = Max( h >> 1, 1 );
	}

	R_StaticFree( buffer );
	if ( swapped ) {
		R_StaticFree( swapped );
	}

	uploadWidth = scaledWidth;
	uploadHeight = scaledHeight;
	type = TT_2D;

	SetImageFilterAndRepeat();

	if ( etxBuilding ) {
		etxTopWidth = topWidth;
		etxTopHeight = topHeight;
		etxSourceWidth = width;
		etxSourceHeight = height;
		WriteETCCache();
	}
	etxBuilding = false;
	EtxFreeBuffer();
}

/*
================
WriteETCCache

Called at the end of GenerateETCCacheLevels() with the levels collected by
EtxAppendLevel()
================
*/
void idImage::WriteETCCache() const {
	char filename[MAX_IMAGE_NAME * 2];

	if ( etxLevels != NumLevelsForImageSize( etxTopWidth, etxTopHeight ) ) {
		return;		// incomplete mip chain, don't cache
	}
	if ( !ETCCacheFileName( filename, -1 ) ) {
		return;
	}
	// if another variant of this image already has the plain name, keep it
	// and use the variant name, so the two don't overwrite each other
	etxHeader_t existing;
	int resultDepth;
	if ( EtxReadHeader( filename, existing ) && !EtxVariantMatches( existing, repeat, etxDepthIn, resultDepth ) ) {
		if ( !ETCCacheFileName( filename, etxDepthIn ) ) {
			return;
		}
	}

	etxHeader_t header;
	memset( &header, 0, sizeof( header ) );
	header.magic = ETX_MAGIC;
	header.version = ETX_VERSION;
	header.format = etxFormat;
	header.width = etxTopWidth;
	header.height = etxTopHeight;
	header.numLevels = etxLevels;
	header.sourceWidth = etxSourceWidth;
	header.sourceHeight = etxSourceHeight;
	header.depthIn = etxDepthIn;
	header.depthOut = depth;
	header.repeat = repeat;
	header.roundDown = globalImages->image_roundDown.GetInteger();
	header.timestamp = (long long)timestamp;

	const int total = sizeof( header ) + etxDataSize;
	byte *buffer = (byte *)malloc( total );
	if ( buffer == NULL ) {
		return;
	}
	memcpy( buffer, &header, sizeof( header ) );
	memcpy( buffer + sizeof( header ), etxData, etxDataSize );
	fileSystem->WriteFile( filename, buffer, total );
	free( buffer );
}

/*
================
EtxLevelSize

Bytes of one level in an .etx file (without its size field and padding)
================
*/
static int EtxLevelSize( int format, int width, int height ) {
	return ( format == ETX_ETC1 ) ? (int)etc1_data_size( width, height ) : width * height * 2;
}

/*
================
LoadETCCache

Uploads the image straight from its .etx file, without reading the source
image. The current image_downSize* settings pick the first level to upload,
and only that level and the smaller ones are read from the file (they are
at its end). Returns false (and leaves the image untouched) if there is no
usable cache file, so the caller builds the image from the source as usual
(which rewrites the file).

The plain file name is tried first, then this variant's name (see
ETCCacheFileName). With "developer 1" the reason a file is rejected is
printed to the console.
================
*/
// the parts of the last successful LoadETCFile, for the level load timing
static double	etxLastTimestampMs;
static double	etxLastReadMs;
static double	etxLastUploadMs;
static int		etxLastBytes;

bool idImage::LoadETCCache() {
	char filename[MAX_IMAGE_NAME * 2];

	if ( !r_useETC1.GetBool() || !r_useETC1Cache.GetBool() || !glConfig.isInitialized ) {
		return false;
	}
	const double start = LoadTiming_Now();
	if ( ( ETCCacheFileName( filename, -1 ) && LoadETCFile( filename ) )
		|| ( ETCCacheFileName( filename, depth ) && LoadETCFile( filename ) ) ) {
		// render thread only (ActuallyLoadImage), see loadTimingRenderThreadImages
		loadTimingImages_t &timing = loadTimingRenderThreadImages;
		const double parts = etxLastTimestampMs + etxLastReadMs + etxLastUploadMs;
		timing.etxHits++;
		timing.etxFindMs += LoadTiming_Now() - start - parts;
		timing.etxTimestampMs += etxLastTimestampMs;
		timing.etxReadMs += etxLastReadMs;
		timing.etxUploadMs += etxLastUploadMs;
		timing.etxBytes += etxLastBytes;
		return true;
	}
	return false;
}

/*
================
LoadETCFile
================
*/
bool idImage::LoadETCFile( const char *filename ) {
	idFile *f = fileSystem->OpenFileRead( filename );
	if ( f == NULL ) {
		return false;
	}

	const int len = f->Length();
	etxHeader_t header;
	bool valid = false;
	const char *reason = "bad header";
	ID_TIME_T sourceTime = 0;
	int startLevel = -1;
	int startOffset = 0;
	int resultDepth = depth;

	if ( len >= (int)sizeof( header ) && f->Read( &header, sizeof( header ) ) == (int)sizeof( header ) ) {
		valid = header.magic == ETX_MAGIC
			&& header.version == ETX_VERSION
			&& ( header.format == ETX_ETC1 || header.format == ETX_RGBA4444 )
			&& header.width > 0 && header.height > 0
			&& header.sourceWidth >= header.width && header.sourceHeight >= header.height
			&& header.numLevels == NumLevelsForImageSize( header.width, header.height )
			&& header.roundDown == globalImages->image_roundDown.GetInteger();
		if ( valid && !EtxVariantMatches( header, repeat, depth, resultDepth ) ) {
			valid = false;
			reason = "other variant (repeat mode / bump map)";
		}
	}

	if ( valid ) {
		// the size the current settings want, see GenerateImage()
		const textureDepth_t savedDepth = depth;
		int targetWidth = header.sourceWidth;
		int targetHeight = header.sourceHeight;
		depth = (textureDepth_t)resultDepth;
		GetDownsize( targetWidth, targetHeight );
		depth = savedDepth;

		// find that size in the cached chain, and check the file length;
		// the level sizes follow from the header, so this needs no reads
		int offset = sizeof( header );
		int w = header.width;
		int h = header.height;
		for ( int level = 0; level < header.numLevels; level++ ) {
			if ( startLevel < 0 && w == targetWidth && h == targetHeight ) {
				startLevel = level;
				startOffset = offset;
			}
			offset += 4 + ( ( EtxLevelSize( header.format, w, h ) + 3 ) & ~3 );
			w = Max( w >> 1, 1 );
			h = Max( h >> 1, 1 );
		}
		// startLevel < 0: the settings want a bigger size than the cache has
		// (only if the source or the GPU texture size limit changed)
		valid = ( startLevel >= 0 && offset == len );
		if ( !valid ) {
			reason = ( startLevel < 0 ) ? "needed size not in the file" : "wrong file length";
		}
	}

	if ( valid ) {
		// only the timestamps, the source image isn't loaded
		const double timestampStart = LoadTiming_Now();
		R_LoadImageProgram( imgName, NULL, NULL, NULL, &sourceTime, NULL );
		etxLastTimestampMs = LoadTiming_Now() - timestampStart;
		valid = ( (long long)sourceTime == header.timestamp );
		if ( !valid ) {
			reason = "source image changed (timestamp)";
		}
	}

	// read the levels we need
	byte *data = NULL;
	const int dataSize = len - startOffset;
	if ( valid ) {
		reason = NULL;
		const double readStart = LoadTiming_Now();
		data = (byte *)malloc( dataSize );
		valid = data != NULL
			&& f->Seek( startOffset, FS_SEEK_SET ) == 0
			&& f->Read( data, dataSize ) == dataSize;
		etxLastReadMs = LoadTiming_Now() - readStart;
		etxLastBytes = dataSize;
	}

	fileSystem->CloseFile( f );

	// check the size fields before touching GL
	if ( valid ) {
		int offset = 0;
		int w = Max( header.width >> startLevel, 1 );
		int h = Max( header.height >> startLevel, 1 );
		for ( int level = startLevel; level < header.numLevels; level++ ) {
			int size;
			memcpy( &size, data + offset, 4 );
			if ( size != EtxLevelSize( header.format, w, h ) ) {
				valid = false;
				break;
			}
			offset += 4 + ( ( size + 3 ) & ~3 );
			w = Max( w >> 1, 1 );
			h = Max( h >> 1, 1 );
		}
		if ( !valid ) {
			reason = "corrupt level data";
		}
	} else if ( reason == NULL ) {
		reason = "read error";
	}

	if ( !valid ) {
		common->DPrintf( "ETC cache: %s not used: %s\n", filename, reason );
		free( data );
		return false;
	}

	const double uploadStart = LoadTiming_Now();
	PurgeImage();
	qglGenTextures( 1, &texnum );
	Bind();

	// RGBA4444 rows of 1-pixel-wide levels are only 2 bytes long
	qglPixelStorei( GL_UNPACK_ALIGNMENT, 2 );

	int offset = 0;
	int w = Max( header.width >> startLevel, 1 );
	int h = Max( header.height >> startLevel, 1 );
	uploadWidth = w;
	uploadHeight = h;
	for ( int level = startLevel; level < header.numLevels; level++ ) {
		int size;
		memcpy( &size, data + offset, 4 );
		// malloc-aligned buffer, every level starts at a multiple of 4
		const void *levelData = data + offset + 4;
		const int glLevel = level - startLevel;
		if ( header.format == ETX_ETC1 ) {
			qglCompressedTexImage2D( GL_TEXTURE_2D, glLevel, GL_ETC1_RGB8_OES, w, h, 0, size, levelData );
		} else {
			qglTexImage2D( GL_TEXTURE_2D, glLevel, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, levelData );
		}
		offset += 4 + ( ( size + 3 ) & ~3 );
		w = Max( w >> 1, 1 );
		h = Max( h >> 1, 1 );
	}

	qglPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	etxLastUploadMs = LoadTiming_Now() - uploadStart;

	free( data );

	type = TT_2D;
	internalFormat = GL_RGBA;
	depth = (textureDepth_t)resultDepth;
	timestamp = sourceTime;

	SetImageFilterAndRepeat();
	GL_CheckErrors();
	return true;
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
===============
ActuallyLoadImage

Absolutely every image goes through this path
On exit, the idImage will have a valid OpenGL texture number that can be bound
===============
*/
void	idImage::ActuallyLoadImage( bool fromBind ) {
	int		width, height;
	byte	*pic;

	if(fromBind)
	{
		//LOGI("ERROR!! CAN NOT LOAD IMAGE FROM BIND");
		globalImages->AddAllocList( this );
		return;
	}


	if( cinematic )
	{
		cinData_t	cin;

		cin = cinematic->ImageForTime( cinmaticNextTime );

		if( texnum == TEXTURE_NOT_LOADED )
			qglGenTextures( 1, &texnum );

		if ( cin.image ) {
			UploadScratch( cin.image, cin.imageWidth, cin.imageHeight );
		} else {
			//globalImages->blackImage->Bind();
		}

		return;
	}

	// this is the ONLY place generatorFunction will ever be called
	if ( generatorFunction ) {
		generatorFunction( this );
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

		for ( int i = 0 ; i < 6 ; i++ ) {
			if ( pics[i] ) {
				R_StaticFree( pics[i] );
			}
		}
	} else {
		// see if we have a pre-generated image file that is
		// already image processed and compressed
		const double loadStart = LoadTiming_Now();
		if ( LoadETCCache() ) {
			return;
		}
		// for the level load timing, see loadTimingRenderThreadImages
		struct etxMissTiming_t {
			double start;
			~etxMissTiming_t() {
				loadTimingRenderThreadImages.etxMisses++;
				loadTimingRenderThreadImages.etxMissMs += LoadTiming_Now() - start;
			}
		} etxMissTiming = { loadStart };

		const int depthIn = depth;

		R_LoadImageProgram( imgName, &pic, &width, &height, &timestamp, &depth );

		if ( pic == NULL ) {
			common->Warning( "Couldn't load image: %s", imgName.c_str() );
			MakeDefault();
			return;
		}

		etxWantCache = true;
		etxDepthIn = depthIn;
		GenerateImage( pic, width, height, filter, allowDownSize, repeat, depth );
		etxWantCache = false;

		R_StaticFree( pic );
	}
}

//=========================================================================================================

/*
===============
PurgeImage
===============
*/
void idImage::PurgeImage() {
	if ( texnum != TEXTURE_NOT_LOADED ) {
		qglDeleteTextures( 1, &texnum );	// this should be the ONLY place it is ever called!
		GL_TexBindForget( texnum );
		texnum = TEXTURE_NOT_LOADED;
	}
}

/*
==============
Bind

Automatically enables 2D mapping, cube mapping, or 3D texturing if needed
==============
*/
bool idImage::Bind() {
	// load the image if necessary (FIXME: not SMP safe!)
	if ( texnum == TEXTURE_NOT_LOADED ) {

		// load the image on demand here, which isn't our normal game operating mode
		ActuallyLoadImage( true );
		// Load a black image to reduce flicker
		globalImages->blackImage->Bind();
		return false;
	}

	// bump our statistic counters
	frameUsed = backEnd.frameCount;
	bindCount++;

	// bind the texture
	if ( type == TT_2D ) {
		if ( !GL_TexBindCached( 0, texnum ) ) {
			qglBindTexture( GL_TEXTURE_2D, texnum );
		}
	} else if ( type == TT_CUBIC ) {
		if ( !GL_TexBindCached( 1, texnum ) ) {
			qglBindTexture( GL_TEXTURE_CUBE_MAP, texnum );
		}
	}
	return true;
}

/*
==============
BindFragment

Fragment programs explicitly say which type of map they want, so we don't need to
do any enable / disable changes
==============
*/
void idImage::BindFragment() {

	// load the image if necessary (FIXME: not SMP safe!)
	if ( texnum == TEXTURE_NOT_LOADED ) {
		// load the image on demand here, which isn't our normal game operating mode
		ActuallyLoadImage( true );
	}


	// bump our statistic counters
	frameUsed = backEnd.frameCount;
	bindCount++;

	// bind the texture
	if ( type == TT_2D ) {
		if ( !GL_TexBindCached( 0, texnum ) ) {
			qglBindTexture( GL_TEXTURE_2D, texnum );
		}
	} else if ( type == TT_CUBIC ) {
		if ( !GL_TexBindCached( 1, texnum ) ) {
			qglBindTexture( GL_TEXTURE_CUBE_MAP, texnum );
		}
	}
}


/*
====================
CopyFramebuffer
====================
*/
void idImage::CopyFramebuffer( int x, int y, int imageWidth, int imageHeight, bool useOversizedBuffer ) {
	Bind();

	if ( cvarSystem->GetCVarBool( "g_lowresFullscreenFX" ) ) {
		imageWidth = 512;
		imageHeight = 512;
	}

	// if the size isn't a power of 2, the image must be increased in size
	int	potWidth, potHeight;

	potWidth = MakePowerOfTwo( imageWidth );
	potHeight = MakePowerOfTwo( imageHeight );

	// Don't do this, otherwise the Grabber gun graphics from ROE do not work properly
	//GetDownsize( imageWidth, imageHeight );
	//GetDownsize( potWidth, potHeight );

	//Disabled for OES2
	//qglReadBuffer( GL_BACK );

	// only resize if the current dimensions can't hold it at all,
	// otherwise subview renderings could thrash this
	if ( ( useOversizedBuffer && ( uploadWidth < potWidth || uploadHeight < potHeight ) )
		|| ( !useOversizedBuffer && ( uploadWidth != potWidth || uploadHeight != potHeight ) ) ) {
		uploadWidth = potWidth;
		uploadHeight = potHeight;
		if ( potWidth == imageWidth && potHeight == imageHeight ) {
			qglCopyTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, x, y, imageWidth, imageHeight, 0 );
		} else {
			byte	*junk;
			// we need to create a dummy image with power of two dimensions,
			// then do a qglCopyTexSubImage2D of the data we want
			// this might be a 16+ meg allocation, which could fail on _alloca
			junk = (byte *)Mem_Alloc( potWidth * potHeight * 3 );
			memset( junk, 0, potWidth * potHeight * 3 );		//!@#
			qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, potWidth, potHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, junk );

			Mem_Free( junk );

			qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, x, y, imageWidth, imageHeight );
		}
	} else {
		// otherwise, just subimage upload it so that drivers can tell we are going to be changing
		// it and don't try and do a texture compression or some other silliness
		qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, x, y, imageWidth, imageHeight );
	}

	// if the image isn't a full power of two, duplicate an extra row and/or column to fix bilerps
	if ( imageWidth != potWidth ) {
		qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, imageWidth, 0, x+imageWidth-1, y, 1, imageHeight );
	}
	if ( imageHeight != potHeight ) {
		qglCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, imageHeight, x, y+imageHeight-1, imageWidth, 1 );
	}

	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	backEnd.c_copyFrameBuffer++;
}

/*
====================
CopyDepthbuffer

This should just be part of copyFramebuffer once we have a proper image type field
====================
*/
void idImage::CopyDepthbuffer( int x, int y, int imageWidth, int imageHeight ) {
	Bind();

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

	// if rows = cols * 6, assume it is a cube map animation
	if ( rows == cols * 6 ) {
		if ( type != TT_CUBIC ) {
			type = TT_CUBIC;
			uploadWidth = -1;	// for a non-sub upload
		}

		Bind();

		rows /= 6;
		// if the scratchImage isn't in the format we want, specify it as a new texture
		if ( cols != uploadWidth || rows != uploadHeight ) {
			uploadWidth = cols;
			uploadHeight = rows;

			// upload the base level
			for ( i = 0 ; i < 6 ; i++ ) {
				qglTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X+i, 0, GL_RGBA, cols, rows, 0,
					GL_RGBA, GL_UNSIGNED_BYTE, data + cols*rows*4*i );
			}
		} else {
			// otherwise, just subimage upload it so that drivers can tell we are going to be changing
			// it and don't try and do a texture compression
			for ( i = 0 ; i < 6 ; i++ ) {
				qglTexSubImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X+i, 0, 0, 0, cols, rows,
					GL_RGBA, GL_UNSIGNED_BYTE, data + cols*rows*4*i );
			}
		}
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		// no other clamp mode makes sense
		qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		qglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	} else {
		// otherwise, it is a 2D image
		if ( type != TT_2D ) {
			type = TT_2D;
			uploadWidth = -1;	// for a non-sub upload
		}

		Bind();

		// if the scratchImage isn't in the format we want, specify it as a new texture
		if ( cols != uploadWidth || rows != uploadHeight ) {
			uploadWidth = cols;
			uploadHeight = rows;
			qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
		} else {
			// otherwise, just subimage upload it so that drivers can tell we are going to be changing
			// it and don't try and do a texture compression
			qglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data );
		}
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		// these probably should be clamp, but we have a lot of issues with editor
		// geometry coming out with texcoords slightly off one side, resulting in
		// a smear across the entire polygon
#if 1
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
#else
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
#endif
	}
}


void idImage::SetClassification( int tag ) {
	classification = tag;
}


bool idImage::isLoaded()
{
	return (!purgePending && (texnum != TEXTURE_NOT_LOADED));
}

/*
==================
StorageSize
==================
*/
int idImage::StorageSize() const {
	int		baseSize;

	if ( texnum == TEXTURE_NOT_LOADED ) {
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
	if ( generatorFunction ) {
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
	case TT_RECT:
		common->Printf( "R" );
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
	case 1:
	case 2:
	case 3:
	case 4:
		common->Printf( "RGBA  " );
		break;
	case GL_RGBA4:
		common->Printf( "RGBA4 " );
		break;
	case GL_RGB5_A1:
		common->Printf( "RGB5_A1  " );
		break;
	case 0:
		common->Printf( "      " );
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
