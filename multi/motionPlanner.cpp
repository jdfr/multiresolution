#include "motionPlanner.hpp"

//#define TEST_INTRINSIC
#ifdef TEST_INTRINSIC
#include <stdio.h>
#include <stdexcept>
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
#   elif defined(_WIN64) && defined(_MSC_VER)
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
#   else
#       error For now, the only supported compilers are GCC and MSVC in 64 bit mode
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
void verySimpleMotionPlannerHelper(StartState &startState, PathCloseMode mode, clp::Paths &paths, std::vector<bool> &valid, int &numvalid, clp::Paths &output) {
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
    
    auto nearest_path = mode == PathOpen ? verysimple_get_nearest_path<PathOpen> : verysimple_get_nearest_path<PathLoop>;
    BENCHGETTICK(t[0]);
    while (numvalid>0) {
        //verysimple_get_nearest_path<mode>(startState.start_near, paths, valid, idx, isfront);
        nearest_path(startState.start_near, paths, valid, idx, isfront);
        if (idx < 0) {
            //this cannot possibly happen
            throw std::runtime_error("NEVER HAPPEN in verySimpleMotionPlanner()");
        }
        valid[idx] = false;
        --numvalid;
        bool add   = true;
        clp::Path &path = paths[idx];
        if (!isfront)        clp::ReversePath(path);
        if (!output.empty()) add = output.back().back() != path.front();
        if (add) {
            output.push_back(std::move(path));
        } else {
            output.back().reserve(output.back().size()+path.size());
            std::move(path.begin()+1, path.end(), std::back_inserter(output.back()));
        }
        startState.start_near = output.back().back();
    }
    BENCHGETTICK(t[1]);
    SHOWBENCHMARK("TIME IN MOTION PLANNER LOOP: %f\n", t[0], t[1]);
    WAIT;
}

void verySimpleMotionPlanner(StartState &startState, PathCloseMode mode, clp::Paths &paths) {
    std::vector<bool> valid(paths.size(), true);
    int numvalid = (int)paths.size();
    //this hack is to enable the caller to use the same for input and output. It seems wasteful, but there is no way around copying
    //copying the paths *again* seems unnecesarily wasteful, but we require it because sonme paths may be reversed
    //only if this library is interfaced with IO (iopaths.cpp), but NOT if it is interfaced with an API, the alternative is to codify the order and the reversions as a custom specification, and use it at the output time
    //so it seems like it is better to do it right here
    clp::Paths output;
    output.reserve(paths.size());
    
    if (startState.notinitialized) {
        startState.notinitialized = false;
        valid[0] = false;
        --numvalid;
        output.push_back(std::move(paths.front()));
        startState.start_near = output.back().back();
    }
    
    verySimpleMotionPlannerHelper(startState, mode, paths, valid, numvalid, output);
    
    paths = std::move(output);
}

bool almost_equal(clp::IntPoint &pointA, clp::IntPoint &pointB) {
    const clp::cInt almost_equal_value = 3; //maybe TODO: make this a configuration value instead of a constant    
    return (std::abs(pointA.X - pointB.X) <= almost_equal_value) &&
           (std::abs(pointA.Y - pointB.Y) <= almost_equal_value);
}

bool SaferOverhangingVerySimpleMotionPlanner::tryToConcat(clp::Paths &paths, std::vector<bool> &valid, int &this_numvalid, int &idx, bool &isfront) {
    nearest_path(startState.start_near, paths, valid, idx, isfront);
    if (idx < 0) {
        //this cannot possibly happen
        throw std::runtime_error("NEVER HAPPEN in SaferOverhangingVerySimpleMotionPlanner::tryToConcat()");
    }
    clp::Path &path = paths[idx];
    //TODO: right now, this code does not support clearance (i.e., toolpaths that cannot overlap)
    // to support clearance, we should be able to erode the lines by one of their ends, and incorporate
    //the logic to erode the lines instead of just laying them down to have coincident ends.
    //Anyway, this would be a nice-to-have feature even for non-clearance toolpaths.
    if (almost_equal(output.back().back(), isfront ? path.front() : path.back())) {
        if (isfront) {
            std::move(path. begin()+1, path. end(), std::back_inserter(output.back()));
        } else {
            std::move(path.rbegin()+1, path.rend(), std::back_inserter(output.back()));
        }
        startState.start_near = output.back().back();
        valid[idx] = false;
        --this_numvalid;
        --numvalid;
        return true;
    }
    return false;
}

void SaferOverhangingVerySimpleMotionPlanner::addInPath() {
    clp::Path &path = in[idx_in];
    if (!isfront_in) clp::ReversePath(path);
    bool copyFullPath = true;
    if (keepStartInsideSupport) {
        //try to avoid situations where you will put an *in* toolpath whose start end is coincident with an *out* toolpath
        int idx_out1, idx_out2;
        bool isfront_out1, isfront_out2;
        nearest_path(path.front(), out, valid_out, idx_out1, isfront_out1);
        if ((idx_out1 < 0)) {
            //this cannot possibly happen
            throw std::runtime_error("NEVER HAPPEN in saferOverhangingVerySimpleMotionPlanner()");
        }
        clp::Path &path_out1 = out[idx_out1];
        bool frontIsExtended = almost_equal(path.front(), isfront_out1 ? path_out1.front() : path_out1.back());
        if (frontIsExtended) {
            /*in this case, the front of the toolpath is extended in the *out* partition. Now:
                  *   If the back of the toolpath is also extended, we divide the toolpath in two (by its representation midpoint,
                      but it would be better to divide it according to the geometrical midpoint), in order to be able
                      to write the overhangs starting entirely from already existing supported toolpaths
                  *   Otherwise, we reverse the toolpath (even if this means violating the greedy ordering algorithm),
                      to avoid later starting a toolpaths from the edge of the support
            */
            nearest_path(path.back(),  out, valid_out, idx_out2, isfront_out2);
            if ((idx_out2 < 0)) {
                //this cannot possibly happen
                throw std::runtime_error("NEVER HAPPEN in saferOverhangingVerySimpleMotionPlanner()");
            }
            clp::Path &path_out2 = out[idx_out2];
            bool  backIsExtended = almost_equal(path.back(),  isfront_out2 ? path_out2.front() : path_out2.back());
            if (backIsExtended) {
                copyFullPath = false;
                //TODO: right now, this code does not support clearance (i.e., toolpaths that cannot overlap)
                // to support clearance, we should be able to erode the lines by one of their ends, and incorporate
                //the logic to erode the lines instead of just creating them to have coincident ends.
                //Anyway, this would be a nice-to-have feature even for non-clearance toolpaths.
                switch (path.size()) {
                    case 0:
                        throw std::runtime_error("inconsistent empty path in saferOverhangingVerySimpleMotionPlanner()");
                    case 1:
                        throw std::runtime_error("inconsistent point path in saferOverhangingVerySimpleMotionPlanner()");
                    case 2: {
                        output.push_back(clp::Path(2));
                        clp::IntPoint meanPoint((path.front().X+path.back().X)/2, (path.front().Y+path.back().Y)/2); 
                        output.back().front() = meanPoint;
                        output.back().back()  = path.back();
                        path.back() = meanPoint;
                        break;
                    } default: {
                        int meanPoint = (int)path.size()/2;
                        output.emplace_back();
                        output.back().reserve(path.size()-meanPoint);
                        std::move(path.begin()+meanPoint, path.end(), std::back_inserter(output.back()));
                        path.resize(path.size()-output.back().size()+1);
                    }
                }
            } else {
                //in this case, violate motion planning. The other side of the path may be far away, but it is better to do it from here
                //to avoid unnecesarily broken toolpaths
                //TODO: modify this algorithm to try to pick another toolpath instead of insisting in applying this one
                clp::ReversePath(path);
            }
        }
    }
    if (copyFullPath) {
        output.push_back(std::move(path));
        valid_in[idx_in] = false;
        --numvalid_in;
        --numvalid;
    }
    startState.start_near = output.back().back();
}



void SaferOverhangingVerySimpleMotionPlanner::saferOverhangingVerySimpleMotionPlanner(int ntool, clp::Paths &support, PathCloseMode mode, clp::Paths &paths) {
    if (paths.empty()) return;

    //compute partition of toolpaths into segments that are *in* and *out* of the support
    {
        clp::PolyTree pt;
        res.clipper.AddPaths(support, clp::ptClip, true);
        res.clipper.AddPaths(paths, clp::ptSubject, false);
        res.clipper.Execute(clp::ctIntersection, pt, clp::pftNonZero, clp::pftNonZero);
        OpenPathsFromPolyTree(pt, in);
        pt.Clear();
        res.clipper.Execute(clp::ctDifference,   pt, clp::pftNonZero, clp::pftNonZero);
        OpenPathsFromPolyTree(pt, out);
        pt.Clear();
        res.clipper.Clear();
        ClipperEndOperation(res.clipper);
    }
    
    //if there are no toolpaths without support (or all toolpaths are without support), just use plain motion planning
    if (out.empty() || in.empty()) {
        verySimpleMotionPlanner(startState, mode, paths);
        clear();
        return;
    }
    
    nearest_path = mode == PathOpen ? &verysimple_get_nearest_path<PathOpen> : &verysimple_get_nearest_path<PathLoop>;
    keepStartInsideSupport = res.spec->pp[ntool].keepStartInsideSupport;
    
    valid_in .assign(in .size(), true);
    valid_out.assign(out.size(), true);
    numvalid_in  = (int)in .size();
    numvalid_out = (int)out.size();
    numvalid     = numvalid_in + numvalid_out;
    
    //this hack is to enable the caller to use the same for input and output. It seems wasteful, but keeping track of everything (ordering, reversed orientations, fused toolpaths, partially broken toolpaths, etc) may get hairy quite fast
    output.reserve((paths.size()+paths.size()/10));
    
    //add first toolpath from the *in* partition
    if (startState.notinitialized) {
        //get just 
        startState.notinitialized = false;
        idx_in = 0;
        isfront_in  = true;
    } else {
        nearest_path(startState.start_near, in, valid_in, idx_in, isfront_in);
        if (idx_in < 0) {
            //this cannot possibly happen
            throw std::runtime_error("NEVER HAPPEN in saferOverhangingVerySimpleMotionPlanner()");
        }
    }
    addInPath();
    
    bool tryoutfirst = true;
    
    //main loop: keep trying to concat to the current toolpath. If not possible, go to the next nearest toolpath in the *in* partition
    while (numvalid>0) {
        //when some of the two kinds of toolpaths is exhausted, just plan the motion of the rest
        if (numvalid_in == 0) {
            if (numvalid_out == 0) break;
            verySimpleMotionPlannerHelper(startState, mode, out, valid_out, numvalid_out, output);
            break;
        }
        if (numvalid_out == 0) {
            verySimpleMotionPlannerHelper(startState, mode,  in, valid_in,  numvalid_in,  output);
            break;
        }
        //try to concatenate a toolpath
        if (tryoutfirst) {
            tryoutfirst = false;
            if (tryToConcat(out, valid_out, numvalid_out, idx_out, isfront_out)) continue;
            tryoutfirst = true;
            if (tryToConcat(in,  valid_in,  numvalid_in,  idx_in,  isfront_in))  continue;
        } else {
            tryoutfirst = true;
            if (tryToConcat(in,  valid_in,  numvalid_in,  idx_in,  isfront_in))  continue;
            tryoutfirst = false;
            if (tryToConcat(out, valid_out, numvalid_out, idx_out, isfront_out)) continue;
        }
        //could not concatenate a toolpath: add the nearest path in the *in* partition (guaranteed to not be empty)
        tryoutfirst = true;
        addInPath();
    }

    paths = std::move(output);
    clear();
}