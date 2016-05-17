#ifndef PATHSPLITTER_HEADER
#define PATHSPLITTER_HEADER

#include "multislicer.hpp"

/*to write an object in a machine that works in chunks (i.e. nanoscribe),
we must divide it in small blocks. The blocks are cuboids, placed in a
checkerboard pattern, such that the walls are angled*/
typedef struct PathSplitterConfig {
    clp::IntPoint origin;         //origin of coordinates for the checkerboard pattern (marks the bottom-left corner of the first square)
    clp::IntPoint displacement;   //size of each chekerboard square
    clp::cInt margin;             //this is added to each square, so the window is a square of size displacement+2*margin
    clp::IntPoint min, max;       //min/max XY values (to set up the grid)
    double zmin;                  //min Z value (to set the groud)
    double wallAngle;             //angle of the walls with respect to the normal, in degrees
    bool useOrigin;               //if true, the checkerboard pattern is rigid. If false, distribute the space defined by the min/max values evenly among squares, with an effective displacement possibly lower than specified
    bool applyMotionPlanning;
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
    void assign(T d) { data.assign(data.size(), std::move(d)); }
    int idx(int x, int y) { return x*numy + y; }
    T& at(int x, int y) { return data[idx(x, y)]; }
};

typedef std::vector<PathSplitterConfig> PathSplitterConfigs;

class PathSplitter {
public:
    typedef struct EnclosedPaths {
        clp::Paths paths;         //this is generated in processPaths(): it can be manipulated by callers
        clp::Path actualSquare;   //this is generated in processPaths(): it can be manipulated by callers
        clp::Path originalSquare; //this is generated in setup():: it should not be overwritten by callers
        StartState motionPlanningState;
    } EnclosedPaths;
    clp::IntPoint originalSize;
    std::string err;
    Matrix<EnclosedPaths> buffer;
    std::shared_ptr<ClippingResources> res;
    int numx, numy; //matrix sizes
    bool angle90;
    PathSplitterConfig config;
    PathSplitter(PathSplitterConfig _config, std::shared_ptr<ClippingResources> _res, Configuration *_cfg = NULL) : res(std::move(_res)), config(std::move(_config)), setup_done(false), cfg(_cfg) {}
    bool setup();
    Matrix<TriangleMesh> generateGridCubes(double scaling, double zmin, double zmax);
    bool processPaths(clp::Paths &paths, bool pathsClosed, double z, double scaling);
protected:
    void applyMotionPlanning();
    bool setupSquares(double z, double scaling);
    void applyClipping(std::vector<clp::Paths> &toClip, bool pathsClosed);
    typedef struct SquareState {
        bool no_lines;              //this flag means that no line is intended to be added in this square
        bool create_new;            //this flag means that a new line must be created
        bool currentPointIsInside;  //this flag is a buffer whose content is transfered every loop to the flag previousPointIsInside
        bool previousPointIsInside; //this flag means that the previous point was inside this square
        bool pointadded;            //this flag means that no point has been added to this square during this loop
        void reset();               //default state: all flags are false, except no_lines, which is true
    } SquareState;
    Matrix<SquareState> states;
    Configuration *cfg;
    SnapToGridSpec snapspec;
    clp::PolyTree pt;
    double sinangle;
    bool setup_done;
    bool singlex, singley, justone;
};

#endif