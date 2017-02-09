#include "pathsplitter.hpp"
#include "motionPlanner.hpp"
#include "showcontours.hpp"
#include <cmath>

void clipPaths(clp::Clipper &clipper, clp::Path &clip, clp::Paths &subject, bool subjectClosed, clp::Paths &result) {
    clipper.AddPath(clip, clp::ptClip, true);
    clipper.AddPaths(subject, clp::ptSubject, subjectClosed);
    if (subjectClosed) {
        clipper.Execute(clp::ctIntersection, result, clp::pftNonZero, clp::pftNonZero);
    } else {
        clp::PolyTree *intermediate;
        clipper.Execute(clp::ctIntersection, intermediate, clp::pftNonZero, clp::pftNonZero);
        OpenPathsFromPolyTree(*intermediate, result);
    }
    clipper.Clear();
}

void clipPaths(clp::Clipper &clipper, bool pathsClosed, clp::Paths &paths, Matrix<PathSplitter::EnclosedPaths> &buffer) {
    for (int x = 0; x < buffer.numx; ++x) {
        for (int y = 0; y < buffer.numy; ++y) {
            auto &enclosed = buffer.at(x, y);
            clipPaths(clipper, enclosed.actualSquare, paths, pathsClosed, enclosed.paths);
        }
    }
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

bool PathSplitter::processPaths(clp::Paths &paths, bool pathsClosed, double z, double scaling) {
    if (justone) {
        buffer.data[0].actualSquare = buffer.data[0].originalSquare;
        buffer.data[0].paths = paths;
        return true;
    }

    if (!setupSquares(z, scaling)) return false;

    clipPaths(res->clipper, pathsClosed, paths, buffer);

    //clipping messes with path ordering, so reapply motionPlanning
    if (config.applyMotionPlanning) applyMotionPlanning();

    return true;
}

