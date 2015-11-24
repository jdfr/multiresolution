#ifndef APP_HEADER
#define APP_HEADER

#include "common.hpp"
#include "iopaths.hpp"
#include "spec.hpp"
#include "multiresolution.h" //this is needed just to import the definitions for fields saveFormat and type in SliceHeader

static_assert((sizeof(double) == sizeof(int64)) && (sizeof(int64) == sizeof(T64)) && (sizeof(double) == 8), "this code requires that <double>, <long long int> and their union all have a size of 8 bytes.");

typedef std::vector<FILE*> FILES;

bool fileExists(const char *filename);

//Returns NULL if it could not resolve the path. The returned string must be freed with free()
char *fullPath(const char *path);

typedef struct FileHeader {
    int64 numtools;
    int64 useSched;
    std::vector<double> radiusX;
    std::vector<double> radiusZ;
    int64 numRecords;
    FileHeader() {}
    FileHeader(MultiSpec &multispec, MetricFactors &factors) { buildFrom(multispec, factors); }
    void buildFrom(MultiSpec &multispec, MetricFactors &factors);
    std::string readFromFile(FILE *f);
    std::string writeToFile(FILE *f, bool alsoNumRecords);
} HeaderFile;

typedef struct SliceHeader {
    int64 totalSize;
    int64 headerSize;
    int64 type;
    int64 ntool;
    double z;
    int64 saveFormat;
    double scaling;
    static const int numFields = 7;
    std::vector<T64> alldata;
    SliceHeader() {}
    SliceHeader(clp::Paths &paths, PathCloseMode mode, int64 _type, int64 _ntool, double _z, int64 _saveFormat, double _scaling) { setTo(paths, mode, _type, _ntool, _z, _saveFormat, _scaling); }
    void setTo(clp::Paths &paths, PathCloseMode mode, int64 _type, int64 _ntool, double _z, int64 _saveFormat, double _scaling);
    void setBuffer();
    std::string writeToFile(FILE * f);
    std::string readFromFile(FILE * f);
} SliceHeader;


std::string writeSlice(FILE *f, SliceHeader header, clp::Paths &paths, PathCloseMode mode);

typedef struct PathInFileSpec {
    int64 type;
    int64 ntool;
    double z;
    bool usetype;
    bool usentool;
    bool usez;
    PathInFileSpec() : usetype(false), usentool(false), usez(false) {}
    PathInFileSpec(int _type) : type(_type), usetype(true), usentool(false), usez(false) {}
    PathInFileSpec(int _type, int _ntool) : type(_type), ntool(_ntool), usetype(true), usentool(true), usez(false) {}
    PathInFileSpec(int _type, double _z) : type(_type), z(_z), usetype(true), usentool(false), usez(true) {}
    PathInFileSpec(int _type, int _ntool, double _z) : type(_type), ntool(_ntool), z(_z), usetype(true), usentool(true), usez(true) {}
    PathInFileSpec(double _z) : z(_z), usetype(false), usentool(false), usez(true) {}
    //this function matches writeSlice()'s header
    bool matchesHeader(SliceHeader &h);
    //read at most 'maxtimes' specs (as much as possible if maxtimes<0). If furtherArgs is false, tries to consume all the remaining input until all is consumed, treating anything non-conformant as an error. If it is true, it stops if it cannot recognize an argument, to enable consumption of further arguments by other code
    std::string readFromCommandLine(ParamReader &rd, int maxtimes, bool furtherArgs);
} PathInFileSpec;

std::string seekNextMatchingPathsFromFile(FILE * f, FileHeader &fileheader, int &currentRecord, PathInFileSpec &spec, SliceHeader &sliceheader);

typedef struct Point3D {
    double x, y, z;
    Point3D() {}
    Point3D(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}
} Point3D;

typedef std::vector<Point3D> Path3D;
typedef std::vector<Path3D> Paths3D;

bool read3DPaths(IOPaths &iop, Paths3D &paths);
bool write3DPaths(IOPaths &iop, Paths3D &paths, PathCloseMode mode);
int getPathsSerializedSize(Paths3D &paths, PathCloseMode mode);

#endif