#ifndef AUXGEOM_HEADER
#define AUXGEOM_HEADER

#include "common.hpp"
#include <iterator>

#if defined(_WIN32) || defined(_WIN64)
#else
#  include <limits.h>
#endif

#define M_PI 3.14159265358979323846

std::string handleClipperException(clp::clipperException &e);

void printClipperPaths(clp::Paths &paths, const char * name, FILE* f);

template<typename T> inline void MOVETO(T &source, std::vector<T> &dest) {
    dest.push_back(std::move(source));
};

template<typename T> inline void MOVETO(std::vector<std::vector<T>> &source, std::vector<T> &dest) {
    for (auto s = source.begin(); s != source.end(); ++s) {
        std::move(s->begin(), s->end(), std::back_inserter(dest));
    }
};

template<typename T> inline void MOVETO(std::vector<T> &source, std::vector<T> &dest) {
    std::move(source.begin(), source.end(), std::back_inserter(dest));
};

template<typename T> inline void COPYTO(std::vector<T> &source, std::vector<T> &dest) {
    dest.insert(dest.end(), source.begin(), source.end());
};

template<typename T> inline void COPYTO(std::vector<std::vector<T>> &source, std::vector<T> &dest) {
    for (auto s = source.begin(); s != source.end(); ++s) {
        dest.insert(dest.end(), s->begin(), s->end());
    }
};


typedef struct Segment {
    clp::IntPoint a;
    clp::IntPoint b;
    Segment(clp::IntPoint _a, clp::IntPoint _b) : a(_a), b(_b) {}
    double length() const;
    double orientation() const;
} Segment;

double length(clp::Path &path);
clp::IntPoint point_in_vector(clp::IntPoint &origin, clp::IntPoint &dest, double distance);

typedef std::vector<Segment> Segments;

double distancePS(clp::IntPoint &p, Segment &s);

double distance_to(const clp::IntPoint &point1, const clp::IntPoint &point2);

enum PathMode {openMode, closedMode};

void addPathToSegments(clp::Path &path, Segments &segments, PathMode mode);
void addPathsToSegments(clp::Paths &paths, Segments &segments, PathMode mode);

inline void copyOpenToClosedPath(clp::Path &input, clp::Path &output) {
    if (input.size() > 0) {
        output.reserve(output.size() + input.size() + 1);
        COPYTO(input, output);
        output.push_back(input.front());
    }
}

inline void moveOpenToClosedPath(clp::Path &input, clp::Path &output) {
    if (input.size() > 0) {
        size_t pos = output.size();
        output.reserve(pos + input.size() + 1);
        MOVETO(input, output);
        output.push_back(output[pos]);
    }
}

typedef void(*PathFun)(clp::Path &, clp::Path &);

enum VectorOption {ReserveCapacity, AmortizedCost};

template<PathFun pathfun, VectorOption opt> inline void applyToPaths(clp::Paths &input, clp::Paths &output) {
    if (opt == ReserveCapacity) {
        output.reserve(output.size() + input.size());
    }
    for (auto path = input.begin(); path != input.end(); ++path) {
        output.push_back(clp::Path());
        pathfun(*path, output.back());
    }
}

struct HoledPolygon;
typedef std::vector<HoledPolygon> HoledPolygons;

//for some tasks, it is easier to compute in terms of HoledPolygons instead of clp::PolyTree or clp::Paths
typedef struct HoledPolygon {
    clp::Path contour;
    clp::Paths holes;

    void addToSegments(Segments &segments);
    void clipPaths(clp::Clipper &clipper, clp::Paths &paths);
    template<typename T> void offset(clp::ClipperOffset &offset, double radius, T &result);
    void offset(clp::ClipperOffset &offset, double radius, HoledPolygons &result);
    void offset2(clp::ClipperOffset &offset, double radius1, double radius2, HoledPolygons &result);
    inline void moveToPaths(clp::Paths &paths);
    inline void copyToPaths(clp::Paths &paths);
} HoledPolygon;

inline void HoledPolygon::moveToPaths(clp::Paths &paths) {
    paths.reserve(paths.size() + this->holes.size() + 1);
    paths.push_back(std::move(this->contour));
    MOVETO(this->holes, paths);
}

inline void HoledPolygon::copyToPaths(clp::Paths &paths) {
    paths.push_back(this->contour);
    COPYTO(holes, paths);
}

template<typename T> void HoledPolygon::offset(clp::ClipperOffset &offset, double radius, T &result) {
    offset.AddPath(this->contour, clp::jtRound, clp::etClosedPolygon);
    offset.AddPaths(this->holes, clp::jtRound, clp::etClosedPolygon);
    offset.Execute(result, radius);
    offset.Clear();
    if (!std::is_same<T, clp::PolyTree>::value) ClipperEndOperation(offset);
}


void AddPathsToHPs(clp::Clipper &clipper, clp::Paths &pt, HoledPolygons &hps);
void AddPolyTreeToHPs(clp::PolyTree &pt, HoledPolygons &hps);

void AddHPsToPaths(HoledPolygons &hps, clp::Paths &paths);
void AddHPsToClosedPaths(HoledPolygons &hps, clp::Paths &paths);


typedef struct Transformation {
    bool doit, usescale;
    clp::cInt dx, dy;
    double scale;
    double invscale;
    Transformation() : doit(false) {}
    Transformation(clp::cInt _dx, clp::cInt _dy, double s = 1.0) : doit(true), usescale(s!=1.0), dx(_dx), dy(_dy), scale(s), invscale(1/s) {}
    inline bool notTrivial() { return (dx != 0) || (dy != 0) || (scale != 1.0); }
} Transformation;

void applyTransform(Transformation &t, clp::Path &path);
void reverseTransform(Transformation &t, clp::Path &path);
typedef void(*DoTransform)(Transformation &, clp::Path &);
template<DoTransform fun> void transformAllPaths(Transformation &t, HoledPolygon &hp) {
    fun(t, hp.contour);
    for (clp::Paths::iterator path = hp.holes.begin(); path != hp.holes.end(); ++path) {
        fun(t, *path);
    }
}

typedef struct Point3D {
    double x, y, z;
    Point3D() = default;
    Point3D(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}
} Point3D;

typedef std::vector<Point3D> Path3D;
typedef std::vector<Path3D> Paths3D;

//const int mlen = 16; //for true matrix
const int TransformationMatrixLength = 12; //do not care about the last row of the transformation matrix
//the matrix is specified row-wise
typedef double TransformationMatrix[TransformationMatrixLength];

inline bool transformationIs2DCOmpatible(TransformationMatrix matrix) {
    return ((matrix[2] == 0.0) && (matrix[6] == 0.0) && (matrix[8] == 0.0) && (matrix[9] == 0.0));
}

inline bool transformationSurelyIsAffine(TransformationMatrix matrix) {
    return (TransformationMatrixLength == 16) && ((matrix[12] != 0.0) || (matrix[13] != 0.0) || (matrix[14] != 0.0) || (matrix[15] != 1.0));
}

inline void applyTransform2DCompatibleXY(clp::DoublePoint &p, TransformationMatrix matrix) {
    double x = (matrix[0] * p.X) + (matrix[1] * p.Y) + matrix[3];
    double y = (matrix[4] * p.X) + (matrix[5] * p.Y) + matrix[7];
    p.X = x;
    p.Y = y;
}

inline double applyTransform2DCompatibleZ(double z, TransformationMatrix matrix) {
    return z*matrix[10] + matrix[11];
}

inline bool transform2DIsIdentityInXY(TransformationMatrix matrix) {
    return (matrix[0] == 1.0) && (matrix[1] == 0.0) && (matrix[3] == 0.0) && (matrix[4] == 0.0) && (matrix[5] == 1.0) && (matrix[7] == 0.0);
}

inline bool transform2DIsIdentityInZ(TransformationMatrix matrix) {
    return (matrix[10] == 1.0) && (matrix[11] == 0.0);
}

inline Point3D applyTransformFull3D(clp::DoublePoint &p, double iz, TransformationMatrix matrix) {
    double x = (matrix[0] * p.X) + (matrix[1] * p.Y) + (matrix[2] * iz) + matrix[3];
    double y = (matrix[4] * p.X) + (matrix[5] * p.Y) + (matrix[6] * iz) + matrix[7];
    double z = (matrix[8] * p.X) + (matrix[9] * p.Y) + (matrix[10] * iz) + matrix[11];
    return Point3D(x, y, z);
}

inline void applyTransformFull3D(Point3D &p, TransformationMatrix matrix) {
    double x = (matrix[0] * p.x) + (matrix[1] * p.y) + (matrix[2] * p.z) + matrix[3];
    double y = (matrix[4] * p.x) + (matrix[5] * p.y) + (matrix[6] * p.z) + matrix[7];
    double z = (matrix[8] * p.x) + (matrix[9] * p.y) + (matrix[10] * p.z) + matrix[11];
    p.x = x;
    p.y = y;
    p.z = z;
}


typedef struct BBox {
    clp::cInt minx;
    clp::cInt maxx;
    clp::cInt miny;
    clp::cInt maxy;
    BBox(clp::cInt _minx = 0, clp::cInt _maxx = 0, clp::cInt _miny = 0, clp::cInt _maxy = 0) : minx(_minx), maxx(_maxx), miny(_miny), maxy(_maxy) {}
    void merge(BBox &second) {
        minx = std::min(minx, second.minx);
        miny = std::min(miny, second.miny);
        maxx = std::max(maxx, second.maxx);
        maxy = std::max(maxy, second.maxy);
    }
    Transformation fitToInt32();
} BBox;
BBox getBB(clp::Path &path);
BBox getBB(clp::Paths &paths);
BBox getBB(HoledPolygon &hp);
BBox getBB(HoledPolygons &hps);

typedef struct TriangleMesh {
  struct Point {
    double x, y, z;
    Point() = default;
    Point(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}
  };
  struct Triangle {
    int a, b, c;
    Triangle() = default;
    Triangle(int _a, int _b, int _c) : a(_a), b(_b), c(_c) {}
  };
  std::vector<Point>    points;
  std::vector<Triangle> triangles;
  TriangleMesh() = default;
  TriangleMesh(int npoints, int ntriangles) : points(npoints), triangles(ntriangles) {}
} TriangleMesh;

#endif
