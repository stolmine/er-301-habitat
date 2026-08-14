#ifndef GlobalDefinitions_h
#define GlobalDefinitions_h

#include <float.h>

/** This file contains a bunch of useful macros which are not wrapped into the
rosic namespace to facilitate their global use. */

// habitat port: upstream degraded INLINE to a bare `inline` on GCC, which is
// only a hint. Per feedback_static_inline_not_guaranteed, hot helpers have been
// observed staying out-of-line under -O3, so route it through the port's forced
// variant (which stays a plain `inline` under SWIG's parser).
#include "o3_config.h"

#ifdef _MSC_VER
#define INLINE __forceinline
#else
#define INLINE O3_ALWAYS_INLINE
#endif

//_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON)

//-------------------------------------------------------------------------------------------------
// mathematical constants:

#define PI 3.1415926535897932384626433832795
#define EULER 2.7182818284590452353602874713527
#define SQRT2 1.4142135623730950488016887242097
#define ONE_OVER_SQRT2 0.70710678118654752440084436210485
#define LN10 2.3025850929940456840179914546844
#define ONE_OVER_LN10 0.43429448190325182765112891891661
#define LN2 0.69314718055994530941723212145818
#define ONE_OVER_LN2 1.4426950408889634073599246810019
#define SEMITONE_FACTOR 1.0594630943592952645618252949463

//-------------------------------------------------------------------------------------------------
// type definitions:

// unsigned 64 bit integers:
#ifdef _MSC_VER
typedef unsigned __int64 UINT64;
#else
typedef unsigned long long UINT64;
#endif

// signed 64 bit integers:
#ifdef _MSC_VER
typedef signed __int64 INT64;
#else
typedef signed long long INT64;
#endif

// unsigned 32 bit integers:
#ifdef _MSC_VER
typedef unsigned __int32 UINT32;
#else
typedef unsigned long UINT32;
#endif

// ...constants for numerical precision issues, denorm, etc.:
#define TINY FLT_MIN
#define EPS DBL_EPSILON

// define infinity values:

inline double dummyFunction(double x) { return x; }
#define INF (1.0/dummyFunction(0.0))
#define NEG_INF (-1.0/dummyFunction(0.0))

//-------------------------------------------------------------------------------------------------
// debug stuff:

// this will try to break the debugger if one is currently hosting this app:
#ifdef _DEBUG

#ifdef _MSC_VER
#pragma intrinsic (__debugbreak)
#define DEBUG_BREAK __debugbreak();
#else
#define DEBUG_BREAK {}
#endif

#else

#define DEBUG_BREAK {}  // evaluate to no op in release builds

#endif

// an replacement of the ASSERT macro
#define rassert(expression)  { if (! (expression)) DEBUG_BREAK }

//-------------------------------------------------------------------------------------------------
// bit twiddling:

//extract the exponent from a IEEE 754 floating point number (single and double precision):
//
// habitat port: upstream type-punned through reinterpret_cast, which is
// undefined behavior that GCC at -O3 is entitled to exploit - anamnesis was
// burned by exactly this class of UB. Both macros now route through the
// memcpy-based helper in o3_config.h, which compilers fold to a plain move.
// EXPOFFLT is the one on the audio path (BlendOscillator's mip level select);
// EXPOFDBL is kept for source compatibility only.
#define EXPOFFLT(value) (o3Exponent(value))
#define EXPOFDBL(value) (o3Exponent((float)(value)))

#endif
