#include "slicermanager.hpp"
#include "subprocess.hpp"
#include "iopaths.hpp"
#include <cmath>

/*strictly speaking, spawning a different process for the slicer is only required if
we are compiling under MSVS (as slic3r does not compile in MSVS). As the overhead
seems to be fairly small, we keep it this way for all platforms.*/
class ExternalSlicerManager : public SlicerManager {
    SubProcessManager subp;
    IOPaths iopIN, iopOUT;
#ifdef SLICER_USE_DEBUG_FILE
    std::string debugfile;
#endif
    std::string execpath;
    std::string workdir;
    std::string err;
    bool repair, incremental;
    double scaled;
    long scale;
    bool useIntegerScale;
    int numSlice;
public:
    ExternalSlicerManager(
#ifdef SLICER_USE_DEBUG_FILE
        std::string &&_debugfile,
#endif
    std::string &&_execpath, std::string &&_workdir, bool _repair, bool _incremental, double _scaled) :
#ifdef SLICER_USE_DEBUG_FILE
    debugfile(std::move(_debugfile)),
#endif
    subp(true, true), execpath(std::move(_execpath)), workdir(std::move(_workdir)), repair(_repair), incremental(_incremental), scaled(_scaled), scale((long)_scaled), useIntegerScale(((double)scale)==scaled) {}
    virtual ~ExternalSlicerManager() { finalize(); }
    virtual bool start(const char * stlfilename);
    virtual bool terminate();
    virtual bool finalize();
    virtual std::string getErrorMessage() { return err; }
    virtual void getLimits(double *minx, double *maxx, double *miny, double *maxy, double *minz, double *maxz);
    virtual void sendZs(double *values, int numvalues);
    virtual int  askForNextSlice();
    virtual void readNextSlice(clp::Paths &nextSlice);
};

bool ExternalSlicerManager::start(const char * stlfilename) {
    if (subp.started()) { return false; }
    numSlice = 0;
    subp.execpath = execpath;
    subp.workdir = workdir;
    subp.args.clear();
#ifdef SLICER_USE_DEBUG_FILE
    if (debugfile.empty()) {
        debugfile = "slicerlog.standalone.txt";
    }
    subp.args.push_back(debugfile);
#endif
    subp.args.push_back(std::string(repair ? "repair" : "norepair"));
    subp.args.push_back(std::string(incremental ? "incremental" : "noincremental"));
    subp.args.push_back(stlfilename);

    err = subp.start();

    iopIN .f = subp.pipeIN;
    iopOUT.f = subp.pipeOUT;

    return err.empty();
}

bool ExternalSlicerManager::terminate() {
    subp.kill();
    return true;
}

bool ExternalSlicerManager::finalize() {
    subp.wait();
    return true;
}

void ExternalSlicerManager::getLimits(double *minx, double *maxx, double *miny, double *maxy, double *minz, double *maxz) {
    if (!repair) {
        int64 need_repair;
        if (!iopOUT.readInt64(need_repair)) {
            err = "could not read need_repair value from the slicer!!!";
            return;
        }
        if (need_repair != 0) {
            err = "The STL needs to be repaired!";
            return;
        }
    }
    double limits[6];
    if (!iopOUT.readDoubleP(limits, 6)) {
        err = "could not read min/max values from the slicer!!!";
        return;
    }
    *minx = limits[0];
    *maxx = limits[1];
    *miny = limits[2];
    *maxy = limits[3];
    *minz = limits[4];
    *maxz = limits[5];
}

void ExternalSlicerManager::sendZs(double *values, int numvalues) {
    int64 num = numvalues;
    if (!iopIN.writeInt64(num)) {
        err = "could not write number of Z values to the slicer!!!";
        return;
    }
    if (!iopIN.writeDoubleP(values, num)) {
        err = "could not write Z values to the slicer!!!";
        return;
    }
    fflush(subp.pipeIN);
}

std::vector<double> prepareSTLSimple(double zmin, double zmax, double zbase, double zstep) {
    double zend = (zstep > 0) ? zmax : zmin;
    std::vector<double> zs;
    int numneeded = (int)((zend - zbase) / zstep);
    if (numneeded > 0) {
        zs.reserve(numneeded + 2);
        for (double j = zbase; j <= zmax; j += zstep) {
            zs.push_back(j);
        }
    }
    return zs;
}

std::vector<double> prepareSTLSimple(double zmin, double zmax, double zstep) {
    std::vector<double> zs;
    zs.reserve((int)((zmax - zmin) / std::abs(zstep)) + 2);
    if (zstep > 0) {
        for (double j = zmin + zstep / 2; j <= zmax; j += zstep) zs.push_back(j);
    } else {
        for (double j = zmax + zstep / 2; j >= zmin; j += zstep) zs.push_back(j);
    }
    return zs;
}

int ExternalSlicerManager::askForNextSlice() {
    return numSlice++;
}


void ExternalSlicerManager::readNextSlice(clp::Paths &nextSlice) {
    askForNextSlice();
    if (!iopOUT.readPrefixedClipperPaths(nextSlice)) {
        err = "Could not read slice from slicer!!!";
        return;
    }
    if (scale != 0) {
        if (useIntegerScale) {
            for (auto &path : nextSlice) for (auto &point : path) {
                point.X *= scale;
                point.Y *= scale;
            }
        } else {
            for (auto &path : nextSlice) for (auto &point : path) {
                point.X = (clp::cInt)(point.X * scaled);
                point.Y = (clp::cInt)(point.Y * scaled);
            }
        }
    }
}

std::shared_ptr<SlicerManager> getSlicerManager(Configuration &config, MetricFactors &factors, SlicerManagerType type) {
    switch (type) {

    case SlicerManagerExternal: {

        return std::make_shared<ExternalSlicerManager>(
#ifdef SLICER_USE_DEBUG_FILE
            config.getValue("SLICER_DEBUGFILE"),
#endif
            config.getValue("SLICER_EXEC"),
            config.getValue("SLICER_PATH"),
            config.getValue("SLICER_REPAIR").compare("true") == 0,
            config.getValue("SLICER_INCREMENTAL").compare("true") == 0,
            factors.slicer_to_internal);

    } case SlicerManagerNative:
    default:
        //not implemented yet
        return std::shared_ptr<ExternalSlicerManager>();
    }
}
