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

#include <SDL.h>

#include "sys/platform.h"
#include "framework/Common.h"

#include "framework/LoadTiming.h"

static double				ltBucketMs[LT_NUM_BUCKETS];
static int					ltBucketCount[LT_NUM_BUCKETS];
static idLoadTimingScope *	ltCurrentScope = NULL;

loadTimingImages_t			loadTimingRenderThreadImages;

static loadTimingImages_t	ltImages;				// all images since the reset
static loadTimingImages_t	ltImagesAtPrint;		// the part of them loaded before the summary
static bool					ltImageReportPending = false;

/*
================
LoadTiming_Now
================
*/
double LoadTiming_Now( void ) {
	static double msPerTick = 0.0;
	if ( msPerTick == 0.0 ) {
		msPerTick = 1000.0 / (double)SDL_GetPerformanceFrequency();
	}
	return (double)SDL_GetPerformanceCounter() * msPerTick;
}

/*
================
LoadTiming_Reset
================
*/
void LoadTiming_Reset( void ) {
	for ( int i = 0; i < LT_NUM_BUCKETS; i++ ) {
		ltBucketMs[i] = 0.0;
		ltBucketCount[i] = 0;
	}
	memset( &ltImages, 0, sizeof( ltImages ) );
	memset( &ltImagesAtPrint, 0, sizeof( ltImagesAtPrint ) );
	ltImageReportPending = false;
}

/*
================
LoadTiming_ModelBucket
================
*/
loadTimingBucket_t LoadTiming_ModelBucket( const char *modelName ) {
	idStr extension;
	idStr( modelName ).ExtractFileExtension( extension );
	if ( extension.Icmp( "ase" ) == 0 || extension.Icmp( "lwo" ) == 0
		|| extension.Icmp( "flt" ) == 0 || extension.Icmp( "ma" ) == 0 ) {
		return LT_MODEL_STATIC;
	}
	if ( extension.Icmp( "md5mesh" ) == 0 ) {
		return LT_MODEL_MD5;
	}
	return LT_MODEL_OTHER;
}

/*
================
LoadTiming_PrintImages
================
*/
static void LoadTiming_PrintImages( const char *label, const loadTimingImages_t &images ) {
	const double mb = images.bytesRead / ( 1024.0 * 1024.0 );
	common->Printf( "%s: %d in %.0f msec, %.1f MB read (%.1f MB/s)\n", label, images.count, images.ms, mb,
		images.ms > 0.0 ? mb * 1000.0 / images.ms : 0.0 );
	if ( images.etxHits > 0 || images.etxMisses > 0 ) {
		const double etxMb = images.etxBytes / ( 1024.0 * 1024.0 );
		common->Printf( "  etx cache: %d hits: find=%.0f timestamps=%.0f read=%.0f (%.1f MB) upload=%.0f msec; "
			"%d built from source in %.0f msec\n",
			images.etxHits, images.etxFindMs, images.etxTimestampMs, images.etxReadMs, etxMb, images.etxUploadMs,
			images.etxMisses, images.etxMissMs );
	}
}

/*
================
LoadTiming_AddImageStats

dst += sign * src
================
*/
static void LoadTiming_AddImageStats( loadTimingImages_t &dst, const loadTimingImages_t &src, int sign ) {
	dst.count += sign * src.count;
	dst.ms += sign * src.ms;
	dst.bytesRead += sign * src.bytesRead;
	dst.etxHits += sign * src.etxHits;
	dst.etxFindMs += sign * src.etxFindMs;
	dst.etxTimestampMs += sign * src.etxTimestampMs;
	dst.etxReadMs += sign * src.etxReadMs;
	dst.etxUploadMs += sign * src.etxUploadMs;
	dst.etxBytes += sign * src.etxBytes;
	dst.etxMisses += sign * src.etxMisses;
	dst.etxMissMs += sign * src.etxMissMs;
}

/*
================
LoadTiming_Print
================
*/
void LoadTiming_Print( double preMs, double procMs, double gameMs, double mediaMs, double settleMs ) {
	double bucketSum = 0.0;
	for ( int i = 0; i < LT_NUM_BUCKETS; i++ ) {
		bucketSum += ltBucketMs[i];
	}
	// main thread time outside the buckets: entity spawn, scripts, GUIs, the .cm/.proc files, ...
	// (images need no subtracting: the main thread waits for them inside LT_LOADING_SCREEN)
	const double total = preMs + procMs + gameMs + mediaMs + settleMs;
	double other = total - bucketSum;
	if ( other < 0.0 ) {
		other = 0.0;
	}

	common->Printf( "load phases (msec): pre=%.0f proc=%.0f game=%.0f media=%.0f settle=%.0f total=%.0f\n",
		preMs, procMs, gameMs, mediaMs, settleMs, total );
	common->Printf( "load models (msec): static=%.0f (%d) md5=%.0f (%d) other=%.0f (%d) collision=%.0f (%d) aas=%.0f (%d)\n",
		ltBucketMs[LT_MODEL_STATIC], ltBucketCount[LT_MODEL_STATIC],
		ltBucketMs[LT_MODEL_MD5], ltBucketCount[LT_MODEL_MD5],
		ltBucketMs[LT_MODEL_OTHER], ltBucketCount[LT_MODEL_OTHER],
		ltBucketMs[LT_CM_CONVERT], ltBucketCount[LT_CM_CONVERT],
		ltBucketMs[LT_AAS], ltBucketCount[LT_AAS] );
	common->Printf( "load decls (msec): material=%.0f (%d) modelDef+anims=%.0f (%d) entityDef=%.0f (%d) sound=%.0f (%d) particle+fx=%.0f (%d) other=%.0f (%d)\n",
		ltBucketMs[LT_DECL_MATERIAL], ltBucketCount[LT_DECL_MATERIAL],
		ltBucketMs[LT_DECL_MODELDEF], ltBucketCount[LT_DECL_MODELDEF],
		ltBucketMs[LT_DECL_ENTITYDEF], ltBucketCount[LT_DECL_ENTITYDEF],
		ltBucketMs[LT_DECL_SOUND], ltBucketCount[LT_DECL_SOUND],
		ltBucketMs[LT_DECL_PARTICLE_FX], ltBucketCount[LT_DECL_PARTICLE_FX],
		ltBucketMs[LT_DECL_OTHER], ltBucketCount[LT_DECL_OTHER] );
	common->Printf( "load work (msec): sound samples=%.0f (%d) loading screen=%.0f (%d frames) other=%.0f\n",
		ltBucketMs[LT_SOUND_SAMPLE], ltBucketCount[LT_SOUND_SAMPLE],
		ltBucketMs[LT_LOADING_SCREEN], ltBucketCount[LT_LOADING_SCREEN],
		other );
	LoadTiming_PrintImages( "load images so far (render thread)", ltImages );

	// images still queued are loaded by the render thread in the next frames
	ltImagesAtPrint = ltImages;
	ltImageReportPending = true;
}

/*
================
LoadTiming_AddImages

Called by the main thread every frame. After a map load, the first frame that
loads no images means the image queue is empty: report what was loaded after
the load summary was printed.
================
*/
void LoadTiming_AddImages( const loadTimingImages_t &images ) {
	if ( images.count > 0 ) {
		LoadTiming_AddImageStats( ltImages, images, 1 );
		return;
	}
	if ( ltImageReportPending ) {
		ltImageReportPending = false;
		loadTimingImages_t after = ltImages;
		LoadTiming_AddImageStats( after, ltImagesAtPrint, -1 );
		LoadTiming_PrintImages( "load images after the map load", after );
		LoadTiming_PrintImages( "load images in all", ltImages );
	}
}

/*
================
idLoadTimingScope::idLoadTimingScope
================
*/
idLoadTimingScope::idLoadTimingScope( loadTimingBucket_t bucket ) {
	this->bucket = bucket;
	parent = NULL;
	childMs = 0.0;
	start = 0.0;
	active = Sys_IsMainThread();
	if ( active ) {
		parent = ltCurrentScope;
		ltCurrentScope = this;
		start = LoadTiming_Now();
	}
}

/*
================
idLoadTimingScope::~idLoadTimingScope
================
*/
idLoadTimingScope::~idLoadTimingScope( void ) {
	if ( !active ) {
		return;
	}
	const double ms = LoadTiming_Now() - start;
	ltBucketMs[bucket] += ms - childMs;
	ltBucketCount[bucket]++;
	if ( parent != NULL ) {
		parent->childMs += ms;
	}
	ltCurrentScope = parent;
}
