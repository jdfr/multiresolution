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
    std::string postfix;
    std::string execpath;
    std::string workdir;
    std::string err;
    bool repair, incremental;
    double scaled;
    double scalingFactor;
    long scale;
    bool useIntegerScale;
    int numSlice;
public:
    ExternalSlicerManager(
#ifdef SLICER_USE_DEBUG_FILE
        std::string _debugfile,
#endif
    std::string _postfix, std::string _execpath, std::string _workdir, bool _repair, bool _incremental, double _scaled) :
#ifdef SLICER_USE_DEBUG_FILE
    debugfile(std::move(_debugfile)),
#endif
    subp(true, true), postfix(_postfix), execpath(std::move(_execpath)), workdir(std::move(_workdir)), repair(_repair), incremental(_incremental), scaled(_scaled), scale((long)_scaled), useIntegerScale(((double)scale)==scaled), scalingFactor(0.0) {}
    virtual ~ExternalSlicerManager() { finalize(); }
    virtual bool start(const char * stlfilename);
    virtual bool terminate();
    virtual bool finalize();
    virtual std::string getErrorMessage() { return err; }
    virtual void getLimits(double *minx, double *maxx, double *miny, double *maxy, double *minz, double *maxz);
    virtual double getScalingFactor();
    virtual bool sendZs(double *values, int numvalues);
    virtual bool readNextSlice(clp::Paths &nextSlice);
    virtual bool skipNextSlices(int numSkip);
};

bool ExternalSlicerManager::start(const char * stlfilename) {
    if (subp.started()) { return false; }
    numSlice = 0;
    subp.execpath = execpath;
    subp.workdir = workdir;
    subp.args.clear();
#ifdef SLICER_USE_DEBUG_FILE
    if (debugfile.empty()) {
        debugfile = "slicerlog.standalone";
    }
    subp.args.push_back(str(debugfile, ".txt"));
#endif
    subp.args.push_back(std::string(repair ? "repair" : "norepair"));
    subp.args.push_back(std::string(incremental ? "incremental" : "noincremental"));
    subp.args.push_back(stlfilename);
    
    if (!postfix.empty()) subp.exename += postfix;

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
    double limits[7];
    if (!iopOUT.readDoubleP(limits, 7)) {
        err = "could not read min/max values from the slicer!!!";
        return;
    }
    *minx = limits[0];
    *maxx = limits[1];
    *miny = limits[2];
    *maxy = limits[3];
    *minz = limits[4];
    *maxz = limits[5];
    scalingFactor = limits[6];
}

double ExternalSlicerManager::getScalingFactor() {
    return scalingFactor;
}

bool ExternalSlicerManager::sendZs(double *values, int numvalues) {
    int64 num = numvalues;
    if (!iopIN.writeInt64(num)) {
        err = "could not write number of Z values to the slicer!!!";
        return false;
    }
    if (!iopIN.writeDoubleP(values, num)) {
        err = "could not write Z values to the slicer!!!";
        return false;
    }
    fflush(subp.pipeIN);
    return true;
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

bool ExternalSlicerManager::readNextSlice(clp::Paths &nextSlice) {
    ++numSlice;
    if (!iopOUT.readClipperPaths(nextSlice)) {
        err = "Could not read slice from slicer!!!";
        return false;
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
    return true;
}

bool ExternalSlicerManager::skipNextSlices(int numSkip) {
    for (int i = 0; i < numSkip; ++i) {
        clp::Paths dummy;
        readNextSlice(dummy);
    }
    return true;
}

std::shared_ptr<SlicerManager> getExternalSlicerManager(Configuration &config, MetricFactors &factors, std::string DEBUG_FILE_NAME, std::string postfix) {
    DEBUG_FILE_NAME += postfix;
    return std::make_shared<ExternalSlicerManager>(
#ifdef SLICER_USE_DEBUG_FILE
        std::move(DEBUG_FILE_NAME),
#endif
        std::move(postfix),
        config.getValue("SLICER_EXEC"),
        config.getValue("SLICER_PATH"),
        config.getValue("SLICER_REPAIR").compare("true") == 0,
        config.getValue("SLICER_INCREMENTAL").compare("true") == 0,
        factors.slicer_to_internal);
}







#define RAW_MAGIC_NUMBER ((clp::cInt)0x4154414453574152) //"RAWSDATA", little endian

void prepareRawFileHeader(FileHeader &header, double scalingFactor, double minx, double maxx, double miny, double maxy, double minz, double maxz) {
    if (header.version == 0) header.version = 1;
    header.additional.clear();
    header.additional.reserve(8);
    header.additional.push_back(RAW_MAGIC_NUMBER);
    header.additional.push_back(scalingFactor);
    header.additional.push_back(minx);
    header.additional.push_back(maxx);
    header.additional.push_back(miny);
    header.additional.push_back(maxy);
    header.additional.push_back(minz);
    header.additional.push_back(maxz);
}


class RawSlicerManager : public SlicerManager {
    FILE * f;
    std::string filename;
    IOPaths iop_f;
    std::vector<double> expectedzs;
    double epsilon, scalingFactor, minx, maxx, miny, maxy, minz, maxz;
    std::string err;
    int numSlice;
public:
    RawSlicerManager(double _epsilon) : epsilon(_epsilon), f(NULL) {}
    virtual ~RawSlicerManager() { finalize(); }
    virtual bool start(const char * stlfilename);
    virtual bool terminate() { return finalize(); }
    virtual bool finalize();
    virtual std::string getErrorMessage() { return err; }
    virtual void getLimits(double *minx, double *maxx, double *miny, double *maxy, double *minz, double *maxz);
    virtual double getScalingFactor() { return scalingFactor; }
    virtual bool sendZs(double *values, int numvalues);
    virtual bool readNextSlice(clp::Paths &nextSlice);
    virtual bool skipNextSlices(int numSkip);
};

bool RawSlicerManager::start(const char * fname) {
    numSlice = 0;
    
    filename = fname;
    
    f = fopen(fname, "rb");
    if (f == NULL) {
        err = str("Could not open input file ", fname);
        return false;
    }
    
    FileHeader fileheader;
    err = fileheader.readFromFile(f);
    if (!err.empty()) { 
        fclose(f);
        f = NULL;
        err = str("Error reading file header for ", fname, ": ", err);
        return false;
    }
    
    if ((fileheader.additional.size()<8) || (fileheader.additional[0].i != RAW_MAGIC_NUMBER)) {
        fclose(f);
        f = NULL;
        err = str("Error, incorrect metadata's magic number in file ", fname);
        return false;
    }
    
    scalingFactor = fileheader.additional[1].d;
    minx          = fileheader.additional[2].d;
    maxx          = fileheader.additional[3].d;
    miny          = fileheader.additional[4].d;
    maxy          = fileheader.additional[5].d;
    minz          = fileheader.additional[6].d;
    maxz          = fileheader.additional[7].d;

    iop_f = IOPaths(f);
    
    return err.empty();
}

bool RawSlicerManager::finalize() {
    if (f != NULL) {
        fclose(f);
        f = NULL;
    }
    return true;
}

void RawSlicerManager::getLimits(double *minx, double *maxx, double *miny, double *maxy, double *minz, double *maxz) {
    *minx = this->minx;
    *maxx = this->maxx;
    *miny = this->miny;
    *maxy = this->maxy;
    *minz = this->minz;
    *maxz = this->maxz;
}

bool RawSlicerManager::sendZs(double *values, int numvalues) {
    expectedzs.resize(numvalues);
    auto expected = &expectedzs.front();
    for (int i = 0; i < numvalues; ++i) {
        *expected = *values;
        ++expected;
        ++values;
    }
    return true;
}

bool RawSlicerManager::readNextSlice(clp::Paths &nextSlice) {
    SliceHeader sliceheader;
    std::string e = sliceheader.readFromFile(f);
    
    if (!e.empty())                     { err = str("Error reading ", numSlice, "-th slice header from ", filename, ": ", err); return false; }
    if (sliceheader.alldata.size() < 7) { err = str("Error reading ", numSlice, "-th slice header from ", filename, ": header is too short!"); return false; }
    if (std::fabs(sliceheader.z - expectedzs[numSlice]) > epsilon) {
        err = str("Error reading ", numSlice, "-th slice header from ", filename, ": z is ", sliceheader.z, "but it was expected to be ", expectedzs[numSlice]);
        return false;
    }
    
    nextSlice.clear();
    if (sliceheader.saveFormat == PATHFORMAT_INT64) {
        if (!iop_f.readClipperPaths(nextSlice)) {
            err = str("Error reading ", numSlice, "-th integer clipperpaths from file ", filename);
            return false;
        }
    } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE) {
        if (!iop_f.readDoublePaths(nextSlice, 1 / sliceheader.scaling)) {
            err = str("Error reading ", numSlice, "-th double clipperpaths from file ", filename);
            return false;
        }
    } else {
        err = str("Error: unsupported clipperpaths format in RawSlicerManager for file ", filename, ": ", sliceheader.saveFormat);
        return false;
    }
    
    //remove last point from each path, which was added in writeClipperPaths() because contours have their first point also added as the last one
    for (auto &path : nextSlice) if (!path.empty()) path.resize(path.size()-1);
    
    ++numSlice;
    return true;
}

bool RawSlicerManager::skipNextSlices(int numSkip) {
    int64 totalSize;
    for (int i = 0; i < numSkip; ++i) {
        if (fread(&totalSize,  sizeof(totalSize),  1, f) != 1) return false;
        long toSkip = (long)(totalSize - sizeof(totalSize));
        if (toSkip>0) if (fseek(f, toSkip, SEEK_CUR)!=0) return false;
    }
    return true;
}

std::shared_ptr<SlicerManager> getRawSlicerManager(double z_epsilon) {
    return std::make_shared<RawSlicerManager>(z_epsilon);
}
