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

static double				ltImageMs = 0.0;			// all images since the reset
static int					ltImageCount = 0;
static double				ltImageMsAtPrint = 0.0;	// part of the above loaded before the print
static int					ltImageCountAtPrint = 0;
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
	ltImageMs = 0.0;
	ltImageCount = 0;
	ltImageMsAtPrint = 0.0;
	ltImageCountAtPrint = 0;
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
	common->Printf( "load work (msec): static models=%.0f (%d) md5=%.0f (%d) other models=%.0f (%d) collision=%.0f (%d) aas=%.0f (%d)\n",
		ltBucketMs[LT_MODEL_STATIC], ltBucketCount[LT_MODEL_STATIC],
		ltBucketMs[LT_MODEL_MD5], ltBucketCount[LT_MODEL_MD5],
		ltBucketMs[LT_MODEL_OTHER], ltBucketCount[LT_MODEL_OTHER],
		ltBucketMs[LT_CM_CONVERT], ltBucketCount[LT_CM_CONVERT],
		ltBucketMs[LT_AAS], ltBucketCount[LT_AAS] );
	common->Printf( "load work (msec): materials=%.0f (%d) other decls=%.0f (%d) sounds=%.0f (%d) other=%.0f\n",
		ltBucketMs[LT_DECL_MATERIAL], ltBucketCount[LT_DECL_MATERIAL],
		ltBucketMs[LT_DECL_OTHER], ltBucketCount[LT_DECL_OTHER],
		ltBucketMs[LT_SOUND_SAMPLE], ltBucketCount[LT_SOUND_SAMPLE],
		other );
	common->Printf( "load work (msec): loading screen=%.0f (%d frames), images on the render thread so far=%.0f (%d)\n",
		ltBucketMs[LT_LOADING_SCREEN], ltBucketCount[LT_LOADING_SCREEN],
		ltImageMs, ltImageCount );

	// images still queued are loaded by the render thread in the next frames
	ltImageMsAtPrint = ltImageMs;
	ltImageCountAtPrint = ltImageCount;
	ltImageReportPending = true;
}

/*
================
LoadTiming_AddImageTime

Called by the main thread every frame. After a map load, the first frame that
loads no images means the image queue is empty: report what was loaded after
the load summary was printed.
================
*/
void LoadTiming_AddImageTime( double ms, int count ) {
	if ( count > 0 ) {
		ltImageMs += ms;
		ltImageCount += count;
		return;
	}
	if ( ltImageReportPending ) {
		ltImageReportPending = false;
		common->Printf( "load images (msec): %.0f (%d) after the map load, %.0f (%d) in all\n",
			ltImageMs - ltImageMsAtPrint, ltImageCount - ltImageCountAtPrint, ltImageMs, ltImageCount );
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
