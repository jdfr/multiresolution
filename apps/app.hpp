#ifndef APP_HEADER
#define APP_HEADER

#include "common.hpp"
#include "iopaths.hpp"
#include "spec.hpp"

#define TYPE_RAW_CONTOUR       0
#define TYPE_PROCESSED_CONTOUR 1
#define TYPE_TOOLPATH          2

#define SAVEMODE_INT64     0
#define SAVEMODE_DOUBLE    1
#define SAVEMODE_DOUBLE_3D 2

static_assert((sizeof(double) == sizeof(int64)) && (sizeof(int64) == sizeof(T64)) && (sizeof(double) == 8), "this code requires that <double>, <long long int> and their union all have a size of 8 bytes.");

bool fileExists(const char *filename);

//Returns NULL if it could not resolve the path. The returned string must be freed with free()
char *fullPath(const char *path);

//utility template for iopaths functionality
template<typename Function, typename... Args> void applyToAllFiles(FILES &files, Function function, Args... args) {
    for (auto file = files.begin(); file != files.end(); ++file) {
        function(*file, args...);
    }
}

typedef struct FileHeader {
    int64 numtools;
    int64 useSched;
    std::vector<double> radiusX;
    std::vector<double> radiusZ;
    int64 numRecords;
    FileHeader() {}
    FileHeader(MultiSpec &multispec, MetricFactors &factors) { buildFrom(multispec, factors); }
    void buildFrom(MultiSpec &multispec, MetricFactors &factors);
    std::string readFromFile(FILE * f);
    std::string openAndReadFromFile(const char * filename, FILE *&file);
    void writeToFiles(FILES &files, bool alsoNumRecords);
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
    void writeToFiles(FILES &files);
    std::string readFromFile(FILE * f);
} SliceHeader;


std::string writeSlice(FILES &files, SliceHeader header, clp::Paths &paths, PathCloseMode mode);

inline std::string writeSlice(FILES &files, clp::Paths &paths, PathCloseMode mode, int64 type, int64 ntool, double z, int64 saveFormat, double scaling) {
    return writeSlice(files, SliceHeader(paths, mode, type, ntool, z, saveFormat, scaling), paths, mode);
}

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
    std::string readFromCommandLine(ParamReader &rd);
} PathInFileSpec;

std::string seekNextMatchingPathsFromFile(FILE * f, FileHeader &fileheader, int &currentRecord, PathInFileSpec &spec, SliceHeader &sliceheader);

typedef struct Point3D {
    double x, y, z;
    Point3D() {}
    Point3D(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}
} Point3D;

typedef std::vector<Point3D> Path3D;
typedef std::vector<Path3D> Paths3D;

void read3DPaths(FILE * f, Paths3D &paths);
void write3DPaths(FILE * f, Paths3D &paths, PathCloseMode mode);
int getPathsSerializedSize(Paths3D &paths, PathCloseMode mode);

#endif