#include "pathsplitter.hpp"
#include "showcontours.hpp"
#include <cmath>

#define M_PI 3.14159265358979323846

bool PathSplitter::setup() {
    if (setup_done) return true;
    buffer.clear();
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
        int sqminx          = (int)std::floor(numstepsMinX);
        int sqminy          = (int)std::floor(numstepsMinY);
        int sqmaxx          = (int)std::ceil(numstepsMaxX);
        int sqmaxy          = (int)std::ceil(numstepsMaxY);
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
        double dispX      = sizeX / (double)numx;
        double dispY      = sizeY / (double)numy;
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
    if (spec != NULL) {
        clp::Paths squares;
        for (auto &encl : buffer.data) {
            squares.push_back(encl.originalSquare);
            squares.back().push_back(squares.back().front());
        }
        SHOWCONTOURS(*spec->global.config, "PathSplitter squares", &squares);
    }
    singlex    = numx == 1;
    singley    = numy == 1;
    angle90    = (singlex && singley) || (std::abs(config.wallAngle - 90.0) < 1e-6);
    sinangle   = std::sin(config.wallAngle * M_PI / 180.0);
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
            auto &enclosed = buffer.at(x,y);
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
            clipPaths(clipper, square, paths, pathsClosed, pt, enclosed.paths);
        }
    }
    return true;
}

