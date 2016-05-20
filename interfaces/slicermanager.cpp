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
    std::vector<double> values;
    double scaled;
    double scalingFactor;
    double z_for_last_slice;
    long   scale;
    int    numSlice;
    bool   useIntegerScale;
    bool   repair, incremental;
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
    virtual bool sendZs(std::vector<double> _values);
    virtual bool readNextSlice(clp::Paths &nextSlice);
    virtual double getZForPreviousSlice() { return z_for_last_slice; }
    virtual bool reachedEnd() { return numSlice >= values.size(); }
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

bool ExternalSlicerManager::sendZs(std::vector<double> _values) {
    values    = std::move(_values);
    int64 num = (int64)values.size();
    if (!iopIN.writeInt64(num)) {
        err = "could not write number of Z values to the slicer!!!";
        return false;
    }
    if (!iopIN.writeDoubleP(&values.front(), values.size())) {
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
    z_for_last_slice = values[numSlice];
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
    ++numSlice;
    return true;
}

bool ExternalSlicerManager::skipNextSlices(int numSkip) {
    for (int i = 0; i < numSkip; ++i) {
        clp::Paths dummy;
        if (!readNextSlice(dummy)) return false;
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
    double z_for_last_slice;
    int64 pathTypeValue;
    std::string err;
    int numSlice, numRead, numRecords;
    bool metadataRequired;
    bool zsHaveBeenSent;
    bool filterByPathType;
public:
    RawSlicerManager(double _epsilon, bool _metadataRequired, bool _filterByPathType, int64 _pathTypeValue) : f(NULL), epsilon(_epsilon), pathTypeValue(_pathTypeValue), metadataRequired(_metadataRequired), filterByPathType(_filterByPathType) {}
    virtual ~RawSlicerManager() { finalize(); }
    virtual bool start(const char * stlfilename);
    virtual bool terminate() { return finalize(); }
    virtual bool finalize();
    virtual std::string getErrorMessage() { return err; }
    virtual void getLimits(double *minx, double *maxx, double *miny, double *maxy, double *minz, double *maxz);
    virtual double getScalingFactor() { return scalingFactor; }
    virtual bool sendZs(std::vector<double> values);
    virtual bool readNextSlice(clp::Paths &nextSlice);
    virtual double getZForPreviousSlice() { return z_for_last_slice; }
    virtual bool reachedEnd() { return numRead >= numRecords; }
    virtual bool skipNextSlices(int numSkip);
};

bool RawSlicerManager::start(const char * fname) {
    numSlice = numRead = 0;
    zsHaveBeenSent = false;
    
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
    
    numRecords = fileheader.numRecords;

    iop_f = IOPaths(f);

    if (metadataRequired) {
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
    } else {
        minx = maxx = miny = maxy = minz = maxz = scalingFactor = 0.0;
    }

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

bool RawSlicerManager::sendZs(std::vector<double> values) {
    expectedzs     = std::move(values);
    zsHaveBeenSent = true;
    return true;
}

bool RawSlicerManager::readNextSlice(clp::Paths &nextSlice) {
    SliceHeader sliceheader;
    
    while (true) {
        std::string e = sliceheader.readFromFile(f);
        
        if (numRead >= numRecords)          { err = str("Error: trying to read more records than the file declared to have"); return false; }
        if (!e.empty())                     { err = str("Error reading ", numSlice, "-th slice header from ", filename, ": ", err); return false; }
        if (sliceheader.alldata.size() < 7) { err = str("Error reading ", numSlice, "-th slice header from ", filename, ": header is too short!"); return false; }
        if (zsHaveBeenSent) {
            if (std::fabs(sliceheader.z - expectedzs[numSlice]) > epsilon) {
                err = str("Error reading ", numSlice, "-th slice header from ", filename, ": z is ", sliceheader.z, "but it was expected to be ", expectedzs[numSlice]);
                return false;
            }
        }
        
        if (!filterByPathType || (sliceheader.type == pathTypeValue)) {
            break;
        }
        fseek(f, (long)(sliceheader.totalSize - sliceheader.headerSize), SEEK_CUR);
        ++numRead;
    }
    
    z_for_last_slice = sliceheader.z;
    
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
    ++numRead;
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

std::shared_ptr<SlicerManager> getRawSlicerManager(double z_epsilon, bool metadataRequired, bool filterByPathType, int64 pathTypeValue) {
    return std::make_shared<RawSlicerManager>(z_epsilon, metadataRequired, filterByPathType, pathTypeValue);
}
