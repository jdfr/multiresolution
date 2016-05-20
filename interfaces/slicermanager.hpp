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
    virtual bool start(const char * stlfilename) = 0;
    virtual bool terminate() = 0;
    virtual bool finalize() = 0;
    virtual std::string getErrorMessage() = 0;
    virtual void getLimits(double *minx, double *maxx, double *miny, double *maxy, double *minz, double *maxz) = 0;
    virtual double getScalingFactor() = 0;
    virtual bool sendZs(std::vector<double> values) = 0;
    virtual bool readNextSlice(clp::Paths &nextSlice) = 0;
    virtual double getZForPreviousSlice() = 0;
    virtual bool reachedEnd() = 0;
    virtual bool skipNextSlices(int numSkip) = 0;
};

std::vector<double> prepareSTLSimple(double zmin, double zmax, double zbase, double zstep);
std::vector<double> prepareSTLSimple(double zmin, double zmax, double zstep);

std::shared_ptr<SlicerManager> getExternalSlicerManager(Configuration &config, MetricFactors &factors, std::string DEBUG_FILE_NAME, std::string postfix);
std::shared_ptr<SlicerManager> getRawSlicerManager(double z_epsilon, bool metadataRequired = true, bool filterByPathType = false, int64 pathTypeValue = 0);

void prepareRawFileHeader(FileHeader &header, double scalingFactor, double minx, double maxx, double miny, double maxy, double minz, double maxz);

#endif