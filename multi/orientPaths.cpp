//#define DEBUG_ORIENT

#ifdef DEBUG_ORIENT
  #include <stdio.h> 
  #define DO_DEBUG_ORIENT(x) x
#else
  #define DO_DEBUG_ORIENT(x)
#endif

#include "orientPaths.hpp"
#include <algorithm>

//adapted from a very readable and concise python implementation. sigh...

typedef std::vector<bool> orients_t;
typedef orients_t::iterator orients_t_i;

typedef std::vector<double> areas_t;
typedef std::vector<double>::iterator areas_t_i;

typedef std::pair<double,int> mypair;
typedef std::vector<mypair> mypairs;
typedef mypairs::iterator mypairs_i;
typedef mypairs::reverse_iterator mypairs_ri;

struct treenode;

typedef std::vector<treenode> trees;
typedef trees::iterator trees_i;

typedef struct treenode {
    size_t index;
    trees children;
    treenode(size_t i) : index(i), children(trees()) {};
} treenode;

inline bool mycomp( const mypair& l, const mypair& r) {
    return l.first < r.first;
}

void initAreas(clp::Paths &paths, areas_t &areas, mypairs &absareas, orients_t &orients) {
    mypairs_i pa = absareas.begin();
    areas_t_i ar = areas.begin();
    orients_t_i ot = orients.begin();
    int i=0;
    for (clp::Paths::iterator it = paths.begin(); it != paths.end(); ++it) {
        *(ar) = clp::Area(*it);
        *(ot++) = (*ar)>=0;
        *(pa++) = mypair(std::fabs(*ar), i++);
        ++ar;
        DO_DEBUG_ORIENT(printf("  INITAREA %d: %f, %f, %d\n", i-1, *(ar-1), (pa-1)->first, (pa-1)->second));
    }
    std::sort(absareas.begin(), absareas.end(), mycomp);
}

  /*This function computes the nesting tree of the ClipperPaths, in order to put
  them in the right orientation. Roots (paths not contained in any other path)
  are contours, their children are holes, the grandchildren are contours, etc. */
void recursiveAddOrientation(clp::Paths &paths, orients_t &orients, size_t k, trees &nesting, bool notHole) {
    DO_DEBUG_ORIENT(printf("  INIT recursiveAddOrientation(k=%zu, size=%zu, notHole=%d)\n", k, nesting.size(), notHole); int ii=0);
    DO_DEBUG_ORIENT(puts("  BEFORE FOR"));
    for (trees_i it = nesting.begin(); it!=nesting.end(); ++it) {
        DO_DEBUG_ORIENT(printf("    LOOP %d\n", ii));
        if (clp::PointInPolygon(paths[k][0], paths[it->index])!=0) {
            recursiveAddOrientation(paths, orients, k, it->children, !notHole);
            return;
        }
    }
    DO_DEBUG_ORIENT(printf("  AFTER FOR, nesting.size()==%zu\n", nesting.size()));
    //add it to the root contours if the list is empty or no other root contained it
    nesting.push_back(treenode(k));
    DO_DEBUG_ORIENT(printf("  AFTER PUSH_BACK: size=%zu, last index==k=%d\n", nesting.size(), nesting[nesting.size()-1].index));
    /*the path is reversed if it is a contour (non-hole) and the orientation 
    is false (clockwise) or it is not a contour and the orientation is true
    (counter-clockwise)*/
    if (notHole != orients[k]) {
        //printf("  REVERSING!!!! k==%zu, notHole==%d, orients[k]==%s, paths.size()==%zu\n", k, notHole, orients[k] ? "true" : "false", paths.size());
        clp::ReversePath(paths[k]);
    }
    DO_DEBUG_ORIENT(printf("  END recursiveAddOrientation(k=%zu, size=%zu, notHole=%d)\n", k, nesting.size(), notHole));
}

//implementation of public interface

  /*this function requires the paths to be strictly nested,
  (neither intersections nor overlapping lines are allowed)
  otherwise the result is undefined. Takes areas into account
  to minimize the number of calls to pointInPolygon.
  PLEASE NOTE: slic3r does not do it this way, because it
  makes sure that contours and holes already get opposite
  orientations in the slicing stage, by making all triangle
  normals to point in the same in/out direction. It also
  takes into account spurious contours from internal shells.
  It works out which are proper contours and which are holes, and subtracts
  the holes from the contours. Expensive but more reliable, of course*/
void orientPaths(clp::Paths &paths) {
    size_t numpaths = paths.size();
    areas_t areas(numpaths);
    mypairs absareas(numpaths);
    orients_t orients(numpaths);
    trees nesting;
    
    DO_DEBUG_ORIENT(puts("before initAreas()"));
    initAreas(paths, areas, absareas, orients);
    DO_DEBUG_ORIENT(printf("after initAreas(), paths.size()==%zu\n", paths.size()); int ii=0);
    
    //iterate from bigger to smaller areas
    for (mypairs_ri it = absareas.rbegin(); it != absareas.rend(); ++it) {
        DO_DEBUG_ORIENT(printf("Before %d pair (%f, %d)\n", ii, it->first, it->second));
        recursiveAddOrientation(paths, orients, it->second, nesting, true);
        DO_DEBUG_ORIENT(printf("after %d pair\n", ii++));
    }
    DO_DEBUG_ORIENT(printf("ENDING orientPaths(), paths.size()==%zu\n", paths.size()));
    //puts("");
}
