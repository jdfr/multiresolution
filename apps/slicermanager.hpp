#ifndef SLICERMANAGER_HEADER
#define SLICERMANAGER_HEADER

#include <stdexcept>
#include "common.hpp"
#include "config.hpp"

//functionally abstract class
class SlicerManager {
public:
    virtual ~SlicerManager() {}
    virtual bool start(const char * stlfilename) { throw std::runtime_error("start not implemented!!!"); }
    virtual bool finalize() { throw std::runtime_error("finalize not implemented!!!"); }
    virtual std::string getErrorMessage() { throw std::runtime_error("getErrorMessage not implemented!!!"); }
    virtual void getZLimits(double *minz, double *maxz) { throw std::runtime_error("getZLimits not implemented!!!"); }
    virtual void sendZs(double *values, int numvalues) { throw std::runtime_error("sendZs not implemented!!!"); }
    virtual std::vector<double> prepareSTLSimple(double zmin, double zstep) { throw std::runtime_error("prepareSTLSimple not implemented!!!"); }
    virtual std::vector<double> prepareSTLSimple(double zstep) { throw std::runtime_error("prepareSTLSimple not implemented!!!"); }
    virtual int  askForNextSlice() { throw std::runtime_error("askForNextSlice not implemented!!!"); }
    virtual void readNextSlice(clp::Paths &nextSlice) { throw std::runtime_error("readNextSlice not implemented!!!"); }
};

enum SlicerManagerType {
    SlicerManagerExternal,
    SlicerManagerNative
};

SlicerManager *getSlicerManager(Configuration &config, SlicerManagerType type);

#endif