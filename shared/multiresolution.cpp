//no point in defining all this if not exporting symbols...
#ifdef LIBRARY_EXPORTS

#include "multiresolution.h"
#include "common.hpp"
static_assert(sizeof(coord_type) == sizeof(clp::cInt), "please correct interface.h so typedef coord_type resolves to the same type as typedef ClipperLib::cInt");
#include "pathsfile.hpp"

//THIS FILE IMPLEMENTS THE SHARED LIBRARY INTERFACE

#include "config.hpp"
#include "spec.hpp"
#include "parsing.hpp"
#include "3d.hpp"

#include <fcntl.h>
#include <string.h>
#include <vector>
#include <iterator>
#include <algorithm>
#include <cmath>

#if defined(_WIN32) || defined(_WIN64)
#  include <io.h>
#endif

/////////////////////////////////////////////////
//SHARED LIBRARY INTERFACE
/////////////////////////////////////////////////

typedef struct SharedLibraryConfig {
    std::string err;
    Configuration config;

    SharedLibraryConfig(char * configfile) { config.load(configfile); }
} SharedLibraryConfig;

typedef struct SharedLibraryState {
    std::string err;
    MultiSpec spec;
    std::vector<clp::cInt> processRadiuses;
    SimpleSlicingScheduler * sched;
    Multislicer * multi;
    SharedLibraryState(Configuration *config) : spec(*config), sched(NULL), multi(NULL) {}
    ~SharedLibraryState() {
        if (this->sched) {
            delete this->sched;
        }
        if (this->multi) {
            delete this->multi;
        }
    }
} SharedLibraryState;



//structure to read a slice in the shared library interface
typedef struct SharedLibrarySlice {
    std::string err;
    clp::Paths *paths;
    bool freepaths;
    int* numpoints;
    clp::cInt** pathpointers;
    SharedLibrarySlice(int numpaths);
    SharedLibrarySlice(clp::Paths *_paths);
    ~SharedLibrarySlice();
} SharedLibrarySlice;

//structure to hold the results of the multislicer in the shared library interface
typedef struct SharedLibraryResult {
    std::string err;
    std::vector<std::shared_ptr<ResultSingleTool>> res;
    int* numpoints;
    clp::cInt** pathpointers;
    double z;
    int ntool;
    SharedLibraryResult(size_t _numtools, int _ntool = -1, double _z = NAN) : res(_numtools), ntool(_ntool), z(_z), numpoints(NULL), pathpointers(NULL) {}
    SharedLibraryResult(std::shared_ptr<ResultSingleTool> single) : numpoints(NULL), pathpointers(NULL), res(1, std::move(single)), ntool(res[0]->ntool), z(res[0]->z) {}
    ~SharedLibraryResult();
} SharedLibraryResult;

#if ( defined(_WIN32) || defined(_WIN64) )
//taken from http://codereview.stackexchange.com/questions/419/converting-between-stdwstring-and-stdstring
std::wstring s2ws(const std::string& s)
{
    int len;
    int slength = (int)s.length() + 1;
    len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0); 
    std::wstring r(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, &r[0], len);
    return r;
}

std::string ws2s(const std::wstring& s)
{
    int len;
    int slength = (int)s.length() + 1;
    len = WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, 0, 0, 0, 0); 
    std::string r(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, &r[0], len, 0, 0); 
    return r;
}
#endif

SharedLibrarySlice::SharedLibrarySlice(int numpaths) {
    this->paths = new clp::Paths(numpaths);
    this->freepaths = true;
    this->numpoints = new int[numpaths];
    this->pathpointers = new clp::cInt*[numpaths];
}

SharedLibrarySlice::SharedLibrarySlice(clp::Paths *_paths) {
    this->paths = _paths;
    this->freepaths = false;
    size_t numpaths = _paths->size();
    this->numpoints = new int[numpaths];
    this->pathpointers = new clp::cInt*[numpaths];
}

SharedLibrarySlice::~SharedLibrarySlice() {
    if (this->freepaths) delete this->paths;
    delete this->numpoints;
    delete this->pathpointers;
}

SharedLibraryResult::~SharedLibraryResult() {
    if(this->numpoints!=NULL) delete this->numpoints;
    if(this->pathpointers!=NULL) delete this->pathpointers;
}


LIBRARY_API  BSTR getErrorText(void* value) {
    //err is defined as the first memeber in all structs to be able to retrieve it from any of them with uniform code. Yes, I know, this is dirty as hell
#if ( defined(_WIN32) || defined(_WIN64) )
    std::wstring werr = s2ws(((SharedLibraryResult*)value)->err);
    return SysAllocString(werr.c_str());
#else
    //char *noreturn = ((SharedLibraryResult*)value)->err.c_str();
    //char *ret = (char*)malloc(strlen(noreturn) + 1);
    //strcpy(ret, noreturn);
    //return ret;
    return ((SharedLibraryResult*)value)->err.c_str();
#endif
}

StateHandle initState(Configuration *config, std::vector<std::string> &args, bool doscale) {
    SharedLibraryState *state = new SharedLibraryState(config);
    if (state->spec.global.config.has_err) {
        state->err = state->spec.global.config.err;
        return state;
    }
    double scale;
    std::string err = getScale(doscale, *config, scale);
    if (!err.empty()) { state->err = err; return state; }
    err = parseAll(state->spec, NULL, args, scale);
    if (!err.empty()) { state->err = err; return state; }
    if (state->spec.global.useScheduler) {
        bool removeUnused = true;
        state->sched = new SimpleSlicingScheduler(removeUnused, state->spec);
    } else {
        state->multi = new Multislicer(state->spec);
    }
    //state->args.err = "hellooooooooo"; return args;
    return state;
}

LIBRARY_API  StateHandle parseArguments(ConfigHandle config, int doscale, char* arguments) {
    std::string argl(arguments);
    auto args = normalizedSplit(argl);
    return initState(&config->config, args, doscale != 0);
}

LIBRARY_API  StateHandle parseArgumentsMainStyle(ConfigHandle config, int doscale, int argc, const char** argv) {
    auto args = getArgs(argc, argv);
    return initState(&config->config, args, doscale!=0);
}

LIBRARY_API ParamsExtractInfo getParamsExtract(StateHandle state) {
    ParamsExtractInfo ret;
    ret.numProcesses    = (int)state->spec.numspecs;
    state->processRadiuses.clear();
    state->processRadiuses.reserve(state->spec.numspecs);
    for (auto &pp : state->spec.pp) {
        state->processRadiuses.push_back(pp.radius);
    }
    ret.processRadiuses = &(state->processRadiuses.front());
    return ret;
}

LIBRARY_API  ConfigHandle readConfiguration(char *configfilename) {
    SharedLibraryConfig *config = new SharedLibraryConfig(configfilename);
    if (config->config.has_err) {
        config->err = config->config.err;
    }
    return config;
}

std::string helpstr;
LIBRARY_API char * getParameterHelp(int showGlobals, int showPerProcess, int showExample) {
    if (helpstr.empty()) {
        composeParameterHelp(showGlobals != 0, showPerProcess != 0, showExample != 0, helpstr);
    }
    return (char *)helpstr.c_str();
}


LIBRARY_API  void freeState(StateHandle state) {
    if (state != NULL) delete state;
}

LIBRARY_API  void freeConfig(ConfigHandle config) {
    if (config != NULL) delete config;
}

LIBRARY_API  InputSliceInfo createInputSlice(int numpaths) {
    InputSliceInfo info;
    SharedLibrarySlice *slice = new SharedLibrarySlice(numpaths);
    info.slice = slice;
    info.numpointsArray = slice->numpoints;
    return info;
}

LIBRARY_API  clp::cInt** getPathsArray(SharedLibrarySlice* slice) {
    size_t numpaths = slice->paths->size();
    for (size_t k = 0; k < numpaths; ++k) {
        (*(slice->paths))[k].resize(slice->numpoints[k]);
        slice->pathpointers[k] = (clp::cInt*)( (*( slice->paths))[k].data());
    }
    return slice->pathpointers;
}
    

LIBRARY_API  ResultsHandle computeResult(SharedLibrarySlice* slice, StateHandle state) {
    clp::Paths dummy;
    size_t numspecs = state->spec.numspecs;
    SharedLibraryResult * result = new SharedLibraryResult(numspecs);
        
    GlobalSpec &global = state->spec.global;
    if (slice->paths->size() > 0) {
        //this is a very ugly hack to compromise between part of the code requiring vector<shared_ptr<T>>
        //because of convoluted co-ownership requirements and other part happily using vector<T>
        std::vector<SingleProcessOutput*> ress(result->res.size());
        for (int k = 0; k < result->res.size(); ++k) {
            result->res[k] = std::make_shared<ResultSingleTool>(ResultSingleTool());
            ress[k] = &*result->res[k];
        }

        int lastk = state->multi->applyProcesses(ress, *slice->paths, dummy);
        if (lastk != numspecs) {
            result->err = result->res[lastk]->err;
            return result;
        }

    }
    
    return result;

}
    
LIBRARY_API  void freeInputSlice(SharedLibrarySlice* slice) {
    if (slice!=NULL) delete slice;
}

LIBRARY_API  int alsoComplementary(SharedLibraryResult* result, int ntool) {
    return result->res[ntool]->alsoInfillingAreas;
}

inline clp::Paths *getDesiredPaths(SharedLibraryResult *result, int ntool, OutputSliceInfo_PathType pathtype) {
    clp::Paths * paths;
    switch (pathtype) {
    case PathProcessed:
        paths = &result->res[ntool]->infillingAreas; break;
    case PathContour:
        paths = &result->res[ntool]->contoursToShow; break;
    case PathToolPath:
    default:
        paths = &result->res[ntool]->toolpaths; break;
    }
    return paths;
}

template<typename InfoStruct, typename PointType, typename CoordType> void genericFillOutput(InfoStruct &out, std::vector<std::vector<PointType>> &paths, int *&numpoints, CoordType **&pathpointers) {
    out.numpaths = (int)(paths.size());
    if (numpoints != NULL) delete numpoints;
    numpoints = new int[out.numpaths];
    for (size_t k = 0; k<out.numpaths; ++k) {
        numpoints[k] = (int)(paths[k].size());
    }
    out.numpointsArray = numpoints;

    if (pathpointers != NULL) delete pathpointers;
    pathpointers = new CoordType*[out.numpaths];
    for (size_t k = 0; k < out.numpaths; ++k) {
        pathpointers[k] = (CoordType*) (paths[k].data());
    }
    out.pathsArray = (decltype(out.pathsArray))pathpointers;
}

LIBRARY_API OutputSliceInfo getOutputSliceInfo(SharedLibraryResult* result, int ntool, OutputSliceInfo_PathType pathtype) {
    clp::Paths * paths = getDesiredPaths(result, ntool, pathtype);

    OutputSliceInfo out;
    genericFillOutput(out, *paths, result->numpoints, result->pathpointers);

    out.ntool = result->ntool;

    out.z = result->z;

    return out;
}


LIBRARY_API  void freeResult(SharedLibraryResult* result) {
    if (result!=NULL) delete result;
}

void voidSlices3DSpecInfo(Slices3DSpecInfo &info) {
    info.numinputslices = info.numoutputslices = -1;
    info.zs = NULL;
}

LIBRARY_API Slices3DSpecInfo computeSlicesZs(StateHandle state, double zmin, double zmax) {
    Slices3DSpecInfo ret;
    if (state->sched == NULL) {
        state->err = "Cannot use computeSlicesZs if the scheduler was not configured in the arguments!!!!";
        voidSlices3DSpecInfo(ret);
        return ret;
    }
    state->sched->createSlicingSchedule(zmin, zmax, state->spec.global.z_epsilon, ScheduleTwoPhotonSimple);
    if (state->sched->has_err) {
        state->err = state->sched->err;
        voidSlices3DSpecInfo(ret);
        return ret;
    }
    if (state->spec.global.fb.feedback) {
        //MetricFactors is not needed anywhere else in the shared library interface, so we can create it here as an one-off
        MetricFactors factors(state->spec.global.config);
        if (!factors.err.empty()) {
            state->err = factors.err;
            voidSlices3DSpecInfo(ret);
            return ret;
        }

        //un-scale the z values for raw slices, since they are needed by applyFeedback()
        std::vector<double> rawZs = state->sched->rm.rawZs;
        for (auto z = rawZs.begin(); z != rawZs.end(); ++z) {
            *z *= factors.internal_to_input;
        }

        std::string err = applyFeedback(state->spec.global.config, factors, *state->sched, rawZs, state->sched->rm.rawZs);
        if (!err.empty()) {
            state->err = err;
            voidSlices3DSpecInfo(ret);
            return ret;
        }
    }

    ret.numinputslices = (int)state->sched->rm.raw.size();
    ret.numoutputslices = (int)state->sched->output.size();
    ret.zs = &state->sched->rm.rawZs.front();
    return ret;
}

//important: after using the slice here, it is "spent" (cannot be used in another context), but you still have to use freeInputSlice on it!!!
LIBRARY_API void receiveAdditionalAdditiveContours(StateHandle state, double z, InputSliceHandle slice) {
    //this method uses std::move in the second argument!
    state->sched->tm.takeAdditionalAdditiveContours(z, *slice->paths);
}

LIBRARY_API void purgueAdditionalAdditiveContours(StateHandle state) {
    state->sched->tm.purgeAdditionalAdditiveContours();
}

//error messages from this function can be queried from the StateHandle argument
LIBRARY_API void receiveInputSlice(StateHandle state, InputSliceHandle slice) {
    state->sched->rm.receiveNextRawSlice(*slice->paths);
    if (state->sched->has_err) {
        state->err = state->sched->err;
    }
}

LIBRARY_API void computeOutputSlices(StateHandle state) {
    state->sched->computeNextInputSlices();
    if (state->sched->has_err) {
        state->err = state->sched->err;
    }
}

/*may return NULL to mean that no output is ready yet. Because of that,
error strings can be queried from the ArgumentsHandle argument*/
LIBRARY_API ResultsHandle giveOutputIfAvailable(StateHandle state) {
    if (state->sched->output_idx >= state->sched->output.size()) {
        return NULL;
    }
    std::shared_ptr<ResultSingleTool> single = state->sched->giveNextOutputSlice(); //this method will return slices in the ordering
    if (state->sched->has_err) {
        state->err = state->sched->err + " (in giveOutputIfAvailable.1)";
        return NULL;
    }
    if (single == NULL) {
        return NULL;
    }
    if (single->has_err) {
        state->err = single->err + " (in giveOutputIfAvailable.2)";
        return NULL;
    }
    ResultsHandle ret = new SharedLibraryResult(std::move(single));
    /*ResultsHandle ret = new SharedLibraryResult(1, single->ntool, single->z);
    ret->res[0] = single;*/

    return ret;
}


typedef struct SharedLibraryPaths {
    std::string err;
    std::string filename;
    IOPaths iop;
    FileHeader fileheader;
    int currentRecord;
    int* numpoints;
    clp::cInt **pathpointersi;
    double **pathpointersd;
    clp::Paths pathsi;
    DPaths pathsd;
    Paths3D pathsd3;
    SharedLibraryPaths(const char *_filename, FILE *f) : filename(_filename), iop(f), err(), currentRecord(0), numpoints(NULL), pathpointersi(NULL), pathpointersd(NULL) {}
    void clearPaths() {
        pathsi.clear();
        pathsd.clear();
        pathsd3.clear();
        if (pathpointersi != NULL) delete pathpointersi;
        if (pathpointersd != NULL) delete pathpointersd;
        pathpointersi = NULL;
        pathpointersd = NULL;
    }
    ~SharedLibraryPaths() {
        if (numpoints     != NULL) delete numpoints;
        if (pathpointersi != NULL) delete pathpointersi;
        if (pathpointersd != NULL) delete pathpointersd;
        if (iop.f         != NULL) fclose(iop.f);
    }
} SharedLibraryPaths;

LIBRARY_API  LoadPathFileInfo loadPathsFile(char *pathsfilename) {
    LoadPathFileInfo result;
    result.numRecords = result.ntools = -1;
    result.pathfile   = NULL;

    FILE * file = fopen(pathsfilename, "rb");

    result.pathfile = new SharedLibraryPaths(pathsfilename, file);

    if (file == NULL) {
        result.pathfile->err = str("error while trying to open file ", pathsfilename);
        return result;
    }

    result.pathfile->err = result.pathfile->fileheader.readFromFile(file);
    result.numRecords    = (int)result.pathfile->fileheader.numRecords;
    result.ntools        = (int)result.pathfile->fileheader.numtools;

    return result;
}

LIBRARY_API void freePathsFile(PathsHandle paths) {
    if (paths != NULL) {
        delete paths;
    }
}

LIBRARY_API  LoadPathInfo loadNextPaths(PathsHandle paths) {
    LoadPathInfo out;

    if (!paths->err.empty()) {
        out.numpointsArray = NULL; //avoid annoying warning in MSVS
        return out;
    }
    if (paths->currentRecord >= paths->fileheader.numRecords) {
        out.numRecord = -1;
        return out;
    } else {
        out.numRecord = paths->currentRecord;
    }
    if (feof(paths->iop.f) != 0) {
        paths->err = str("In file ", paths->filename, ": could not read record ", paths->currentRecord, " (unexpected EOF)");
        return out;
    }

    SliceHeader header;
    paths->err = header.readFromFile(paths->iop.f);
    if (!paths->err.empty()) {
        return out;
    }
    if (header.alldata.size() < 7) {
        paths->err = str("in file ", paths->filename, "record ", paths->currentRecord, " had a bad header");
        return out;
    }

    paths->clearPaths();
    if (header.saveFormat == PATHFORMAT_INT64) {
        if (!paths->iop.readClipperPaths(paths->pathsi)) { paths->err = str("In file ", paths->filename, ": could not read integer paths in record ", paths->currentRecord, ", message: <", paths->iop.errs[0].message, "> in ", paths->iop.errs[0].function); out; };
        genericFillOutput(out, paths->pathsi, paths->numpoints, paths->pathpointersi);
    } else if (header.saveFormat == PATHFORMAT_DOUBLE) {
        if (!paths->iop.readDoublePaths(paths->pathsd))  { paths->err = str("In file ", paths->filename, ": could not read double paths in record ",  paths->currentRecord, ", message: <", paths->iop.errs[0].message, "> in ", paths->iop.errs[0].function); out; };
        genericFillOutput(out, paths->pathsd, paths->numpoints, paths->pathpointersd);
    } else if (header.saveFormat == PATHFORMAT_DOUBLE_3D) {
        if (!read3DPaths(paths->iop, paths->pathsd3))    { paths->err = str("In file ", paths->filename, ": could not read 3d paths in record ",      paths->currentRecord, ", message: <", paths->iop.errs[0].message, "> in ", paths->iop.errs[0].function); out; };
        genericFillOutput<LoadPathInfo, Point3D, double>(out, paths->pathsd3, paths->numpoints, paths->pathpointersd);
    }
    out.ntool      = (int)header.ntool;
    out.type       = (int)header.type;
    out.saveFormat = (int)header.saveFormat;
    out.scaling    =      header.scaling;
    out.z          =      header.z;

    ++(paths->currentRecord);

    return out;
}


#endif
