#include "pathsplitter.hpp"
#include "showcontours.hpp"
#include <cmath>

void clipPaths(clp::Clipper &clipper, clp::Path &clip, clp::Paths &subject, bool subjectClosed, clp::PolyTree &intermediate, clp::Paths &result) {
    clipper.AddPath(clip, clp::ptClip, true);
    clipper.AddPaths(subject, clp::ptSubject, subjectClosed);
    if (subjectClosed) {
        clipper.Execute(clp::ctIntersection, result, clp::pftNonZero, clp::pftNonZero);
        clipper.Clear();
    } else {
        clipper.Execute(clp::ctIntersection, intermediate, clp::pftNonZero, clp::pftNonZero);
        clipper.Clear();
        OpenPathsFromPolyTree(intermediate, result);
        intermediate.Clear();
    }
    ClipperEndOperation(clipper, !subjectClosed ? &intermediate : (clp::PolyTree*)NULL);
}

void clipPaths(clp::Clipper &clipper, bool pathsClosed, clp::Paths &paths, Matrix<PathSplitter::EnclosedPaths> &buffer, clp::PolyTree &pt) {
    for (int x = 0; x < buffer.numx; ++x) {
        for (int y = 0; y < buffer.numy; ++y) {
            auto &enclosed = buffer.at(x, y);
            clipPaths(clipper, enclosed.actualSquare, paths, pathsClosed, pt, enclosed.paths);
        }
    }
}


bool testInsideSquare(clp::IntPoint point, clp::Path &square) {
    return (point.X >= square[0].X) && (point.X <= square[2].X) &&
           (point.Y >= square[0].Y) && (point.Y <= square[2].Y);
}

#define M_PI 3.14159265358979323846

bool PathSplitter::setup() {
    if (setup_done) return true;
    buffer.clear();
    double dispX, dispY;
    int sqminx, sqminy, sqmaxx, sqmaxy;
    if (config.useOrigin) {
        //generate rigid checkerboard pattern as specified by the origin and displacement
        clp::cInt sqdmin    = -config.margin;
        clp::cInt sqdmaxX   = config.displacement.X + config.margin;
        clp::cInt sqdmaxY   = config.displacement.Y + config.margin;
        originalSize.X      = sqdmaxX - sqdmin;
        originalSize.Y      = sqdmaxY - sqdmin;
        double numstepsMinX = ((double)(config.min.X - config.origin.X)) / config.displacement.X;
        double numstepsMinY = ((double)(config.min.Y - config.origin.Y)) / config.displacement.Y;
        double numstepsMaxX = ((double)(config.max.X - config.origin.X)) / config.displacement.X;
        double numstepsMaxY = ((double)(config.max.Y - config.origin.Y)) / config.displacement.Y;
        sqminx              = (int)std::floor(numstepsMinX);
        sqminy              = (int)std::floor(numstepsMinY);
        sqmaxx              = (int)std::ceil(numstepsMaxX);
        sqmaxy              = (int)std::ceil(numstepsMaxY);
        numx                = sqmaxx - sqminx;
        numy                = sqmaxy - sqminy;
        buffer.reset(numx, numy);
        states.reset(numx, numy);
        for (int x = 0; x < numx; ++x) {
                clp::cInt shiftx  = ((clp::cInt)(x + sqminx)) * config.displacement.X + config.origin.X;
            for (int y = 0; y < numy; ++y) {
                clp::cInt shifty  = ((clp::cInt)(y + sqminy)) * config.displacement.Y + config.origin.Y;
                auto &enclosed    = buffer.at(x,y);
                enclosed.motionPlanningState.start_near.X = enclosed.motionPlanningState.start_near.Y = 0;
                enclosed.motionPlanningState.notinitialized = true;
                clp::Path &square = enclosed.originalSquare;
                square.reserve(4);
                square.emplace_back(shiftx + sqdmin,  shifty + sqdmin);
                square.emplace_back(shiftx + sqdmaxX, shifty + sqdmin);
                square.emplace_back(shiftx + sqdmaxX, shifty + sqdmaxY);
                square.emplace_back(shiftx + sqdmin,  shifty + sqdmaxY);
            }
        }
    } else {
        //evenly distribute space among squares, using an effective displacement possible smaller than the specified one
        clp::cInt sizeX   = config.max.X - config.min.X;
        clp::cInt sizeY   = config.max.Y - config.min.Y;
        numx              = (int)std::ceil((double)sizeX / config.displacement.X);
        numy              = (int)std::ceil((double)sizeY / config.displacement.Y);
        dispX             = sizeX / (double)numx;
        dispY             = sizeY / (double)numy;
        clp::cInt sqdmin  = -config.margin;
        clp::cInt sqdmaxX = ((clp::cInt)dispX) + config.margin;
        clp::cInt sqdmaxY = ((clp::cInt)dispY) + config.margin;
        originalSize.X    = sqdmaxX - sqdmin;
        originalSize.Y    = sqdmaxY - sqdmin;
        buffer.reset(numx, numy);
        states.reset(numx, numy);
        for (int x = 0; x < numx; ++x) {
                clp::cInt shiftx  = config.min.X + (clp::cInt)(x*dispX);
            for (int y = 0; y < numy; ++y) {
                clp::cInt shifty  = config.min.Y + (clp::cInt)(y*dispY);
                auto &enclosed    = buffer.at(x,y);
                enclosed.motionPlanningState.start_near.X = enclosed.motionPlanningState.start_near.Y = 0;
                enclosed.motionPlanningState.notinitialized = true;
                clp::Path &square = enclosed.originalSquare;
                square.reserve(4);
                square.emplace_back(shiftx + sqdmin,  shifty + sqdmin);
                square.emplace_back(shiftx + sqdmaxX, shifty + sqdmin);
                square.emplace_back(shiftx + sqdmaxX, shifty + sqdmaxY);
                square.emplace_back(shiftx + sqdmin,  shifty + sqdmaxY);
            }
        }
    }
    if (cfg != NULL) {
        clp::Paths squares;
        for (auto &encl : buffer.data) {
            squares.push_back(encl.originalSquare);
            squares.back().push_back(squares.back().front());
        }
        SHOWCONTOURS(*cfg, "PathSplitter squares", &squares);
    }
    singlex    = numx == 1;
    singley    = numy == 1;
    justone    = singlex && singley;
    angle90    = justone || (std::abs(config.wallAngle - 90.0) < 1e-6);
    sinangle   = std::sin(config.wallAngle * M_PI / 180.0);
    if (config.useOrigin) {
        snapspec.gridstepX = (double)config.displacement.X;
        snapspec.gridstepY = (double)config.displacement.Y;
        snapspec.shiftX    = (double)(config.origin.X + config.displacement.X / 2 + sqminx * config.displacement.X);
        snapspec.shiftY    = (double)(config.origin.Y + config.displacement.X / 2 + sqminy * config.displacement.Y);
    } else {
        snapspec.gridstepX = dispX;
        snapspec.gridstepY = dispY;
        snapspec.shiftX    = config.min.X + dispX / 2;
        snapspec.shiftY    = config.min.Y + dispY / 2;
    }
    setup_done   = true;
    return true;
}

std::vector<TriangleMesh::Triangle> generateGridCubeTriangles() {
    return std::vector<TriangleMesh::Triangle>({
      TriangleMesh::Triangle(0, 2, 1),
      TriangleMesh::Triangle(1, 2, 3),
      TriangleMesh::Triangle(0, 4, 6),
      TriangleMesh::Triangle(0, 6, 2),
      TriangleMesh::Triangle(1, 3, 5),
      TriangleMesh::Triangle(3, 7, 5),
      TriangleMesh::Triangle(1, 5, 0),
      TriangleMesh::Triangle(0, 5, 4),
      TriangleMesh::Triangle(2, 6, 3),
      TriangleMesh::Triangle(3, 6, 7),
      TriangleMesh::Triangle(4, 7, 6),
      TriangleMesh::Triangle(4, 5, 7)
    });
}

void generateGridCubePoints(std::vector<TriangleMesh::Point> &ps, clp::Path &square, double scaling, double zmin, double zmax) {
    double xmin = square[0].X * scaling;
    double ymin = square[0].Y * scaling;
    double xmax = square[2].X * scaling;
    double ymax = square[2].Y * scaling;
    ps.reserve(8);
    ps.emplace_back(xmin, ymin, zmin);
    ps.emplace_back(xmax, ymin, zmin);
    ps.emplace_back(xmin, ymax, zmin);
    ps.emplace_back(xmax, ymax, zmin);
    ps.emplace_back(xmin, ymin, zmax);
    ps.emplace_back(xmax, ymin, zmax);
    ps.emplace_back(xmin, ymax, zmax);
    ps.emplace_back(xmax, ymax, zmax);
}

Matrix<TriangleMesh> PathSplitter::generateGridCubes(double scaling, double zmin, double zmax) {
    Matrix<TriangleMesh> result(numx, numy);
    auto trs = generateGridCubeTriangles();
    auto output = result.data.begin();
    for (auto encl = buffer.data.begin(); encl != buffer.data.end(); ++encl, ++output) {
       output->triangles = trs;
       generateGridCubePoints(output->points, encl->originalSquare, scaling, zmin, zmax);
    }
    return result;
}

//helper method for processPaths()
void PathSplitter::applyMotionPlanning() {
    for (int x = 0; x < buffer.numx; ++x) {
        for (int y = 0; y < buffer.numy; ++y) {
            int idx = buffer.idx(x, y);
            if (!buffer.data[idx].paths.empty()) {
                verySimpleMotionPlanner(buffer.data[idx].motionPlanningState, PathOpen, buffer.data[idx].paths);
            }
        }
    }
}

//helper method for processPaths()
void PathSplitter::SquareState::reset() {
    no_lines = true;
    create_new = currentPointIsInside = previousPointIsInside = pointadded = false;
}

//helper method for processPaths()
bool PathSplitter::setupSquares(double z, double scaling) {
    clp::IntPoint shiftBecauseAngle;
    if ((z < config.zmin) && !angle90) {
        err = str("Error while splitting toolpaths: z=%g < zmin=%g (this is illegal because the configuration specifies a non-right angle)");
        return false;
    }
    if (!angle90) {
        clp::cInt shiftBecauseAngle_value = (clp::cInt) (sinangle * ((z - config.zmin) / scaling));
        if (shiftBecauseAngle_value >= config.displacement.X) {
            err = "Error while splitting toolpaths: either the angle is too big or the Z value is too far from the base (in X)";
            return false;
        }
        if (shiftBecauseAngle_value >= config.displacement.Y) {
            err = "Error while splitting toolpaths: either the angle is too big or the Z value is too far from the base (in Y)";
            return false;
        }
        shiftBecauseAngle.X = (singlex) ? 0 : shiftBecauseAngle_value;
        shiftBecauseAngle.Y = (singley) ? 0 : shiftBecauseAngle_value;
    }
    for (int x = 0; x < numx; ++x) {
        for (int y = 0; y < numy; ++y) {
            auto &enclosed = buffer.at(x, y);
            auto &square = enclosed.actualSquare;
            enclosed.paths.clear();
            square = enclosed.originalSquare;
            if (!angle90) {
                bool notfirstx = x > 0;
                bool notfirsty = y > 0;
                bool notlastx = x < (numx - 1);
                bool notlasty = y < (numy - 1);
                if (notfirstx) square[0].X -= shiftBecauseAngle.X;
                if (notfirsty) square[0].Y -= shiftBecauseAngle.Y;
                if (notlastx)  square[1].X -= shiftBecauseAngle.X;
                if (notfirsty) square[1].Y -= shiftBecauseAngle.Y;
                if (notlastx)  square[2].X -= shiftBecauseAngle.X;
                if (notlasty)  square[2].Y -= shiftBecauseAngle.Y;
                if (notfirstx) square[3].X -= shiftBecauseAngle.X;
                if (notlasty)  square[3].Y -= shiftBecauseAngle.Y;
            }
        }
    }
    return true;
}

//helper method for processPaths()
void PathSplitter::applyClipping(std::vector<clp::Paths> &toClip, bool pathsClosed) {
    clp::Paths tmp;
    for (int x = 0; x < buffer.numx; ++x) {
        for (int y = 0; y < buffer.numy; ++y) {
            int idx = buffer.idx(x, y);
            if (!toClip[idx].empty()) {
                auto &enclosed = buffer.at(x, y);
                clipPaths(res->clipper, enclosed.actualSquare, toClip[idx], pathsClosed, pt, tmp);
                if (!tmp.empty()) MOVETO(tmp, enclosed.paths);
                toClip[idx].clear();
            }
        }
    }
}

//helper method for processPaths()
void keepWithinBounds(clp::Paths &snappeds, int numx, int numy) {
    for (auto &positions : snappeds) {
        for (auto &position : positions) {
            if (position.X < 0) {
                position.X = 0;
                fprintf(stderr, "Warning: unexpected geometric condition (point was outside the splitting grid, branch (position->X < 0)\n");
            }
            if (position.Y < 0) {
                position.Y = 0;
                fprintf(stderr, "Warning: unexpected geometric condition (point was outside the splitting grid, branch (position->Y < 0)\n");
            }
            if (position.X >= numx) {
                position.X = numx - 1;
                fprintf(stderr, "Warning: unexpected geometric condition (point was outside the splitting grid, branch (position->X >= numx)\n");
            }
            if (position.Y >= numy) {
                position.Y = numy - 1;
                fprintf(stderr, "Warning: unexpected geometric condition (point was outside the splitting grid, branch (position->Y >= numy)\n");
            }
        }
    }

}

bool PathSplitter::processPaths(clp::Paths &paths, bool pathsClosed, double z, double scaling) {
    if (justone) {
        buffer.data[0].actualSquare = buffer.data[0].originalSquare;
        buffer.data[0].paths = paths;
        return true;
    }

    if (!setupSquares(z, scaling)) return false;

    if (pathsClosed) {

       //for closed paths, it is far easier to use full-blown clipping. Downside: very slow!!!
        clipPaths(res->clipper, pathsClosed, paths, buffer, pt);

    } else {

        //for open paths, clipping can be accelerated
        clp::Paths snappeds(paths);
        std::vector<clp::Paths> toClip(buffer.data.size());
        verySimpleGetSnapIndex(snappeds, snapspec);
        keepWithinBounds(snappeds, numx, numy);
        clp::IntPoint currentpoint, prevpoint, prevposition;
        clp::Path segment(2);
        bool no_segment_already_added = true;
        bool no_segment_is_going_to_be_added = true;
        clp::Path::iterator point, position;
        int nx = numx - 1;
        int ny = numy - 1;
        int minx, maxx, miny, maxy;

        //this logic is used more than once in the inner loops, so it is put here as a lambda
        auto define_range = [&minx, &maxx, &miny, &maxy, nx, ny](int mnx, int mxx, int mny, int mxy) {
            minx = std::max(std::min(mnx - 2, nx), 0);
            maxx = std::max(std::min(mxx + 2, nx), 0);
            miny = std::max(std::min(mny - 2, ny), 0);
            maxy = std::max(std::min(mxy + 2, ny), 0);
        };
        
        //this logic is used more than once in the inner loops, so it is put here as a lambda
        auto reset_line_keeping = [&no_segment_already_added, &no_segment_is_going_to_be_added, &toClip, &segment, &point, &position, this](int minx, int maxx, int miny, int maxy) {
            no_segment_already_added = true;
            no_segment_is_going_to_be_added = true;
            for (int x = minx; x <= maxx; ++x) {
                for (int y = miny; y <= maxy; ++y) {
                    int markidx = buffer.idx(x, y);
                    //add segment to be clipped, EXCEPT if it has been already added (to avoid duplications)
                    if (!states.data[markidx].pointadded) {
                        toClip[markidx].push_back(segment);
                    }
                }
            }
            //reset state in ALL squares
            auto state = states.data.begin();
            for (auto toclipp = toClip.begin(); toclipp != toClip.end(); ++state, ++toclipp) {
                state->reset();
            }
            //roll back iterators, because we must take into account the
            //current point again, as the initial point for a new line
            --point;
            --position;
        };

        //first, separate the easy cases, and store apart the lines that cross across boundaries
        auto path = paths.begin();
        for (auto snapped = snappeds.begin(); snapped != snappeds.end(); ++snapped, ++path) {
            point = path->begin();
            for (auto &state : states.data) state.reset();
            no_segment_already_added = true;
            no_segment_is_going_to_be_added = true;
            for (position = snapped->begin(); position != snapped->end(); ++position, ++point) {
                /*this is somewhat complex: we must sweep for squares where current and previous points might reside.
                If no square boundaries are crossed, we can add the lines to several squares concurrently.
                However, as soon as there is any crossing, we bail out and mark the segment to be clipped in all nerby squares*/
                currentpoint = *point;
                if (no_segment_is_going_to_be_added) {
                    define_range((int)position->X,
                                 (int)position->X,
                                 (int)position->Y,
                                 (int)position->Y);
                } else {
                    segment[0] = prevpoint;
                    segment[1] = currentpoint;
                    define_range((int)std::min(position->X, prevposition.X),
                                 (int)std::max(position->X, prevposition.X),
                                 (int)std::min(position->Y, prevposition.Y),
                                 (int)std::max(position->Y, prevposition.Y));
                }
                for (int x = minx; x <= maxx; ++x) {
                    for (int y = miny; y <= maxy; ++y) {
                        //for each reachable square:
                        int currentidx = buffer.idx(x, y);
                        auto &sqstate = states.data[currentidx];
                        sqstate.currentPointIsInside = testInsideSquare(currentpoint, buffer.data[currentidx].actualSquare);

                        if (sqstate.currentPointIsInside) {
                            if (sqstate.no_lines) {
                                //segments can be primed only if no other segment has already been created.
                                //rationale: if a segment lives in the margin shared by several squares, we want to add it to all of them at once,
                                //           but we want to reset everything if lines were already being added in other places
                                if (no_segment_already_added) {
                                    no_segment_is_going_to_be_added = false;
                                    sqstate.no_lines = false; 
                                    sqstate.create_new = true;
                                } else {
                                    reset_line_keeping(minx, maxx, miny, maxy); //mark the segment to be clipped for all squares
                                    goto end_of_position_loop; //ATTENTION: GOTO USED TO EXIT NESTED LOOPS (STATE IS CLEAN)
                                }
                            } else {
                                //the logic prevents this branch from being run if no_segment_already_added is true (when segment is undefined)
                                if (sqstate.previousPointIsInside) {
                                    if (sqstate.create_new) {
                                        //initial action: add prev and current point
                                        buffer.data[currentidx].paths.push_back(segment);
                                        sqstate.create_new = false;
                                    } else {
                                        //subsquent actions: add the point
                                        buffer.data[currentidx].paths.back().push_back(currentpoint);
                                    }
                                    sqstate.pointadded = true; //mark this segment as added for this square (to avoid duplications if reset_line_keeping() is triggered)
                                    no_segment_already_added = false; //signal that we cannot start new segments
                                } else {
                                    reset_line_keeping(minx, maxx, miny, maxy); //mark the segment to be clipped for all squares
                                    goto end_of_position_loop; //ATTENTION: GOTO USED TO EXIT NESTED LOOPS (STATE IS CLEAN)
                                }
                            }
                        } else {
                            //this is not strictly necessary (the reset_line_keeping() will eventually be
                            //triggered in the other IF branch), but let's have it for completeness
                            if (sqstate.previousPointIsInside) {
                                reset_line_keeping(minx, maxx, miny, maxy); //mark the segment to be clipped for all squares
                                goto end_of_position_loop; //ATTENTION: GOTO USED TO EXIT NESTED LOOPS (STATE IS CLEAN)
                            }
                        }
                    }
                }
                //update squares' states
                for (auto &state : states.data) {
                    state.previousPointIsInside = state.currentPointIsInside;
                    state.pointadded = false;
                }
                //set previous point state
                prevpoint    = *point;
                prevposition = *position;
                //no need to update square states nor set previous point state if we come from one of the GOTOs,
                //since the square states have been reset and the previous point will not be used
            end_of_position_loop:
                ;
            }
        }

        //now, clip the lines that go across boundaries
        applyClipping(toClip, pathsClosed);

    }

    //clipping messes with path ordering, so reapply motionPlanning
    if (config.applyMotionPlanning) applyMotionPlanning();

    return true;
}

