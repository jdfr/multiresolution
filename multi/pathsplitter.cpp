#include "pathsplitter.hpp"
#include "config.hpp"
#include <cmath>

#define M_PI 3.14159265358979323846

bool PathSplitter::setup() {
    if (setup_done) return true;
    buffer.clear();
    clp::cInt sqdmin = -config.margin;
    clp::cInt sqdmax = config.displacement + config.margin;
    double numstepsMinX = ((double)(config.min.X - config.origin.X)) / config.displacement;
    double numstepsMinY = ((double)(config.min.Y - config.origin.Y)) / config.displacement;
    double numstepsMaxX = ((double)(config.max.X - config.origin.X)) / config.displacement;
    double numstepsMaxY = ((double)(config.max.Y - config.origin.Y)) / config.displacement;
    int sqminx = (int)std::floor(numstepsMinX);
    int sqminy = (int)std::floor(numstepsMinY);
    int sqmaxx = (int)std::ceil(numstepsMaxX);
    int sqmaxy = (int)std::ceil(numstepsMaxY);
    numx = sqmaxx - sqminx;
    numy = sqmaxy - sqminy;
    buffer.resize(numx, std::vector<EnclosedPaths>(numy));
    for (int x = 0; x < numx; ++x) {
        for (int y = 0; y < numy; ++y) {
            clp::Path &square = buffer[x][y].originalSquare;
            square.reserve(4);
            clp::cInt shiftx = ((clp::cInt)(x + sqminx)) * config.displacement + config.origin.X;
            clp::cInt shifty = ((clp::cInt)(y + sqminy)) * config.displacement + config.origin.Y;
            square.emplace_back(shiftx + sqdmin, shifty + sqdmin);
            square.emplace_back(shiftx + sqdmax, shifty + sqdmin);
            square.emplace_back(shiftx + sqdmax, shifty + sqdmax);
            square.emplace_back(shiftx + sqdmin, shifty + sqdmax);
        }
    }
    singlex = numx == 1;
    singley = numy == 1;
    angle90 = (singlex && singley) || (std::abs(config.wallAngle - 90.0) < 1e-6);
    sinangle = std::sin(config.wallAngle * M_PI / 180.0);
    setup_done = true;
    return true;
}

bool PathSplitter::processPaths(clp::Paths &paths, bool pathsClosed, double z, double scaling) {
    if ((z < config.zmin) && !angle90) {
        err = str("Error while splitting toolpaths: z=%g < zmin=%g (this is illegal because the configuration specifies a non-right angle)");
        return false;
    }
    clp::PolyTree pt;
    clp::IntPoint shiftBecauseAngle;
    if (!angle90) {
        clp::cInt shiftBecauseAngle_value = (clp::cInt) (sinangle * ((z - config.zmin) / scaling));
        if (shiftBecauseAngle_value >= config.displacement) {
            err = "Error while splitting toolpaths: either the angle is too big or the Z value is too far from the base";
            return false;
        }
        shiftBecauseAngle.X = (singlex) ? 0 : shiftBecauseAngle_value;
        shiftBecauseAngle.Y = (singley) ? 0 : shiftBecauseAngle_value;
    }
    for (int x = 0; x < numx; ++x) {
        for (int y = 0; y < numy; ++y) {
            auto &enclosed = buffer[x][y];
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
            clipper.AddPath(square, clp::ptClip, true);
            clipper.AddPaths(paths, clp::ptSubject, pathsClosed);
            if (pathsClosed) {
                clipper.Execute(clp::ctIntersection, enclosed.paths, clp::pftNonZero, clp::pftNonZero);
                clipper.Clear();
            } else {
                clipper.Execute(clp::ctIntersection, pt, clp::pftNonZero, clp::pftNonZero);
                clipper.Clear();
                OpenPathsFromPolyTree(pt, enclosed.paths);
                pt.Clear();
            }
        }
    }
    return true;
}
