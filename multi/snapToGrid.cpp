
#include "snapToGrid.hpp"
#include "showcontours.hpp"
#include <sstream>
#include <cmath>
//#include <stdio.h> 

void inline verySimpleSnapPathToGridWithShift(ClipperLib::Path &path, SnapToGridSpec &spec) {
    for (ClipperLib::Path::iterator pit = path.begin(); pit != path.end(); ++pit) {
        //discretize for the grid
        pit->X = (ClipperLib::cInt)(round((((double)pit->X) - spec.shiftX) / spec.gridstepX)*spec.gridstepX + spec.shiftX);
        pit->Y = (ClipperLib::cInt)(round((((double)pit->Y) - spec.shiftY) / spec.gridstepY)*spec.gridstepY + spec.shiftY);
    }
}

void inline verySimpleSnapPathToGridWithoutShift(ClipperLib::Path &path, SnapToGridSpec &spec) {
    for (ClipperLib::Path::iterator pit = path.begin(); pit != path.end(); ++pit) {
        //discretize for the grid
        pit->X = (ClipperLib::cInt)(round(((double)pit->X) / spec.gridstepX)*spec.gridstepX);
        pit->Y = (ClipperLib::cInt)(round(((double)pit->Y) / spec.gridstepY)*spec.gridstepY);
    }
}

void verySimpleSnapPathsToGrid(ClipperLib::Paths &paths, SnapToGridSpec &spec) {
    if ((spec.shiftX == 0.0) && (spec.shiftY == 0.0)) {
        for (clp::Paths::iterator path = paths.begin(); path != paths.end(); ++path) {
            verySimpleSnapPathToGridWithoutShift(*path, spec);
        }
    } else {
        for (clp::Paths::iterator path = paths.begin(); path != paths.end(); ++path) {
            verySimpleSnapPathToGridWithShift(*path, spec);
        }
    }
}


void verySimpleSnapPathToGrid(ClipperLib::Path &path, SnapToGridSpec &spec) {
    if ((spec.shiftX == 0.0) && (spec.shiftY == 0.0)) {
        verySimpleSnapPathToGridWithoutShift(path, spec);
    } else {
        verySimpleSnapPathToGridWithShift(path, spec);
    }
}

#define BOTHOVER0(a,b)    ( ( ((a)>0) - ((b)>0) ) == 0 )
#define NOTBOTHOVER0(a,b) ( ( ((a)>0) - ((b)>0) ) != 0 )

void populateInfoStruct(ClipperLib::IntPoint *array, int npoints, ClipperLib::cInt fx, ClipperLib::cInt fy, ClipperLib::IntPoint &point, int num, SnapToGridSpec &spec, gridInfo *gridinfo) {
  //printf("ERROR IN POINT %d/%ld: %lld, %lld\n", k, inputs.size(), x, y);
  if (gridinfo!=NULL) {
    gridinfo->npoints=npoints;
    for (int i=0; i<npoints; ++i) {
      gridinfo->grid[i].X = (ClipperLib::cInt)((fx+array[i].X)*spec.gridstepX+spec.shiftX);
      gridinfo->grid[i].Y = (ClipperLib::cInt)((fy + array[i].Y)*spec.gridstepY + spec.shiftY);
    }
    gridinfo->point     = point;
    gridinfo->numPoint  = num;
  } 
}


inline bool getIndex(SnapToGridSpec &spec, ClipperLib::Path &inputs, ClipperLib::IntPoint &selected, ClipperLib::IntPoint &selectedn, ClipperLib::IntPoint *array, ClipperLib::cInt x, ClipperLib::cInt y, ClipperLib::cInt fx, ClipperLib::cInt fy, int np, bool testinside) {
  ClipperLib::IntPoint testp, testpn;
  bool considerThisPoint, nofound = true;
  double dist, mindist = INFINITY;
  ClipperLib::cInt dx, dy;
  int inpol;
  
  for (int i=0;i<np;++i) {
    testpn.X  = fx+array->X;
    testpn.Y  = fy+array->Y;
    ++array;
    //get the grid point in clipper coordinates
    testp.X   = (ClipperLib::cInt)(testpn.X*spec.gridstepX+spec.shiftX);
    testp.Y   = (ClipperLib::cInt)(testpn.Y*spec.gridstepY+spec.shiftY);
    //is the grid point inside the polygon?
    inpol     = ClipperLib::PointInPolygon(testp, inputs);

    /*considerThisPoint means that the grid point will be tested to see if
      it is the closest to the original point. the result of PointInPolygon is:
            1 if the point is              INSIDE the polygon
            0 if the point is             OUTSIDE the polygon
           -1 if the point is IN THE PERIMETER OF the polygon
      The unraveled logic is in the comments below:*/
  
//        if (spec.mode==SnapDilate) {
//          if (iscontour) {
//            considerThisPoint = inpol<=0;
//          }else { //~iscontour==ishole
//            considerThisPoint = inpol!=0;
//          }
//        } else { //spec.mode==SnapErode
//          if (iscontour) {
//            considerThisPoint = inpol!=0;
//          }else { //~iscontour==ishole
//            considerThisPoint = inpol<=0;
//          }
//        }

    /*the actual implementation consolidates the nested ifs with mirrored
      behaviours into a xor operation for using just a single level of if operations*/
    considerThisPoint = testinside ? (inpol!=0) : (inpol<=0);
    if (considerThisPoint) {
      //manhattan distance is faster than euclidean, but it is equivocal in some edge cases 
      //when considering grids wider than the four nearest grid points
      dx              = x - testp.X;
      dy              = y - testp.Y;
      dist            = sqrt(dx*dx+dy*dy);
      if (dist<mindist) { //get minimal distance
        nofound       = false;
        mindist       = dist;
        selected      = testp;
        selectedn     = testpn;
      }
    }
  }
  return nofound;
}


//relative coordinates for four grid points around the point we are considering
static ClipperLib::IntPoint relativesA[4]  = {ClipperLib::IntPoint( 0, 0),
                                              ClipperLib::IntPoint( 0, 1),
                                              ClipperLib::IntPoint( 1, 1),
                                              ClipperLib::IntPoint( 1, 0)};
      
//if the point we are considering has the X coordinate already snapped to grid, we may use this wider grid if instructed to do so
static ClipperLib::IntPoint relativesB[6]  = {ClipperLib::IntPoint( 0, 0),
                                              ClipperLib::IntPoint( 0, 1),
                                              ClipperLib::IntPoint( 1, 1),
                                              ClipperLib::IntPoint( 1, 0),
                                              ClipperLib::IntPoint(-1, 0),
                                              ClipperLib::IntPoint(-1, 1)};
      
//if the point we are considering has the Y coordinate already snapped to grid, we may use this wider grid if instructed to do so
static ClipperLib::IntPoint relativesC[6]  = {ClipperLib::IntPoint( 0, 0),
                                              ClipperLib::IntPoint( 0, 1),
                                              ClipperLib::IntPoint( 1, 1),
                                              ClipperLib::IntPoint( 1, 0),
                                              ClipperLib::IntPoint( 0,-1),
                                              ClipperLib::IntPoint( 1,-1)};

//relative coordinates of the 12 grid points in the outer square (relative to relativesA)
static ClipperLib::IntPoint relativesD[12] = {ClipperLib::IntPoint(-1,-1),
                                              ClipperLib::IntPoint(-1, 0),
                                              ClipperLib::IntPoint(-1, 1),
                                              ClipperLib::IntPoint(-1, 2),
                                              ClipperLib::IntPoint( 0, 2),
                                              ClipperLib::IntPoint( 1, 2),
                                              ClipperLib::IntPoint( 2, 2),
                                              ClipperLib::IntPoint( 2, 1),
                                              ClipperLib::IntPoint( 2, 0),
                                              ClipperLib::IntPoint( 2,-1),
                                              ClipperLib::IntPoint( 1,-1),
                                              ClipperLib::IntPoint( 0,-1)};

//these are the first two concentric squares (relativesA+relativesD)
static ClipperLib::IntPoint relativesE[16] = {ClipperLib::IntPoint( 0, 0),
                                              ClipperLib::IntPoint( 0, 1),
                                              ClipperLib::IntPoint( 1, 1),
                                              ClipperLib::IntPoint( 1, 0),
                                              ClipperLib::IntPoint(-1,-1),
                                              ClipperLib::IntPoint(-1, 0),
                                              ClipperLib::IntPoint(-1, 1),
                                              ClipperLib::IntPoint(-1, 2),
                                              ClipperLib::IntPoint( 0, 2),
                                              ClipperLib::IntPoint( 1, 2),
                                              ClipperLib::IntPoint( 2, 2),
                                              ClipperLib::IntPoint( 2, 1),
                                              ClipperLib::IntPoint( 2, 0),
                                              ClipperLib::IntPoint( 2,-1),
                                              ClipperLib::IntPoint( 1,-1),
                                              ClipperLib::IntPoint( 0,-1)};
      
int snapPathToGrid(ClipperLib::Path &outputs, ClipperLib::Path &inputs, SnapToGridSpec &spec, gridInfo *gridinfo) {
  double numstepsX, numstepsY, fdist, dx, dy;
  ClipperLib::IntPoint *array, selected, selectedn, lastselectedn, prevdelta, thisdelta;
  bool iscontour, testinside, addpoint, nofound,
       collapsedx, collapsedy, testdist,
       nofirstone = false, lessthantwo = true;
  int np, outk=0, k=0;//, kk;
  ClipperLib::cInt x, y, fx, fy;

  if (spec.mode != SnapSimple) {
      iscontour = ClipperLib::Orientation(inputs); //contour or hole
      testinside = (spec.mode == SnapDilate) ^ iscontour; //see explanation about this in getIndex()
  }

  outputs.resize(inputs.size());

  ClipperLib::IntPoint *out_it = (&(*(outputs.begin())))-1;

  for(ClipperLib::Path::iterator pit = inputs.begin(); pit != inputs.end(); ++pit,++k) {
//printf("In POINT %d, %d\n", k, spec.numSquares);
    x             = pit->X;
    y             = pit->Y;
    //discretize for the grid
    numstepsX     = (((double)x)-spec.shiftX)/spec.gridstepX;
    numstepsY     = (((double)y)-spec.shiftY)/spec.gridstepY;

    if (spec.mode == SnapSimple) {

        selectedn.X = (ClipperLib::cInt)round(numstepsX);
        selectedn.Y = (ClipperLib::cInt)round(numstepsY);
        selected.X  = (ClipperLib::cInt)(selectedn.X*spec.gridstepX + spec.shiftX);
        selected.Y  = (ClipperLib::cInt)(selectedn.Y*spec.gridstepY + spec.shiftY);

    } else {

        dx = floor(numstepsX);
        dy = floor(numstepsY);
        fx = (ClipperLib::cInt)dx;
        fy = (ClipperLib::cInt)dy;
        collapsedx = dx == numstepsX;
        collapsedy = dy == numstepsY;
        if (collapsedx && collapsedy) {
            /*the point is exactly on the grid, bypass all the stuff below*/
            selected = *pit;
            selectedn.X = fx;
            selectedn.Y = fy;
        } else {

            array = relativesA;
            np = 4;
            testdist = false;
            if (collapsedx) {
                // printf("POINT %d, COLLAPSEDX\n", k);
                array = relativesB;
                np = 6;
                testdist = true;
            } else if (collapsedy) {
                // printf("POINT %d, COLLAPSEDY\n", k);
                array = relativesC;
                np = 6;
                testdist = true;
            }

            nofound = getIndex(spec, inputs, selected, selectedn, array, x, y, fx, fy, np, testinside);

            if (nofound) {
                // printf("POINT %d, OUTER SQUARE\n", k);
                //try with a wider grid, but this time be careful with the true distance
                testdist = true;
                array = relativesD;
                np = 12;
                nofound = getIndex(spec, inputs, selected, selectedn, array, x, y, fx, fy, np, testinside);
                if (nofound) {
                    //this may or may not be potentially dangerous, more research is needed,
                    //but it is highly unlikely that it will crop up. Return an error to
                    //be able to see the problem
                    populateInfoStruct(relativesE, 16, fx, fy, *pit, k, spec, gridinfo);
                    if (collapsedx) return 2;
                    if (collapsedy) return 3;
                    else            return 4;
                    //          //alternatively, we may just skip this point
                    //          continue;
                }
            }

            if (testdist) {
                dx = (double)(x - selected.X);
                dy = (double)(y - selected.Y);
                fdist = sqrt(dx*dx + dy*dy);
                if (fdist>spec.maxdist) {
                    // printf("ERRINFO: gridstep=%f, maxdist=%f, dist=%f\n", spec.gridstepX, spec.maxdist, fdist);
                    // printf("      dx=%f, dy=%f\n", dx, dy);
                    //this may or may not be potentially dangerous, more research is needed,
                    //but it is highly unlikely that it will crop up. Return an error to
                    //be able to see the problem
                    populateInfoStruct(relativesE, 16, fx, fy, *pit, k, spec, gridinfo);
                    if (collapsedx) return 5;
                    if (collapsedy) return 6;
                    else            return 7;

                    //printf("just skip point %d, as we cannot safely snap it to grid\n", k);
                    //printf("    found=%d, mindist=%f, maxdist=%f\n", found, fdist, spec.maxdist);
                    //printf("    origpoint=%lld,%lld\n", x, y);
                    //printf("    snappoint=%lld,%lld\n", testp.X, testp.Y);//        continue;

                    //          //alternatively, we may just skip this point
                    //          continue;
                }

            }

        }

    }

    /*now, let's add (or not) the selected point to the path.

      it seems like this code is very complicated, with subtle effects and
      *very* complex conditionals. It can be refactored to use many nested 
      if/else statements with simpler conditionals. However, I find far more 
      difficult to reason about a code with many nested levels of if/else 
      branches than code with low nesting and more complex conditionals, even 
      if the effective cyclomatic complexity is the same in both cases*/

    //this branch is executed always except in the first iteration
    if (nofirstone) {
      thisdelta.X     = lastselectedn.X-selectedn.X;
      thisdelta.Y     = lastselectedn.Y-selectedn.Y;
      /*add this point for sure if this is the first one or it is NOT the same as 
        the previous point. Otherwise, if we are allowed by spec.removeRedundant,
        do the test below to be sure*/
      if ((thisdelta.X==0) & (thisdelta.Y==0)) {
        continue;
      }
      //we use & and | instead of && and || everywhere, because the conditional evaluation 
      //of the second argument is not important for us, but it imposes a branch penalty
      if (spec.removeRedundant) {
        /*VERY IMPORTANT: the test
                (prevdelta.X*thisdelta.Y) != (prevdelta.Y*thisdelta.X)
          may overflow (thus producing undefined behaviour), but we are 
          counting on using 64 bit variables for values that should not be 
          very large (these are deltas not on the Clipper scale, but on 
          the much smaller stepping grid)*/
        /*ALSO  IMPORTANT: the logic mentioned in the previous comment
          considers two deltas to be equivalent even if they are opposite. 
          Example: for The pairs [(2,3),(4,6)] and [(-2,3),(-4,6)]
          it works as expected, but it computes as equivalent also the 
          pair [(-2,3),(4,-6)]: (-2,3) and (4,-6) are equivalent 
          modulo normalization, but represent opposite directions.
          However, we understand that this situation can arise 
          only in pathological ClipperPaths, so we don't care.
          If we'd care, it should be easy to refine the test to avoid
          this, just uncomment the code below*/
        addpoint          = lessthantwo | (
                            ( (prevdelta.X*thisdelta.Y) !=
                              (prevdelta.Y*thisdelta.X) )
                        /*  || (NOTBOTHOVER0(prevdelta.X, thisdelta.X) ||
                                NOTBOTHOVER0(prevdelta.Y, thisdelta.Y)) */
                                        );
        lessthantwo       = false;
        if ( addpoint ) {
          //add the point, reset the chain
          prevdelta       = thisdelta; //get the new base direction for this chain.
        } else {
//printf("         -> POINT %d OMITTED\n", k);
          //replace the point
          *out_it         = selected;
          lastselectedn   = selectedn;
          continue;
        }
      }
    }

    /*add point [optionally: only if it is not in the same line as the previous one]

      at first glance, it looks like we should also test for complex topological
      conditions such as loops, intersections, or other parts of the perimeter
      that are geometrically close but not neighbouring in the ClipperPath. 
      However, this function is only to be applied to paths which have been 
      smoothed by opening with a relatively big radius value, and whose 
      high-resolution details in negative space have been removed, 
      so it shouldn't matter...
    */
    nofirstone      = true;
    lastselectedn   = selectedn;
    *(++out_it)     = selected;
    ++outk;

  }

  outputs.resize(outk);

//if (outk!=inputs.size()) printf("->MIRA INK %lu, OUTK: %d\n", inputs.size(), outk);

  if (outk<3) {
    //this may happen in some corner cases: just notify the caller
//testp.X=x;
//testp.Y=y;
//populateInfoStruct(fx, fy, startIdx-2, endIdx+2, testp, k, spec, gridinfo);
    return 1;
  }

  /*ONCE THE WHOLE OUTPUT PATH HAS BEEN COMPUTED: it would be interesting to
    add an option to smooth it by some amount. For +/-1 steppings in X/Y, the
    algorithm would be simple: if eroding/dilating (whatever is the proper
    behaviour) a point to a neighbouring grid point removes small segments,
    do it.
    IMPORTANT: this should be done only if the size of the small radius is
    not too small, otherwise the high-res area would get really large, and
    consequently slow
    ALSO IMPORTANT: however, further erosion may result
    in self-intersecting or topologically invalid toolpaths...
    */

  return 0;
}

const double factor = 30.0;

void addPointAsPlus(SnapToGridSpec &snapspec, clp::Paths &lines, clp::IntPoint p) {
    lines.emplace_back(2);
    lines.back()[0].X = p.X - (clp::cInt)(snapspec.gridstepX * factor);
    lines.back()[0].Y = p.Y;
    lines.back()[1].X = p.X + (clp::cInt)(snapspec.gridstepX * factor);
    lines.back()[1].Y = p.Y;
    lines.emplace_back(2);
    lines.back()[0].X = p.X;
    lines.back()[0].Y = p.Y - (clp::cInt)(snapspec.gridstepY * factor);
    lines.back()[1].X = p.X;
    lines.back()[1].Y = p.Y + (clp::cInt)(snapspec.gridstepY * factor);
}

void addPointAsCross(SnapToGridSpec &snapspec, clp::Paths &lines, clp::IntPoint p) {
    lines.emplace_back(2);
    lines.back()[0].X = p.X - (clp::cInt)(snapspec.gridstepX * factor);
    lines.back()[0].Y = p.Y - (clp::cInt)(snapspec.gridstepY * factor);
    lines.back()[1].X = p.X + (clp::cInt)(snapspec.gridstepX * factor);
    lines.back()[1].Y = p.Y + (clp::cInt)(snapspec.gridstepY * factor);
    lines.emplace_back(2);
    lines.back()[0].X = p.X + (clp::cInt)(snapspec.gridstepX * factor);
    lines.back()[0].Y = p.Y - (clp::cInt)(snapspec.gridstepY * factor);
    lines.back()[1].X = p.X - (clp::cInt)(snapspec.gridstepX * factor);
    lines.back()[1].Y = p.Y + (clp::cInt)(snapspec.gridstepY * factor);
}

void showError(Configuration &config, SnapToGridSpec &snapspec, size_t inputIdx, clp::Path &_input, gridInfo &gridinfo, int errcode) {
    clp::Paths grid;
    clp::Paths errpoint;
    clp::Paths input(1, _input);
    errpoint.reserve(2);
    grid.reserve(gridinfo.npoints * 2);
    addPointAsCross(snapspec, errpoint, gridinfo.point);
    for (int k = 0; k < gridinfo.npoints; ++k) {
        addPointAsPlus(snapspec, grid, gridinfo.grid[k]);
    }
    const char * additional = (errcode >= 5) ? " (this may be alleviated by removing --safestep from the affected process specification)" : "";
    SHOWCONTOURS(config, str("Error while snapping point ", gridinfo.numPoint, " of path ", inputIdx, ", errcode: ", errcode, additional), &grid, &errpoint, &input);
}

bool snapClipperPathsToGrid(Configuration &config, clp::Paths &output, clp::Paths &inputs, SnapToGridSpec &snapspec, std::string &err) {
    gridInfo gridinfo;
    size_t s = inputs.size();
    output.resize(s);
    int numout = 0;
    /*if debug:
    otro               = _c.ClipperPaths()
    otro .thisptr[0].resize(s)*/
    for (size_t i = 0; i<s; ++i) {
        int result = snapPathToGrid(output[numout], inputs[i], snapspec, &gridinfo);
        /*TODO: INSTEAD OF JUST DISCARDING THE PATH WHEN result==1, WE MAY KEEP IT
        BUT TAGGING IT AS A NON-CONTOUR, AND WE SHOULD TRY TO OFFSET IT TAKING
        INTO ACCOUNT THAT IT IS AN OPEN PATH*/
        numout += result == 0;
        if (result>1) {
#ifdef CORELIB_USEPYTHON
            showError(config, snapspec, i, inputs[i], gridinfo, result);
#endif
            std::ostringstream fmt;
            const char * additional = (result >= 5) ? "\n    This may be alleviated by removing --safestep from the affected process specification\n" : "\n";
            fmt << "error in snapPathToGrid for the path " << i << "/" << (s - 1) << ".\n    ERRORCODE: " << result << "\n    numPoint: " << gridinfo.numPoint << "\n    X=" << gridinfo.point.X << "\n    Y=" << gridinfo.point.Y << additional;
#ifndef CORELIB_USEPYTHON
            fmt << "\n    To see a graphical representation of the configuration that led to this error, please recompile the project with python support.\n"
#endif
            err = fmt.str();
            return false;
        }
    }
    output.resize(numout);
    return true;
}
