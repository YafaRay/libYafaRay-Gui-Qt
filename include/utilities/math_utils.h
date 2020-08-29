#pragma once

#ifndef YAFARAY_MATH_UTILS_H
#define YAFARAY_MATH_UTILS_H

#include <cmath>

//#if ( defined(__i386__) || defined(_M_IX86) || defined(_X86_) )
//#define FAST_INT 1
#define DOUBLEMAGICROUNDEPS	      (.5-1.4e-11)
//almost .5f = .5f - 1e^(number of exp bit)
#define DOUBLEMAGIC			double (6755399441055744.0)
//2^52 * 1.5,  uses limited precision to floor
//#endif


inline int round2Int__(double val)
{
#ifdef FAST_INT
	val		= val + DOUBLEMAGIC;
	return ((long *)&val)[0];
#else
	//	#warning "using slow rounding"
	return int (val + DOUBLEMAGICROUNDEPS);
#endif
}

inline int float2Int__(double val)
{
#ifdef FAST_INT
	return (val < 0) ?  round2Int__(val + DOUBLEMAGICROUNDEPS) :
		   (val - DOUBLEMAGICROUNDEPS);
#else
	//	#warning "using slow rounding"
	return (int)val;
#endif
}

inline int floor2Int__(double val)
{
#ifdef FAST_INT
	return round2Int__(val - DOUBLEMAGICROUNDEPS);
#else
	//	#warning "using slow rounding"
	return (int)std::floor(val);
#endif
}

inline int ceil2Int__(double val)
{
#ifdef FAST_INT
	return round2Int__(val + DOUBLEMAGICROUNDEPS);
#else
	//	#warning "using slow rounding"
	return (int)std::ceil(val);
#endif
}

inline double roundFloatPrecision__(double val, double precision) //To round, for example 3.2384764 to 3.24 use precision 0.01
{
	if(precision <= 0.0) return 0.0;
	else return std::round(val / precision) * precision;
}

inline bool isValidFloat__(float value)	//To check a float is not a NaN for example, even while using --fast-math compile flag
{
	return (value >= std::numeric_limits<float>::lowest() && value <= std::numeric_limits<float>::max());
}

#endif // YAFARAY_MATH_UTILS_H
