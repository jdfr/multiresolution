#ifndef MEDIALAXIS_HEADER
#define MEDIALAXIS_HEADER

#include "auxgeom.hpp"

/*  this does not work right because some of the medial axes are generated as intersecting lines.
    An obvious fix would be to find the intersections between the axes. This would require to
    extract some functionality from clipper.cpp to implement clipping of open polygons
    (difficult at this stage) or find an easier to use clipping package
*/
//#define TRY_TO_AVOID_EXTENDING_BIFURCATIONS

/* if this method returns true, the coordinates have been scaled in and out of int32 safe limits,
* because boost's voronoi default settings internally use int32 for the coordinates.
* This is because it needs at some point to multiply coordinates, so it requires to store the result in a suitably wide variable.
* Now, int128 is not supported everywhere. An obvious way to cope with this may be to use double floating points to store the result of multiplications,
* but this reduces the accuracy in nontrivial ways (no issues near the origin, but possibly significant distortions for very large values).
* A compromise is to translate and scale (only if needed) the coordinates */
bool buildMedialAxis(HoledPolygon &hp, clp::Paths &paths, double min_width);
void prunedMedialAxis(HoledPolygon &hp, clp::Clipper &clipper, clp::Paths &lines, double min_width, double max_width
#ifdef TRY_TO_AVOID_EXTENDING_BIFURCATIONS
    , clp::cInt TOLERANCE
#endif
);


#endif
