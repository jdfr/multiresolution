#ifndef PATHWRITER_HEADER
#define PATHWRITER_HEADER

#include "pathsfile.hpp"

//base abstract class with interface to serialize sequences of clp::Paths
class PathWriter {
public:
    std::string err;
    std::string filename;
    virtual ~PathWriter() {}
    virtual bool start(); //start is not strictly necessary, it will be automatically called if the file is not already open, but it is convenient to be able to force its use
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool close();
};

//this class implements a PathWriter using the file format specified by FileHeader and SliceHeader
class PathsFileWriter : public PathWriter {
public:
    PathsFileWriter(std::string file, FILE *_f, std::shared_ptr<FileHeader> _fileheader, int64 _saveFormat, int64 _numRecords = -1) : f(_f), f_already_open(_f != NULL), isOpen(false), saveFormat(_saveFormat), fileheader(std::move(_fileheader)), numRecords(_numRecords) { filename = std::move(file); }
    virtual ~PathsFileWriter() { close(); }
    virtual bool start();
    void setNumRecords(int64 _numRecords) { numRecords = _numRecords; } //this method is required because of the way standalone.cpp is structured
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool close();
protected:
    FILE * f;
    std::shared_ptr<FileHeader> fileheader;
    int64 saveFormat;
    int64 numRecords;
    bool isOpen, f_already_open;
};

//this class implements the common functionality for all PathWriters which can write to several files at once.
//We use the CRTP idiom to get compile-time dispatch where we need it
template<typename T> class PathWriterMultiFile : public PathWriter {
public:
    virtual bool start();
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool close();
    virtual ~PathWriterMultiFile() { close(); }
    bool writeToAll(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
protected:
    bool matchZNtool(int type, int ntool, double z);
    int findOrCreateSubwriter(int type, double radius, int ntool, double z);
    //derived classes must implement a constructor without parameters, specificClose(), startWriter(), writePathsSpecific(), and endWriter()
    double z, epsilon;
    double radius;
    int type;
    int ntool;
    bool isopen;
    bool generic_for_ntool, generic_for_z, generic_for_type, generic_all, delegateWork;
    int currentSubwriter;
    //this has to be a vector of pointers because if the vector gets bigger, it will destroy and recreate the objects, interfering with the life cycle we have designed
    std::vector<T*> subwriters;
    FILE * f;
};

enum DXFWMode { DXFAscii, DXFBinary };

//write to DXF file (either ascii or binary)
//TODO: currently, this only works on little-endian machines such as x86. If necessary, modify so it also work in different XXX-endians
template<DXFWMode mode> class DXFPathWriter : public PathWriterMultiFile<DXFPathWriter<mode>> {
public:
    DXFPathWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z);
    bool startWriter();
    bool endWriter();
    bool writePathsSpecific(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    bool specificClose();
};

typedef DXFPathWriter<DXFAscii>  DXFAsciiPathWriter;
typedef DXFPathWriter<DXFBinary> DXFBinaryPathWriter;

#endif
