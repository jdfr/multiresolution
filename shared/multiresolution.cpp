//THIS FILE IMPLEMENTS THE SHARED LIBRARY INTERFACE

//no point in defining all this if not exporting symbols...
#ifdef LIBRARY_EXPORTS

#include "multiresolution.h"
#include "pathsfile.hpp"
#include "parsing.hpp"
#include "3d.hpp"
#include <string.h>
static_assert(sizeof(coord_type) == sizeof(clp::cInt), "please correct interface.h so typedef coord_type resolves to the same type as typedef ClipperLib::cInt");

/////////////////////////////////////////////////
//SHARED LIBRARY INTERFACE
/////////////////////////////////////////////////

typedef struct HasError {
    std::string err;
} HasError;

typedef struct SharedLibraryConfig : public HasError {
    std::shared_ptr<Configuration> config;
    std::shared_ptr<MetricFactors> factors;

    SharedLibraryConfig(char * configfile) {
        config = std::make_shared<Configuration>();
        config->load(configfile);
        if (config->has_err) {
            err = config->err;
            return;
        }
        bool doscale = true;
        factors = std::make_shared<MetricFactors>(*config, doscale);
        if (!factors->err.empty()) {
            err = factors->err;
        }

    }
} SharedLibraryConfig;

typedef struct SharedLibraryState : public HasError {
    std::shared_ptr<Configuration> config;
    std::shared_ptr<MetricFactors> factors;
    std::shared_ptr<MultiSpec> spec;
    std::vector<clp::cInt> processRadiuses;
    std::shared_ptr<SimpleSlicingScheduler> sched;
    std::shared_ptr<Multislicer> multi;
    SharedLibraryState(std::shared_ptr<Configuration> _config) : config(std::move(_config)) { spec = std::make_shared<MultiSpec>(config); }
} SharedLibraryState;



//structure to read a slice in the shared library interface
typedef struct SharedLibrarySlice : public HasError {
    std::shared_ptr<clp::Paths> paths;
    std::vector<int> numpoints;
    std::vector<clp::cInt*> pathpointers;
    SharedLibrarySlice(int numpaths);
    SharedLibrarySlice(std::shared_ptr<clp::Paths> _paths);
} SharedLibrarySlice;

//structure to hold the results of the multislicer in the shared library interface
typedef struct SharedLibraryResult : public HasError {
    std::vector<std::shared_ptr<ResultSingleTool>> res;
    std::vector<int> numpoints;
    std::vector<clp::cInt*> pathpointers;
    double z;
    int ntool;
    SharedLibraryResult(size_t _numtools, int _ntool = -1, double _z = NAN) : res(_numtools), ntool(_ntool), z(_z) {}
    SharedLibraryResult(std::shared_ptr<ResultSingleTool> single) : res(1, std::move(single)), ntool(res[0]->ntool), z(res[0]->z) {}
} SharedLibraryResult;

SharedLibrarySlice::SharedLibrarySlice(int numpaths) : paths(std::make_shared<clp::Paths>(numpaths)), numpoints(numpaths), pathpointers(numpaths) {}

SharedLibrarySlice::SharedLibrarySlice(std::shared_ptr<clp::Paths> _paths) : paths(std::move(_paths)), numpoints(paths->size()), pathpointers(paths->size()) {}

LIBRARY_API  char * getErrorText(void* value) {
    std::string &err = static_cast<HasError*>(value)->err;
    if (err.empty()) return NULL;
    return const_cast<char *>(err.c_str());
}

StateHandle initState(std::shared_ptr<Configuration> config, std::shared_ptr<MetricFactors> factors, std::vector<std::string> &args) {
    SharedLibraryState *state = new SharedLibraryState(config);
    if (state->spec->global.config->has_err) {
        state->err = state->spec->global.config->err;
        return state;
    }
    state->factors = factors;
    try {
        ParserAllLocalAndGlobal parser(*state->factors, *state->spec, NotAddNano, YesAddResponseFile);
        parser.setParsedOptions(args, NULL);
    } catch (std::exception &e) {
        state->err = e.what();
        return state;
    }
    if (state->spec->global.useScheduler) {
        bool removeUnused = true;
        state->sched = std::make_shared<SimpleSlicingScheduler>(removeUnused, state->spec);
    } else {
        state->multi = std::make_shared<Multislicer>(state->spec);
    }
    //state->args.err = "hellooooooooo"; return args;
    return state;
}

LIBRARY_API  StateHandle parseArguments(ConfigHandle config, char* arguments) {
    std::string argl(arguments);
    auto args = normalizedSplit(argl);
    return initState(config->config, config->factors, args);
}

LIBRARY_API  StateHandle parseArgumentsMainStyle(ConfigHandle config, int argc, const char** argv) {
    auto args = getArgs(argc, argv);
    return initState(config->config, config->factors, args);
}

LIBRARY_API ParamsExtractInfo getParamsExtract(StateHandle state) {
    ParamsExtractInfo ret;
    ret.numProcesses = (int)state->spec->numspecs;
    state->processRadiuses.clear();
    state->processRadiuses.reserve(state->spec->numspecs);
    for (auto &pp : state->spec->pp) {
        state->processRadiuses.push_back(pp.radius);
    }
    ret.processRadiuses = &(state->processRadiuses.front());
    return ret;
}

LIBRARY_API ConfigExtractInfo getConfigExtract(ConfigHandle config) {
    ConfigExtractInfo ret;
    ret.factor_input_to_internal = config->factors->input_to_internal;
    ret.factor_internal_to_input = config->factors->internal_to_input;
    ret.factor_slicer_to_internal = config->factors->slicer_to_internal;
    return ret;
}


LIBRARY_API  ConfigHandle readConfiguration(char *configfilename) {
    SharedLibraryConfig *config = new SharedLibraryConfig(configfilename);
    if (config->config->has_err) {
        config->err = config->config->err;
    }
    return config;
}

void composeParameterHelp(po::options_description *globals, po::options_description* perProcess, bool example, std::ostream &output) {
    bool g = globals != NULL;
    bool p = perProcess != NULL;
    bool gop = g || p;
    if (gop) output << "The multislicing engine is very flexible.\n  It takes parameters as if it were a command line application.\n  If there is no ambiguity, options can be specified as prefixes of their full names.\n";
    if (g)    globals->print(output);
    if (p) perProcess->print(output);
    if (example && gop) {
        output << "\nExample:";
        if (g) output << " --save-contours --motion-planner --slicing-scheduler";
        if (p) output << " --process 0 --radx 75 --voxel-profile constant --voxel-z 75 67.5 --gridstep 0.1 --snap --smoothing 0.01 --tolerances 0.75 0.01 --safestep --clearance --medialaxis-radius 1.0  --process 1 --radx 10 --voxel-profile constant --voxel-z 10 9 --gridstep 0.1 --snap --smoothing 0.1 --tolerances 0.1 0.001 --safestep --clearance --medialaxis-radius 1.0";
        output << "\n";
    }
}

void composeParameterHelp(po::options_description *globals, po::options_description* perProcess, bool example, std::string &output) {
    std::ostringstream fmt;
    composeParameterHelp(globals, perProcess, example, fmt);
    output = fmt.str();
}

LIBRARY_API char * getParameterHelp(int showGlobals, int showPerProcess, int showExample) {
    std::shared_ptr<po::options_description> globals;
    std::shared_ptr<po::options_description> perProcess;
    std::string helpstr;
    if (showGlobals != 0) {
        globals    = std::make_shared<po::options_description>(std::move(    globalOptionsGenerator(NotAddNano, YesAddResponseFile)));
    }
    if (showPerProcess != 0) {
        perProcess = std::make_shared<po::options_description>(std::move(perProcessOptionsGenerator(NotAddNano)));
    }
    composeParameterHelp(globals.get(), perProcess.get(), showExample != 0, helpstr);

    //we copy the string again, just to avoid having a dedicated object to free afterwards
    char * res = new char[helpstr.size() + 2];
    strcpy(res, helpstr.c_str());
    return res;
}

LIBRARY_API  void  freeParameterHelp(char *helpstr) {
    delete helpstr;
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
    info.numpointsArray = &slice->numpoints.front();
    return info;
}

LIBRARY_API  clp::cInt** getPathsArray(SharedLibrarySlice* slice) {
    size_t numpaths = slice->paths->size();
    for (size_t k = 0; k < numpaths; ++k) {
        (*(slice->paths))[k].resize(slice->numpoints[k]);
        slice->pathpointers[k] = (clp::cInt*)((*(slice->paths))[k].data());
    }
    return &slice->pathpointers.front();
}


LIBRARY_API  ResultsHandle computeResult(SharedLibrarySlice* slice, StateHandle state) {
    clp::Paths dummy;
    size_t numspecs = state->spec->numspecs;
    SharedLibraryResult * result = new SharedLibraryResult(numspecs);

    if (slice->paths->size() > 0) {
        //this is a very ugly hack to compromise between part of the code requiring vector<shared_ptr<T>>
        //because of convoluted co-ownership requirements and other part happily using vector<T>
        std::vector<SingleProcessOutput*> ress(result->res.size());
        for (int k = 0; k < result->res.size(); ++k) {
            result->res[k] = std::make_shared<ResultSingleTool>(ResultSingleTool());
            ress[k] = &*result->res[k];
        }

        try {
            int lastk = state->multi->applyProcesses(ress, *slice->paths, dummy);
            if (lastk != numspecs) {
                result->err = result->res[lastk]->err;
            }
        } catch (clp::clipperException &e) {
            result->err = handleClipperException(e);
        } catch (std::exception &e) {
            result->err = str("Unhandled exception: ", e.what());
        }

    }

    return result;

}

LIBRARY_API  void freeInputSlice(SharedLibrarySlice* slice) {
    if (slice != NULL) delete slice;
}

LIBRARY_API  int alsoComplementary(SharedLibraryResult* result, int ntool) {
    return result->res[ntool]->alsoInfillingAreas;
}

inline clp::Paths *getDesiredPaths(SharedLibraryResult *result, int ntool, OutputSliceInfo_PathType pathtype) {
    clp::Paths * paths;
    switch (pathtype) {
    case PathInfillingAreas:
        paths = &result->res[ntool]->infillingAreas; break;
    case PathContour:
        paths = &result->res[ntool]->contoursToShow; break;
    case PathToolPath:
    default:
        paths = &result->res[ntool]->toolpaths; break;
    }
    return paths;
}

template<typename InfoStruct, typename PointType, typename CoordType> void genericFillOutput(InfoStruct &out, std::vector<std::vector<PointType>> &paths, std::vector<int> &numpoints, std::vector<CoordType *> &pathpointers) {
    out.numpaths = (int)(paths.size());
    numpoints.clear();
    numpoints.resize(out.numpaths);
    for (size_t k = 0; k<out.numpaths; ++k) {
        numpoints[k] = (int)(paths[k].size());
    }
    out.numpointsArray = &numpoints.front();

    pathpointers.clear();
    pathpointers.resize(out.numpaths);
    for (size_t k = 0; k < out.numpaths; ++k) {
        pathpointers[k] = (CoordType*)(paths[k].data());
    }
    out.pathsArray = (decltype(out.pathsArray))(&pathpointers.front());
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
    if (result != NULL) delete result;
}

void voidSlices3DSpecInfo(Slices3DSpecInfo &info) {
    info.numinputslices = info.numoutputslices = -1;
    info.zs = NULL;
}

LIBRARY_API Slices3DSpecInfo computeSlicesZs(StateHandle state, double zmin, double zmax) {
    Slices3DSpecInfo ret;
    if (!state->sched) {
        state->err = "Cannot use computeSlicesZs if the scheduler was not configured in the arguments!!!!";
        voidSlices3DSpecInfo(ret);
        return ret;
    }
    state->sched->createSlicingSchedule(zmin, zmax, state->spec->global.z_epsilon, ScheduleTwoPhotonSimple);
    if (state->sched->has_err) {
        state->err = state->sched->err;
        voidSlices3DSpecInfo(ret);
        return ret;
    }
    if (state->spec->global.fb.feedback) {
        double internal_to_input = state->factors->internal_to_input;

        //un-scale the z values for raw slices, since they are needed by applyFeedback()
        std::vector<double> rawZs = state->sched->rm.rawZs;
        for (auto z = rawZs.begin(); z != rawZs.end(); ++z) {
            *z *= internal_to_input;
        }

        std::string err = applyFeedback(*state->spec->global.config, *state->factors, *state->sched, rawZs, state->sched->rm.rawZs);
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
    try {
        state->sched->computeNextInputSlices();
        if (state->sched->has_err) {
            state->err = state->sched->err;
        }
    } catch (clp::clipperException &e) {
        state->err = handleClipperException(e);
    } catch (std::exception &e) {
        state->err = str("Unhandled exception: ", e.what());
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
    if (!single) {
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


typedef struct SharedLibraryPaths : public HasError {
    std::string filename;
    IOPaths iop;
    FileHeader fileheader;
    int currentRecord;
    std::vector<int> numpoints;
    std::vector<clp::cInt*> pathpointersi;
    std::vector<double*> pathpointersd;
    clp::Paths pathsi;
    DPaths pathsd;
    Paths3D pathsd3;
    SharedLibraryPaths(const char *_filename, FILE *f) : filename(_filename), iop(f), currentRecord(0) {}
    void clearPaths() {
        pathsi.clear();
        pathsd.clear();
        pathsd3.clear();
        numpoints.clear();
        pathpointersi.clear();
        pathpointersd.clear();
    }
    ~SharedLibraryPaths() {
        if (iop.f != NULL) fclose(iop.f);
    }
} SharedLibraryPaths;

LIBRARY_API  LoadPathFileInfo loadPathsFile(char *pathsfilename) {
    LoadPathFileInfo result;
    result.numRecords = result.ntools = -1;
    result.pathfile = NULL;

    FILE * file = fopen(pathsfilename, "rb");

    result.pathfile = new SharedLibraryPaths(pathsfilename, file);

    if (file == NULL) {
        result.pathfile->err = str("error while trying to open file ", pathsfilename);
        return result;
    }

    result.pathfile->err = result.pathfile->fileheader.readFromFile(file);
    result.numRecords = (int)result.pathfile->fileheader.numRecords;
    result.ntools = (int)result.pathfile->fileheader.numtools;

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
        if (!paths->iop.readDoublePaths(paths->pathsd))  { paths->err = str("In file ", paths->filename, ": could not read double paths in record ", paths->currentRecord, ", message: <", paths->iop.errs[0].message, "> in ", paths->iop.errs[0].function); out; };
        genericFillOutput(out, paths->pathsd, paths->numpoints, paths->pathpointersd);
    } else if (header.saveFormat == PATHFORMAT_DOUBLE_3D) {
        if (!read3DPaths(paths->iop, paths->pathsd3))    { paths->err = str("In file ", paths->filename, ": could not read 3d paths in record ", paths->currentRecord, ", message: <", paths->iop.errs[0].message, "> in ", paths->iop.errs[0].function); out; };
        genericFillOutput<LoadPathInfo, Point3D, double>(out, paths->pathsd3, paths->numpoints, paths->pathpointersd);
    }
    out.ntool = (int)header.ntool;
    out.type = (int)header.type;
    out.saveFormat = (int)header.saveFormat;
    out.scaling = header.scaling;
    out.z = header.z;

    ++(paths->currentRecord);

    return out;
}


#endif
