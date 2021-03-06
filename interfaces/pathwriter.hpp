#ifndef PATHWRITER_HEADER
#define PATHWRITER_HEADER

#include "pathsfile.hpp"
#include "pathsplitter.hpp"

//base abstract class with interface to serialize sequences of clp::Paths
class PathWriter {
public:
    std::string err;
    std::string filename;
    bool resumeAtStart;
    virtual ~PathWriter() {}
    virtual bool start() = 0; //start is not strictly necessary, it will be automatically called if the file is not already open, but it is convenient to be able to force its use
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) = 0;
    //this method has to be implemented only for subclasses that are used by SplittingPathWriter. It should really be on a separate EnclosedPathWriter subclass,
    //but then, we would like some objects to inherit both from EnclosedPathWriter and from the subclass PathWriterMultiFile, creating the need for virtual inheritance
    //(and that does not make much sense when we are using CRTP in PathWriterMultiFile, after all).
    virtual bool writeEnclosedPaths(PathSplitter::EnclosedPaths &encl, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
        //this is the most sensible default definition for this method
        return writePaths(encl.paths, type, radius, ntool, z, scaling, isClosed);
    }
    virtual bool close() = 0;
};

typedef std::function<bool(int, int, double)> PathFilter;

class PathWriterDelegator : public PathWriter {
public:
    PathWriterDelegator(std::string fname) : isopen(false) { filename = std::move(fname); }
    virtual ~PathWriterDelegator() { close(); }
    virtual bool start(); //start is not strictly necessary, it will be automatically called if the file is not already open, but it is convenient to be able to force its use
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    //this method has to be implemented only for subclasses that are used by SplittingPathWriter. It should really be on a separate EnclosedPathWriter subclass,
    //but then, we would like some objects to inherit both from EnclosedPathWriter and from the subclass PathWriterMultiFile, creating the need for virtual inheritance
    //(and that does not make much sense when we are using CRTP in PathWriterMultiFile, after all).
    virtual bool writeEnclosedPaths(PathSplitter::EnclosedPaths &encl, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool close();
    void addWriter(std::shared_ptr<PathWriter> writer, PathFilter filter) { subs.emplace_back(std::move(filter), std::move(writer)); };
    PathWriter *getWriter(int idx) { return subs[idx].second.get(); }
protected:
    std::vector<std::pair<PathFilter, std::shared_ptr<PathWriter>>> subs;
    bool isopen;
};

//this class implements a PathWriter using the file format specified by FileHeader and SliceHeader
class PathsFileWriter : public PathWriter {
public:
    PathsFileWriter(bool resume, std::string file, FILE *_f, std::shared_ptr<FileHeader> _fileheader, int64 _saveFormat) : f(_f), f_already_open(_f != NULL), isOpen(false), saveFormat(_saveFormat), fileheader(std::move(_fileheader)), numRecords(0), numRecordsSet(false) { filename = std::move(file); resumeAtStart = resume;}
    virtual ~PathsFileWriter() { close(); }
    virtual bool start();
    void setNumRecords(int64 _numRecords) { numRecordsSet = true; numRecords = _numRecords; } //this method is required when the FILE* is a pipe because of the way standalone.cpp is structured
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool close();
protected:
    FILE * f;
    std::shared_ptr<FileHeader> fileheader;
    int64 saveFormat;
    int64 numRecords;
    bool isOpen, f_already_open, numRecordsSet;
};

typedef std::function<std::shared_ptr<PathWriter>(bool, int, PathSplitter&, std::string&, std::string, bool, bool, bool)> SplittingSubPathWriterCreator;

typedef struct SplittingPathWriterState {
    PathSplitter splitter;
    std::string filename_prefix;
    Matrix<std::shared_ptr<PathWriter>> subwriters;
    SplittingPathWriterState(std::shared_ptr<ClippingResources> _res, std::string prefix, PathSplitterConfig config, Configuration *cfg = NULL) : splitter(std::move(config), std::move(_res), cfg), filename_prefix(std::move(prefix)) {}
} SplittingPathWriterState;

//this class splits the Paths it receives in a checkerboard pattern, then passes the pieces to a matrix of PathWriters,
//using the method writeEnclosedPaths() instead of writePaths() for them
class SplittingPathWriter : public PathWriter {
public:
    //either a single PathSplitterConfig, or one for each tool
    SplittingPathWriter(bool resume, std::shared_ptr<ClippingResources> _res, MultiSpec &_spec, SplittingSubPathWriterCreator &callback, PathSplitterConfigs _splitterconfs, std::string file, bool generic_type = true, bool generic_ntool = true, bool generic_z = true) { setup(resume, std::move(_res), (int)_spec.numspecs, _spec.global.config.get(), callback, std::move(_splitterconfs), std::move(file), generic_type, generic_ntool, generic_z); }
    SplittingPathWriter(bool resume, std::shared_ptr<ClippingResources> _res, int numtools,     SplittingSubPathWriterCreator &callback, PathSplitterConfigs _splitterconfs, std::string file, bool generic_type = true, bool generic_ntool = true, bool generic_z = true) { setup(resume, std::move(_res), numtools,            NULL,                      callback, std::move(_splitterconfs), std::move(file), generic_type, generic_ntool, generic_z); }
    virtual ~SplittingPathWriter() { close(); }
    virtual bool start();
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool close();
    virtual bool finishBeforeClose() { return true; } //this method will get called before closing all subwritters, if everything is OK up to that point
protected:
    SplittingPathWriter() {} //this constructor is to be used by subclasses
    bool setup(bool resume, std::shared_ptr<ClippingResources> _res, int ntools, Configuration *_cfg, SplittingSubPathWriterCreator &callback, PathSplitterConfigs splitterconfs, std::string file, bool generic_type, bool generic_ntool, bool generic_z);
    std::vector<SplittingPathWriterState> states; //one state for each PathSplitterConfig
    int numtools;
    bool justone;
    bool isopen;
    bool alreadyfinished;
};

void addExtension(std::string &filename, std::string ext);

#endif
