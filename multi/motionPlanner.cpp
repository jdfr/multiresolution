
#include "motionPlanner.hpp"

#include <stdexcept>
#include <cmath>
#include <stdio.h>
//#define TEST_INTRINSIC
#ifdef TEST_INTRINSIC
#include <exception>

class Int128
{
public:
    ulong64 lo;
    long64 hi;

    Int128(long64 _lo = 0)
    {
        lo = (ulong64)_lo;
        if (_lo < 0)  hi = -1; else hi = 0;
    }
    Int128(const Int128 &val) : lo(val.lo), hi(val.hi){}

    Int128(const long64& _hi, const ulong64& _lo) : lo(_lo), hi(_hi){}

    bool operator == (const Int128 &val) const
    {
        return (hi == val.hi && lo == val.lo);
    }
    bool operator < (const Int128 &val) const
    {
        if (hi != val.hi)
            return hi < val.hi;
        else
            return lo < val.lo;
    }

    Int128 operator + (const Int128 &rhs) const
    {
        Int128 result(*this);
        result += rhs;
        return result;
    }

};
Int128 Int128Mul(long64 lhs, long64 rhs)
{
    bool negate = (lhs < 0) != (rhs < 0);

    if (lhs < 0) lhs = -lhs;
    ulong64 int1Hi = ulong64(lhs) >> 32;
    ulong64 int1Lo = ulong64(lhs & 0xFFFFFFFF);

    if (rhs < 0) rhs = -rhs;
    ulong64 int2Hi = ulong64(rhs) >> 32;
    ulong64 int2Lo = ulong64(rhs & 0xFFFFFFFF);

    //nb: see comments in clipper.pas
    ulong64 a = int1Hi * int2Hi;
    ulong64 b = int1Lo * int2Lo;
    ulong64 c = int1Hi * int2Lo + int1Lo * int2Hi;

    Int128 tmp;
    tmp.hi = long64(a + (c >> 32));
    tmp.lo = long64(c << 32);
    tmp.lo += long64(b);
    if (tmp.lo < b) tmp.hi++;
    if (negate) tmp = -tmp;
    return tmp;
};
#endif

bool anyTrue(std::vector<bool> &valid) {
    for (std::vector<bool>::const_iterator v = valid.begin(); v != valid.end(); ++v) {
        if (*v) return true;
    }
    return false;
}

#define USE_INTRINSIC_128

#ifdef USE_INTRINSIC_128

#   if defined(__GNUC__) && defined(__SIZEOF_INT128__)

#    include <limits.h>

#   elif defined(_WIN64)

#       include <intrin.h>
#       pragma intrinsic(_mul128)
typedef union UI128 {
    __int64 i;
    unsigned __int64 u;
} UI128;

#   else

//in this case, we cannot use intrinsics
#       undef USE_INTRINSIC_128

#   endif

#endif

//quite naive, but almost as simple as it can get
template <PathCloseMode mode> void verysimple_get_nearest_path(clp::IntPoint start_point, clp::Paths &input, std::vector<bool> &valid, int &idx_result, bool &isfront_result) {
    int idx=-1;
    bool isfront;
#ifdef USE_INTRINSIC_128
#   if defined(__GNUC__) && defined(__SIZEOF_INT128__)
    __int128 dx, dy, sqdist, minsqdist = (((__int128)(LLONG_MAX))*(LLONG_MAX-1));
#   else //defined(_WIN64) is guaranteed
    clp::cInt dx, dy;
    UI128 dxlo, dxhi, dylo, dyhi, sqdistlo, sqdisthi, minsqdistlo, minsqdisthi;
    minsqdistlo.u = ULLONG_MAX;
    minsqdisthi.i = LLONG_MAX;
#   endif
#else
    double dx, dy, sqdist, minsqdist = INFINITY;
#endif

    clp::Paths::const_iterator beginpath = input.begin();
    clp::Paths::const_iterator endpath = input.end();
    std::vector<bool>::const_iterator v = valid.begin();
    for (clp::Paths::const_iterator path = beginpath; path != endpath; ++path) {
        if (*v) {
#ifdef USE_INTRINSIC_128
#   if defined(__GNUC__) && defined(__SIZEOF_INT128__)
            dx = (__int128)(start_point.X - path->front().X);
            dy = (__int128)(start_point.Y - path->front().Y);
            sqdist = dx*dx + dy*dy;
            if (sqdist < minsqdist) {
                idx = (int)(path - beginpath);
                isfront = true;
                minsqdist = sqdist;
                if (minsqdist == 0) break;
            }
            if (mode == PathOpen) {
                if ((path->front().X != path->back().X) || (path->front().Y != path->back().Y)) {
                    dx = (__int128)(start_point.X - path->back().X);
                    dy = (__int128)(start_point.Y - path->back().Y);
                    sqdist = dx*dx + dy*dy;
                    if (sqdist < minsqdist) {
                        idx = (int)(path - beginpath);
                        isfront = false;
                        minsqdist = sqdist;
                        if (minsqdist == 0) break;
                    }
                }
            }
#   else //defined(_WIN64) is guaranteed because we HAVE to compile in 64 bits
            //hopefully, this would be faster and better than converting to and operating on doubles.
            //However, it turns out that the speed penalty is minimal, at least for current workloads
            dx = (start_point.X - path->front().X);
            dxlo.i = _mul128(dx, dx, &dxhi.i);
            dy = (start_point.Y - path->front().Y);
            dylo.i = _mul128(dy, dy, &dyhi.i);

            sqdistlo.u = dxlo.u+dylo.u;
            sqdisthi.i = dxhi.i+dyhi.i+(sqdistlo.u<dxlo.u);

#       ifdef TEST_INTRINSIC
            bool a = (sqdisthi.i < minsqdisthi.i) || ((sqdisthi.i == minsqdisthi.i) && (sqdistlo.u < minsqdistlo.u));
            Int128 v1 = Int128Mul(dx, dx) + Int128Mul(dy, dy);
            Int128 v2(sqdisthi.i, sqdistlo.u);
            Int128 v3(minsqdisthi.i, minsqdisthi.u);

            bool x1 = v2 < v3;
            bool x2 = v1 == v2;
            if (!(x1&&x2)) {
                fprintf(stderr, "Problem with _mul128!!!! v1 == v2 is %s, v2 < v3 is %s\n", x2 ? "true" : "false", x1 ? "true" : "false");
                throw std::exception();
            }
#       endif
            if ((sqdisthi.i < minsqdisthi.i) || ((sqdisthi.i == minsqdisthi.i) && (sqdistlo.u < minsqdistlo.u))) {
                idx = (int)(path - beginpath);
                isfront = true;
                minsqdistlo = sqdistlo;
                minsqdisthi = sqdisthi;
                if ((minsqdistlo.u==0)&&(minsqdisthi.i==0)) break;
            }
            if (mode == PathOpen) {
                if ((path->front().X != path->back().X) || (path->front().Y != path->back().Y)) {
                    dx = (start_point.X - path->back().X);
                    dxlo.i = _mul128(dx, dx, &dxhi.i);
                    dy = (start_point.Y - path->back().Y);
                    dylo.i = _mul128(dy, dy, &dyhi.i);

                    sqdistlo.u = dxlo.u+dylo.u;
                    sqdisthi.i = dxhi.i+dyhi.i+(sqdistlo.u<dxlo.u);

                    if ((sqdisthi.i < minsqdisthi.i) || ((sqdisthi.i == minsqdisthi.i) && (sqdistlo.u < minsqdistlo.u))) {
                        idx = (int)(path - beginpath);
                        isfront = false;
                        minsqdistlo = sqdistlo;
                        minsqdisthi = sqdisthi;
                        if ((minsqdistlo.u == 0) && (minsqdisthi.i == 0)) break;
                    }
                }
            }
#   endif
#else
            dx = (double)(start_point.X - path->front().X);
            dy = (double)(start_point.Y - path->front().Y);
            sqdist = dx*dx + dy*dy;
            if (sqdist < minsqdist) {
                idx = (int)(path - beginpath);
                isfront = true;
                minsqdist = sqdist;
                if (minsqdist == 0.0) break;
            }
            if (mode == PathOpen) {
                if ((path->front().X != path->back().X) || (path->front().Y != path->back().Y)) {
                    dx = (double)(start_point.X - path->back().X);
                    dy = (double)(start_point.Y - path->back().Y);
                    sqdist = dx*dx + dy*dy;
                    if (sqdist < minsqdist) {
                        idx = (int)(path - beginpath);
                        isfront = false;
                        minsqdist = sqdist;
                        if (minsqdist == 0.0) break;
                    }
                }
            }
#endif
        }
        ++v;
    }
    idx_result = idx;
    isfront_result = isfront;
}


//#define BENCHMARK 
#ifdef BENCHMARK 
#  include <windows.h>
#  define BENCHFILE stderr
#  define BENCHGETTICK(tick)                 QueryPerformanceCounter(&(tick));
#  define BENCHPRINTF(...)                   {fprintf(BENCHFILE, __VA_ARGS__);}
#  define SHOWBENCHMARK(string, tInit, tEnd) {fprintf(BENCHFILE, string, (double)((tEnd.QuadPart - tInit.QuadPart) * 1.0 / frequency.QuadPart));}
#  include <iostream>
#  define WAIT                               {int x; std::cin >> x;}
#else
#  define BENCHGETTICK(...)                  {}
#  define BENCHPRINTF(...)                   {}
#  define SHOWBENCHMARK(...)                 {}
#  define WAIT                               {}
#endif

//TODO: POSSIBLY SIMPLE OPTIMIZATION HEURISTIC IF A 2D-TREE IS USED:
/* 
-find NEAREST POINT Pn AT DISTANCE Dn
-consider all points within a distance Dn*factor, or possibly nearest NUM points
-consider the set VN of very near points, within Dn*smallfactor
-select as the next point one from VN such as it is the closest within the general opposite direction where most points are
*/
void verySimpleMotionPlanner(StartState &startState, PathCloseMode mode, clp::Paths &paths) {
    if (paths.empty()) return;
#ifdef BENCHMARK
    // ticks per second
    LARGE_INTEGER frequency;
    // ticks
    LARGE_INTEGER t[10], tt[10];
    double elapsedTime;
    // get ticks per second
    QueryPerformanceFrequency(&frequency);
    // start timer
    BENCHGETTICK(t[0]);
#endif  
    int idx;
    bool isfront;
    std::vector<bool> valid(paths.size(), true);

    //this hack is to enable the caller to use the same for input and output. It seems wasteful, but there is no way around copying
    clp::Paths output;
    output.reserve(paths.size());

    //copying the paths *again* seems unnecesarily wasteful, but we require it because sonme paths may be reversed
    //only if this library is interfaced with IO (iopaths.cpp), but NOT if it is interfaced with an API, the alternative is to codify the order and the reversions as a custom specification, and use it at the output time
    //so it seems like it is better to do it right here
    if (startState.notinitialized) {
        startState.notinitialized = false;
        valid[0] = false;
        output.push_back(std::move(paths.front()));
        startState.start_near = output.back().back();
    }

    auto nearest_path = mode == PathOpen ? verysimple_get_nearest_path<PathOpen> : verysimple_get_nearest_path<PathLoop>;
    BENCHGETTICK(t[0]);
    while (anyTrue(valid)) {
        //verysimple_get_nearest_path<mode>(startState.start_near, paths, valid, idx, isfront);
        nearest_path(startState.start_near, paths, valid, idx, isfront);
        if (idx < 0) {
            //this cannot possibly happen
            throw std::runtime_error("NEVER HAPPEN in verySimpleMotionPlanner()");
        }
        valid[idx] = false;
        output.push_back(std::move(paths[idx]));
        if (!isfront) {
            clp::ReversePath(output.back());
        }
        startState.start_near = output.back().back();
    }
    BENCHGETTICK(t[1]);
    SHOWBENCHMARK("TIME IN MOTION PLANNER LOOP: %f\n", t[0], t[1]);
    WAIT;

    std::swap(paths, output);
}

