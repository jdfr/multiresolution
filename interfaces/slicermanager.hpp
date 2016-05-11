#ifndef SLICERMANAGER_HEADER
#define SLICERMANAGER_HEADER

#include "common.hpp"
#include "config.hpp"
#include "pathsfile.hpp"
#include <stdexcept>
#include <memory>

//functionally abstract class
class SlicerManager {
public:
    virtual ~SlicerManager() {}
    virtual bool start(const char * stlfilename) { throw std::runtime_error("start not implemented!!!"); }
    virtual bool terminate() { throw std::runtime_error("terminate not implemented!!!"); }
    virtual bool finalize() { throw std::runtime_error("finalize not implemented!!!"); }
    virtual std::string getErrorMessage() { throw std::runtime_error("getErrorMessage not implemented!!!"); }
    virtual void getLimits(double *minx, double *maxx, double *miny, double *maxy, double *minz, double *maxz) { throw std::runtime_error("getZLimits not implemented!!!"); }
    virtual double getScalingFactor() { throw std::runtime_error("getScalingFactor not implemented!!!"); }
    virtual bool sendZs(double *values, int numvalues) { throw std::runtime_error("sendZs not implemented!!!"); }
    virtual bool readNextSlice(clp::Paths &nextSlice) { throw std::runtime_error("readNextSlice not implemented!!!"); }
    virtual bool skipNextSlices(int numSkip) { throw std::runtime_error("readNextSlice not implemented!!!"); }
};

std::vector<double> prepareSTLSimple(double zmin, double zmax, double zbase, double zstep);
std::vector<double> prepareSTLSimple(double zmin, double zmax, double zstep);

std::shared_ptr<SlicerManager> getExternalSlicerManager(Configuration &config, MetricFactors &factors, std::string DEBUG_FILE_NAME, std::string postfix);
std::shared_ptr<SlicerManager> getRawSlicerManager(double z_epsilon);

void prepareRawFileHeader(FileHeader &header, double scalingFactor, double minx, double maxx, double miny, double maxy, double minz, double maxz);

#endif