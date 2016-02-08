#ifndef PATHSFILE_HEADER
#define PATHSFILE_HEADER

#include "common.hpp"
#include "simpleparsing.hpp"
#include "spec.hpp"
#include "auxgeom.hpp"
#define INCLUDE_MULTIRESOLUTION_ONLY_FOR_DEFINITIONS //hack to avoid using shared library machinery from multiresolution.h
#include "multiresolution.h" //this is needed just to import the definitions for fields saveFormat and type in SliceHeader

/********************************************************
FUNCTIONALITY TO READ/WRITE PATHSFILES
*********************************************************/

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

//this is a utility class to pattern-match the records of a pathsfile
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

bool read3DPaths(IOPaths &iop, Paths3D &paths);
bool write3DPaths(IOPaths &iop, Paths3D &paths, PathCloseMode mode);
int getPathsSerializedSize(Paths3D &paths, PathCloseMode mode);


//base abstract class to serialize sequences of clp::Paths
class PathWriter {
public:
    std::string err;
    std::string filename;
    virtual ~PathWriter() {}
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool close();
};

//this class implements the common functionality for all PathWriters which can write to several files at once
class PathWriterMultiFile: public PathWriter {
public:
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool close();
    virtual ~PathWriterMultiFile() { close(); }
protected:
    bool matchZNtool(int type, int ntool, double z);
    int findOrCreateSubwriter(int type, double radius, int ntool, double z);
    //derived classes must implement a constructor without parameters, closeSpecific(), startWriter(), writePathsSpecific(), and endWriter()
    virtual bool startWriter();
    virtual bool endWriter();
    virtual bool writePathsSpecific(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool specificClose();
    virtual PathWriterMultiFile* createSubWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z);
    double z, epsilon;
    double radius;
    int type;
    int ntool;
    bool isopen;
    bool generic_for_ntool, generic_for_z, generic_for_type, generic_all, delegateWork;
    int currentSubwriter;
    std::vector<PathWriterMultiFile*> subwriters;
    FILE * f;
};

enum DXFWMode {DXFAscii, DXFBinary};

//write to DXF file (either ascii or binary)
template<DXFWMode mode> class DXFPathWriter : public PathWriterMultiFile {
public:
    DXFPathWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z);
    virtual bool startWriter();
    virtual bool endWriter();
    virtual bool writePathsSpecific(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool specificClose();
    virtual PathWriterMultiFile* createSubWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z);
};

typedef DXFPathWriter<DXFAscii>  DXFAsciiPathWriter;
typedef DXFPathWriter<DXFBinary> DXFBinaryPathWriter;

#endif