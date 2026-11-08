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

#ifndef __LOADTIMING_H__
#define __LOADTIMING_H__

/*
===============================================================================

	Level load timing: where the time of a map load goes.

	idSessionLocal::ExecuteMapChange resets the counters, the timed parts of the
	engine add to them, and the totals are printed after "msec to load".

	Each bucket is exclusive: time spent in a scope opened inside another one
	(a model parsing its materials, a sound shader loading its samples, a
	collision model parsing its render model) is taken out of the outer scope,
	so the buckets add up to the time really spent in each kind of work.

	Scopes only count on the main thread. Images are loaded by the render
	thread, which hands its time over with LoadTiming_AddImages() on the
	main thread, after the main thread has waited for the images. During a
	load that wait happens in the loading screen redraws, so the image time is
	also part of LT_LOADING_SCREEN.

	Engine only, never used from the game libraries.

===============================================================================
*/

typedef enum {
	LT_MODEL_STATIC,		// ASE / LWO / FLT / MA: parse + FinishSurfaces
	LT_MODEL_MD5,			// md5mesh
	LT_MODEL_OTHER,			// particles, liquids, md3, ...
	LT_CM_CONVERT,			// collision models built from render models
	LT_AAS,					// .aas files
	LT_DECL_MATERIAL,		// first parse of a decl: materials,
	LT_DECL_MODELDEF,		// model defs (with the md5anims they load, without the md5mesh),
	LT_DECL_ENTITYDEF,		// entity defs,
	LT_DECL_SOUND,			// sound shaders (without their samples),
	LT_DECL_PARTICLE_FX,	// particles and FX,
	LT_DECL_OTHER,			// any other decl
	LT_SOUND_SAMPLE,		// sound sample read + decode + OpenAL upload
	LT_LOADING_SCREEN,		// loading screen redraws (PacifierUpdate), incl. waiting for images
	LT_NUM_BUCKETS
} loadTimingBucket_t;

double	LoadTiming_Now( void );					// milliseconds, high resolution

// resets all counters, called when a map load starts
void	LoadTiming_Reset( void );

// prints the counters; phaseMs are the ExecuteMapChange phases
void	LoadTiming_Print( double preMs, double procMs, double gameMs, double mediaMs, double settleMs );

// images loaded by the render thread in one frame task: filled by the render thread
// (idRenderSystemLocal::BackendThreadTask), handed over with LoadTiming_AddImages()
// on the main thread once that has waited for the images
typedef struct {
	int		count;
	double	ms;
	int		bytesRead;			// fileSystem read count while loading them

	// ETC cache (r_useETC1Cache, Image_load.cpp): images loaded from an .etx file
	int		etxHits;
	double	etxFindMs;			// finding/opening the file (both names) and checking the header
	double	etxTimestampMs;		// looking up the source images' timestamps
	double	etxReadMs;			// reading the mip levels
	double	etxUploadMs;		// glCompressedTexImage2D / glTexImage2D
	int		etxBytes;			// mip level data read
	// images with no usable .etx file, built from the source image (and cached)
	int		etxMisses;
	double	etxMissMs;
} loadTimingImages_t;

void	LoadTiming_AddImages( const loadTimingImages_t &images );

// written only by the render thread in its image loop, read by the main thread after
// it has waited for the images (idRenderSystemLocal::RenderCommands)
extern loadTimingImages_t	loadTimingRenderThreadImages;

// picks the model bucket from a model file name
loadTimingBucket_t LoadTiming_ModelBucket( const char *modelName );

class idLoadTimingScope {
public:
	explicit			idLoadTimingScope( loadTimingBucket_t bucket );
						~idLoadTimingScope( void );

private:
	idLoadTimingScope *	parent;
	loadTimingBucket_t	bucket;
	double				start;
	double				childMs;		// time of the scopes opened inside this one
	bool				active;			// false when opened on another thread

						idLoadTimingScope( const idLoadTimingScope & );
	void				operator=( const idLoadTimingScope & );
};

#endif /* !__LOADTIMING_H__ */
