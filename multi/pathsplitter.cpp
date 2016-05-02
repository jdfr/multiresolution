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

void clipPaths(clp::Clipper &clipper, bool pathsClosed, clp::Paths &paths, Matrix<PathSplitter::EnclosedPaths> &buffer) {
    clp::PolyTree pt;
    for (int x = 0; x < buffer.numx; ++x) {
        for (int y = 0; y < buffer.numy; ++y) {
            auto &enclosed = buffer.at(x, y);
            clipPaths(clipper, enclosed.actualSquare, paths, pathsClosed, pt, enclosed.paths);
        }
    }
}

bool intersectSegmentAndHLine(const clp::IntPoint a, const clp::IntPoint ab, const clp::cInt abminy, const clp::cInt abmaxy, clp::cInt y, const clp::cInt minxl, const clp::cInt maxxl, clp::IntPoint &result) {

    if (ab.Y == 0) return false;

    if (!((y >= abminy) && (y <= abmaxy))) return false;

    result.Y = y;
    result.X = a.X + ab.X*(y - a.Y) / (ab.Y);

    return (result.X >= minxl) && (result.X <= maxxl);
}

bool intersectSegmentAndVLine(const clp::IntPoint a, const clp::IntPoint ab, const clp::cInt abminx, const clp::cInt abmaxx, clp::cInt x, const clp::cInt minyl, const clp::cInt maxyl, clp::IntPoint &result) {

    if (ab.X == 0) return false;

    if (!((x >= abminx) && (x <= abmaxx))) return false;

    result.X = x;
    result.Y = a.Y + ab.Y*(x - a.X) / (ab.X);

    return (result.Y >= minyl) && (result.Y <= maxyl);
}

bool intersectSegmentAndSquare(const clp::IntPoint a, const clp::IntPoint b, const clp::Path &square, clp::IntPoint &result) {
    /*square layout from the splitter:

        3****2
        *    *
        *    *
        *    *
        0****1
    */

    clp::IntPoint ab(b.X - a.X, b.Y - a.Y);

    clp::cInt abminx = std::min(a.X, b.X);
    clp::cInt abmaxx = std::max(a.X, b.X);
    if (intersectSegmentAndVLine(a, ab, abminx, abmaxx, square[1].X, square[1].Y, square[2].Y, result))
        return true;
    if (intersectSegmentAndVLine(a, ab, abminx, abmaxx, square[3].X, square[0].Y, square[3].Y, result))
        return true;

    clp::cInt abminy = std::min(a.Y, b.Y);
    clp::cInt abmaxy = std::max(a.Y, b.Y);
    if (intersectSegmentAndHLine(a, ab, abminy, abmaxy, square[0].Y, square[0].X, square[1].X, result))
        return true;
    if (intersectSegmentAndHLine(a, ab, abminy, abmaxy, square[2].Y, square[3].X, square[2].X, result))
        return true;
    return false;
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
        for (int x = 0; x < numx; ++x) {
                clp::cInt shiftx  = ((clp::cInt)(x + sqminx)) * config.displacement.X + config.origin.X;
            for (int y = 0; y < numy; ++y) {
                clp::cInt shifty  = ((clp::cInt)(y + sqminy)) * config.displacement.Y + config.origin.Y;
                clp::Path &square = buffer.at(x,y).originalSquare;
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
        for (int x = 0; x < numx; ++x) {
                clp::cInt shiftx  = config.min.X + (clp::cInt)(x*dispX);
            for (int y = 0; y < numy; ++y) {
                clp::cInt shifty  = config.min.Y + (clp::cInt)(y*dispY);
                clp::Path &square = buffer.at(x, y).originalSquare;
                square.reserve(4);
                square.emplace_back(shiftx + sqdmin,  shifty + sqdmin);
                square.emplace_back(shiftx + sqdmaxX, shifty + sqdmin);
                square.emplace_back(shiftx + sqdmaxX, shifty + sqdmaxY);
                square.emplace_back(shiftx + sqdmin,  shifty + sqdmaxY);
            }
        }
    }
    insideSquare.reset(numx, numy);
    positionsToTest.reserve(5 * 5);
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
    simpler_snap = angle90 && (config.margin == 0);
    setup_done   = true;
    return true;
}

bool PathSplitter::processPaths(clp::Paths &paths, bool pathsClosed, double z, double scaling) {
    if ((z < config.zmin) && !angle90) {
        err = str("Error while splitting toolpaths: z=%g < zmin=%g (this is illegal because the configuration specifies a non-right angle)");
        return false;
    }
    if (justone) {
        buffer.data[0].paths = paths;
        return true;
    }
    clp::IntPoint shiftBecauseAngle;
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

    if (pathsClosed) {
       //for closed paths, we need to use full-blown clipping. Downside: very slow!!!
        clipPaths(res->clipper, pathsClosed, paths, buffer);
    } else {
        //for open paths, clipping can be greatly accelerated
        clp::Paths snappeds(paths);
        clp::IntPoint previousPosition;
        verySimpleGetSnapIndex(snappeds, snapspec);
        clp::IntPoint currentposition;
        auto path       = paths.begin();
        for (auto snapped = snappeds.begin(); snapped != snappeds.end(); ++snapped, ++path) {
            auto point = path->begin();
            bool start_line = true;
            insideSquare.assign(false);
            bool simpler_snap_valid = simpler_snap;
            for (auto position = snapped->begin(); position != snapped->end(); ++position, ++point) {
                if (position->X < 0) {
                    position->X = 0;
                    fprintf(stderr, "Warning: unexpected geometric condition (point was outside the splitting grid, branch (position->X < 0)\n");
                }
                if (position->Y < 0) {
                    position->Y = 0;
                    fprintf(stderr, "Warning: unexpected geometric condition (point was outside the splitting grid, branch (position->Y < 0)\n");
                }
                if (position->X >= numx) {
                    position->X = numx - 1;
                    fprintf(stderr, "Warning: unexpected geometric condition (point was outside the splitting grid, branch (position->X >= numx)\n");
                }
                if (position->Y >= numy) {
                    position->Y = numy - 1;
                    fprintf(stderr, "Warning: unexpected geometric condition (point was outside the splitting grid, branch (position->Y >= numy)\n");
                }
                simpler_snap_valid = simpler_snap_valid && testInsideSquare(*point, buffer.at((int)position->X, (int)position->Y).actualSquare);
                positionsToTest.clear();
                if (simpler_snap_valid) {
                    //in this case, there can be only one current position, and possibly a previous position
                    positionsToTest.emplace_back((int)position->X, (int)position->Y);
                    if (!start_line && (previousPosition != *position)) {
                        positionsToTest.emplace_back((int)previousPosition.X, (int)previousPosition.Y);
                    }
                    previousPosition = *position;
                } else {
                    //in this case, we must sweep all nearby squares to make sure we do the partitioning correctly
                    int minx, maxx, miny, maxy;
                    minx = (int)position->X - 2;
                    maxx = (int)position->X + 2;
                    miny = (int)position->Y - 2;
                    maxy = (int)position->Y + 2;
                    for (int x = minx; x <= maxx; ++x) {
                        if (x < 0)     continue;
                        if (x >= numx) continue;
                        for (int y = miny; y <= maxy; ++y) {
                            if (y < 0)     continue;
                            if (y >= numy) continue;
                            positionsToTest.emplace_back(x, y);
                        }
                    }
                }
                /*//test all squares
                positionsToTest.clear();
                for (int x = 0; x < numx; ++x) {
                    for (int y = 0; y < numy; ++y) {
                        positionsToTest.emplace_back(x, y);
                    }
                }*/
                for (auto &positionToTest : positionsToTest) {
                    int x                  = (int)positionToTest.first;
                    int y                  = (int)positionToTest.second;
                    auto &enclosed         = buffer.at(x, y);
                    //if simpler_snap_valid is set (meaning that margin==0 and the point was really inside the square), the condition currentlyInside can be simplified
                    bool currentlyInside   = simpler_snap_valid ? ((x == position->X) && (y == position->Y)) :
                                                                  testInsideSquare(*point, enclosed.actualSquare);
                    char &previouslyInside = insideSquare.at(x, y);
                    if (start_line) {
                        //first time, inconditionally initialize grid elements if the point is inside themw
                        if (currentlyInside) {
                            enclosed.paths.emplace_back(1, *point);
                            previouslyInside = true;
                        }
                    } else {
                        if (currentlyInside) {
                            if (previouslyInside) {
                                //point is inside, and there were previous points: add point to square
                                enclosed.paths.back().push_back(*point);
                            } else {
                                clp::IntPoint p;
                                if (intersectSegmentAndSquare(*(point - 1), *point, enclosed.actualSquare, p)) {
                                    /*clp::Paths seg1(1, clp::Path(2)); seg1.back()[0] = p; seg1.back()[1] = *point;
                                    clp::Paths seg2(1, clp::Path(2)); seg2.back()[0] = *(point - 1); seg2.back()[1] = p;
                                    clp::Paths sq(1, enclosed.actualSquare); sq.back().push_back(sq.back().front());
                                    SHOWCONTOURS(*cfg, "currentlyInside && !previouslyInside", &sq, &enclosed.paths, &seg1, &seg2);*/
                                    //first point inside this square: initialize with the intersection, then add the point
                                    enclosed.paths.emplace_back(1, p);
                                    enclosed.paths.back().push_back(*point);
                                    previouslyInside = true;
                                } else {
                                    //the intersection could not be computed: this should never happen.
                                    //fprintf(stderr, "Warning: unexpected geometric constraint violation in intersectSegmentAndSquare in branch (currentlyInside && !previouslyInside): could not compute intersection between segment and square\n");
                                  
                                    //this state may be reached sometimes, but we do not have time to debug it. Abort current operation and trade speed of execution for speed of development...
                                    clipPaths(res->clipper, pathsClosed, paths, buffer);
                                    return true;
                                }
                            }
                        } else {
                            if (previouslyInside) {
                                //point is not inside, but the square contained points: find intersection point and add it
                                previouslyInside = false;
                                clp::IntPoint p;
                                if (intersectSegmentAndSquare(*(point - 1), *point, enclosed.actualSquare, p)) {
                                    /*clp::Paths seg1(1, clp::Path(2)); seg1.back()[0] = enclosed.paths.back().back(); seg1.back()[1] = p;
                                    clp::Paths seg2(1, clp::Path(2)); seg2.back()[0] = p; seg2.back()[1] = *point;
                                    clp::Paths sq(1, enclosed.actualSquare); sq.back().push_back(sq.back().front());
                                    SHOWCONTOURS(*cfg, "!currentlyInside && previouslyInside", &sq, &enclosed.paths, &seg1, &seg2);*/
                                    enclosed.paths.back().push_back(p);
                                } else {
                                    //the intersection could not be computed: this should never happen.
                                    //fprintf(stderr, "Warning: unexpected geometric constraint violation in intersectSegmentAndSquare in branch (!currentlyInside && previouslyInside): could not compute intersection between segment and square\n");
                                  
                                    //this state may be reached sometimes, but we do not have time to debug it. Abort current operation and trade speed of execution for speed of development...
                                    clipPaths(res->clipper, pathsClosed, paths, buffer);
                                    return true;
                                }
                            } else {
                                //point is not inside, and the square contained no points: do nothing
                            }
                        }
                    }
                }
                start_line = false;
            }
        }
    }
    return true;
}

