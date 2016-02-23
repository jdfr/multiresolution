#ifndef SNAPTOGRID_HEADER
#define SNAPTOGRID_HEADER

#include "common.hpp"
#include "config.hpp"

enum SnapMode {
    SnapDilate, //snap by dilating the contour
    SnapErode, //snap by eroding the contour
    SnapSimple //snap by rounding to the nearest point in the grid
};

//struct to define a grid snapping
typedef struct {
  double   gridstepX;
  double   shiftX;
  double   gridstepY;
  double   shiftY;
  double   maxdist;
  int      numSquares;
  SnapMode mode;
  bool     removeRedundant;
} SnapToGridSpec;

//struct to return the point being processed when an error crops up
typedef struct {
  ClipperLib::IntPoint grid[16];
  int npoints;
  ClipperLib::IntPoint point;
  size_t               numPoint;
} gridInfo;

//sophisticated functions: do snapping while dilating/eroding and simplifying the contours
int snapPathToGrid(ClipperLib::Path &outputs, ClipperLib::Path &inputs, SnapToGridSpec &spec, gridInfo *gridinfo=NULL);
bool snapClipperPathsToGrid(Configuration &config, clp::Paths &output, clp::Paths &inputs, SnapToGridSpec &snapspec, std::string &err);

//unsophisticated versions, suitable for open, very short, slightly processed toolpaths (ignores spec.mode: always works as if it is SnapSimple)
void simpleSnapPathToGrid(ClipperLib::Path &path, SnapToGridSpec &spec);
void simpleSnapPathsToGrid(ClipperLib::Paths &paths, SnapToGridSpec &spec);

//unsophisticated versions, suitable for open, very short, slightly processed toolpaths (ignores spec.mode: always works as if it is SnapSimple, and does not remove coincient points)
void verySimpleSnapPathToGrid(ClipperLib::Path &path, SnapToGridSpec &spec);
void verySimpleSnapPathsToGrid(ClipperLib::Paths &paths, SnapToGridSpec &spec);

#endif