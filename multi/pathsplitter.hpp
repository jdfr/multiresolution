#ifndef PATHSPLITTER_HEADER
#define PATHSPLITTER_HEADER

#include "spec.hpp"

/*to write an object in a machine that works in chunks (i.e. nanoscribe),
we must divide it in small blocks. The blocks are cuboids, placed in a
checkerboard pattern, such that the walls are angled*/
typedef struct PathSplitterConfig {
    bool useOrigin;               //if true, the checkerboard pattern is rigid. If false, distribute the space defined by the min/max values evenly among squares, with an effective displacement possibly lower than specified
    clp::IntPoint origin;         //origin of coordinates for the checkerboard pattern (marks the bottom-left corner of the first square)
    clp::IntPoint displacement;   //size of each chekerboard square
    clp::cInt margin;             //this is added to each square, so the window is a square of size displacement+2*margin
    clp::IntPoint min, max;       //min/max XY values (to set up the grid)
    double zmin;                  //min Z value (to set the groud)
    double wallAngle;             //angle of the walls with respect to the normal, in degrees
} PathSplitterConfig;

//wrapper to use a vector as a matrix
template<typename T> class Matrix {
public:
    std::vector<T> data;
    int numx, numy;
    Matrix() : numx(0), numy(0) {}
    Matrix(int _numx, int _numy) : numx(_numx), numy(_numy), data(_numx*_numy) {}
    void clear() { numx = numy = 0; data.clear(); }
    void reset(int _numx, int _numy) { numx = _numx; numy = _numy; data.clear(); data.resize(numx*numy); }
    T& at(int x, int y) { return data[x*numy + y]; }
};

typedef std::vector<PathSplitterConfig> PathSplitterConfigs;

class PathSplitter {
public:
    typedef struct EnclosedPaths {
        clp::Paths paths;         //this is generated in processPaths(): it can be manipulated by callers
        clp::Path actualSquare;   //this is generated in processPaths(): it can be manipulated by callers
        clp::Path originalSquare; //this is generated in setup():: it should not be overwritten by callers
    } EnclosedPaths;
    std::string err;
    Matrix<EnclosedPaths> buffer;
    int numx, numy; //matrix sizes
    PathSplitterConfig config;
    PathSplitter(PathSplitterConfig _config, MultiSpec *_spec = NULL) : config(std::move(_config)), setup_done(false), spec(_spec) {}
    bool setup();
    bool processPaths(clp::Paths &paths, bool pathsClosed, double z, double scaling);
protected:
    MultiSpec *spec;
    clp::Clipper clipper;
    double sinangle;
    bool angle90;
    bool setup_done;
    bool singlex, singley;
};

inline void clipPaths(clp::Clipper &clipper, clp::Path &clip, clp::Paths &subject, bool subjectClosed, clp::PolyTree &intermediate, clp::Paths &result) {
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
}

#endif