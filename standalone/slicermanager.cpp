#include "slicermanager.hpp"

#include "subprocess.hpp"

#include "iopaths.hpp"

#include <sstream>

#include <stdio.h>

/*strictly speaking, spawning a different process for the slicer is only required if
we are compiling under MSVS (as slic3r does not compile in MSVS). As the overhead
seems to be fairly small, we keep it this way for all platforms.*/
class ExternalSlicerManager : public SlicerManager {
    SubProcessManager subp;
    std::string execpath;
    std::string workdir;
    std::string err;
    bool repair, incremental;
    long scale;
    int numSlice;
public:
    ExternalSlicerManager(std::string &&_execpath, std::string &&_workdir, bool _repair, bool _incremental, long _scale) : subp(true, true), execpath(std::move(_execpath)), workdir(std::move(_workdir)), repair(_repair), incremental(_incremental), scale(_scale) {}
    virtual ~ExternalSlicerManager() { finalize(); }
    virtual bool start(const char * stlfilename);
    virtual bool finalize();
    virtual std::string getErrorMessage() { return err; }
    virtual void getZLimits(double *minz, double *maxz);
    virtual void sendZs(double *values, int numvalues);
    virtual std::vector<double> prepareSTLSimple(double zmin, double zstep);
    virtual std::vector<double> prepareSTLSimple(double zstep);
    virtual int  askForNextSlice();
    virtual void readNextSlice(clp::Paths &nextSlice);
};

bool ExternalSlicerManager::start(const char * stlfilename) {
    if (subp.started()) { return false; }
    numSlice = 0;
    subp.execpath = execpath;
    subp.workdir = workdir;
    subp.args.clear();
    subp.args.push_back(std::string(repair ? "repair" : "norepair"));
    subp.args.push_back(std::string(incremental ? "incremental " : "noincremental "));
    subp.args.push_back(stlfilename);

    err = subp.start();

    return err.empty();
}

bool ExternalSlicerManager::finalize() {
    subp.wait();
    return true;
}

void ExternalSlicerManager::getZLimits(double *minz, double *maxz) {
    if (!repair) {
        int64 need_repair;
        READ_BINARY(&need_repair, sizeof(need_repair), 1, subp.pipeOUT);
        if (need_repair != 0) {
            std::runtime_error("The STL needs to be repaired!");
        }
    }
    READ_BINARY(minz, sizeof(double), 1, subp.pipeOUT);
    READ_BINARY(maxz, sizeof(double), 1, subp.pipeOUT);
}

void ExternalSlicerManager::sendZs(double *values, int numvalues) {
    int64 num = numvalues;
    WRITE_BINARY(&num,   sizeof(num),            1, subp.pipeIN);
    WRITE_BINARY(values, sizeof(double), numvalues, subp.pipeIN);
    fflush(subp.pipeIN);
}

std::vector<double> ExternalSlicerManager::prepareSTLSimple(double zmin, double zstep) {
    double nevermind = 0, maxz = 0;
    getZLimits(&nevermind, &maxz);
    std::vector<double> zs;
    zs.reserve((int)((maxz - zmin) / zstep) + 2);
    for (double j = zmin; j <= maxz; j += zstep) {
        zs.push_back(j);
    }
    sendZs(&(zs[0]), (int)zs.size());
    return zs;
}

std::vector<double> ExternalSlicerManager::prepareSTLSimple(double zstep) {
    double zmin = 0, maxz = 0;
    getZLimits(&zmin, &maxz);
    std::vector<double> zs;
    zs.reserve((int)((maxz - zmin) / zstep) + 2);
    for (double j = zmin+zstep/2; j <= maxz; j += zstep) {
        zs.push_back(j);
    }
    sendZs(&(zs[0]), (int)zs.size());
    return zs;
}

int ExternalSlicerManager::askForNextSlice() {
    return numSlice++;
}


void ExternalSlicerManager::readNextSlice(clp::Paths &nextSlice) {
    askForNextSlice();
    readPrefixedClipperPaths(subp.pipeOUT, nextSlice);
    if (scale != 0) {
        auto paend = nextSlice.end();
        for (auto path = nextSlice.begin(); path != paend; ++path) {
            auto poend = path->end();
            for (auto point = path->begin(); point != poend; ++point) {
                point->X *= scale;
                point->Y *= scale;
            }
        }
    }
}

SlicerManager *getSlicerManager(Configuration &config, SlicerManagerType type) {
    switch (type) {

    case SlicerManagerExternal: {

        std::string scale = config.getValue("SLICER_TO_INTERNAL_FACTOR");
        long sc = strtol(scale.c_str(), NULL, 10);

        return new ExternalSlicerManager(
            config.getValue("SLICER_EXEC"),
            config.getValue("SLICER_PATH"),
            config.getValue("SLICER_REPAIR").compare("true") == 0,
            config.getValue("SLICER_INCREMENTAL").compare("true") == 0,
            sc);

    } case SlicerManagerNative:
    default:
        //not implemented yet
        return NULL;
    }
}
