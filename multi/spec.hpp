#ifndef  SPEC_HEADER
#define  SPEC_HEADER

#include "config.hpp"
#include "motionPlanner.hpp"
#include "snapToGrid.hpp"
#include <memory>
#include <cmath>

/********************************************************
GLOBAL PARAMETERS
*********************************************************/

enum SchedulerMode { SimpleScheduler, UniformScheduling, ManualScheduling };

typedef struct GlobalSpec {
    typedef struct ZNTool { double z; unsigned int ntool; ZNTool() {}; ZNTool(double _z, unsigned int _ntool) : z(_z), ntool(_ntool) {} } ZNTool;
    //currently, having a reference to the Configuration here is useful only for debugging with showContours
    Configuration &config;
    SchedulerMode schedMode;
    bool useScheduler;
    bool addsubWorkflowMode;
    bool ignoreRedundantAdditiveContours; //TODO: make this an explicit parameter, if necessary (used only by 3d scheduling)
    bool alsoContours;
    bool applyMotionPlanner;
    bool avoidVerticalOverwriting;
    bool correct; //this is to correct the contour orientations (not needed if the input is from slic3r's adapted code)
    std::vector < ZNTool > schedSpec; //this is for manual specification of slices
    std::vector<int> schedTools; //this is for manual selection of tools for scheduling slices
    clp::cInt limitX, limitY;
    double z_uniform_step; //this parameter is the uniform step if useScheduler is false. Unlike most other metric parameters, this is in the mesh's native units!!!!
    double z_epsilon; //epsilon to consider that to Z values are the same.
    //not mean to be read from the command line (for internal use)
    clp::Paths inputSub; //this is used if flag "addsubWorkflowMode" is set
    bool substractiveOuter;
    clp::cInt outerLimitX, outerLimitY;
    GlobalSpec(Configuration &_config) : config(_config), inputSub(0), ignoreRedundantAdditiveContours(true) {}
} GlobalSpec;


/********************************************************
3D SCHEDULING-SPECIFIC STUFF
*********************************************************/

class VerticalProfile {
public:
    double sliceHeight; //vertical extent of a slice with this kind of voxel
    VerticalProfile(double sh) : sliceHeight(sh) {}
    //I would rather use an abstract method rather than one that throws an exception, but then I would not be able to use vectors of polymorphic pointers to this class
    virtual double getWidth(double zshift) { throw std::runtime_error("getWidth() unimplemented in base class!!!"); }
    //This is DIFFERENT from sliceHeight/2: it is the TRUE voxel extent, while sliceHeight may be adjusted for slicing purposes!!!!
    virtual double getVoxelSemiHeight() { throw std::runtime_error("getVoxelSemiHeight() unimplemented in base class!!!"); }
};

class ConstantProfile : public VerticalProfile {
    double radius, semiheight;
public:
    ConstantProfile(double r, double sh, double slh) : radius(r), semiheight(sh), VerticalProfile(slh) {}
    virtual double getWidth(double zshift) { return std::abs(zshift)<semiheight ? radius : 0.0; }
    virtual double getVoxelSemiHeight() { return semiheight; }
};

class EllipticalProfile : public VerticalProfile {
    double radiusX, radiusZ;
public:
    EllipticalProfile(double rx, double rz, double slh) : radiusX(rx), radiusZ(rz), VerticalProfile(slh) {}
    virtual double getWidth(double zshift) { return std::abs(zshift)<radiusZ ? radiusX*sqrt(1.0 - (zshift*zshift) / (radiusZ*radiusZ)) : 0.0; }
    virtual double getVoxelSemiHeight() { return radiusZ; }
};

typedef std::vector<std::shared_ptr<VerticalProfile>> VerticalProfilePolyVector;

/********************************************************
GLOBAL AND LOCAL PARAMETERS
*********************************************************/

enum InfillingMode { InfillingNone, InfillingJustContours, InfillingConcentric, InfillingRectilinearV, InfillingRectilinearH };

typedef struct MultiSpec {
    GlobalSpec global;
    StartState startState;
    //global required parameters (replicated for each resolution)
    size_t numspecs;

    //required parameters (replicated for each resolution)
    std::vector<clp::cInt> radiuses;              // radius of the tool
    std::vector<clp::cInt> gridsteps;             // grid step of the tool. If used (to snap to grid, mainly), this is expected to be significantly smaller than the radius
    std::vector<clp::cInt> arctolRs;              // arcTolerance when doing offseting at the radius scale
    std::vector<clp::cInt> arctolGs;              // arcTolerance when doing offseting at the gridstep scale
    std::vector<clp::cInt> burrLengths;           // radius to remove too small details (applied when no snap is done)
    std::vector<clp::cInt> radiusesRemoveCommon;  // radius to remove shared arcs between contours of different resolutions (applying this in the current, naive way may become quite expensive)
    std::vector<bool>      applysnaps;            // flag to snap to grid
    std::vector<bool>      snapSmallSafeSteps;    // flag to use a small safeStep if snapping to grid
    std::vector<bool>      addInternalClearances; // make sure that the toolpath is smooth enough to not write over itself
    std::vector<std::vector<double>> medialAxisFactors; //list of medialAxis factors, each list should be strictly decreasing
    std::vector<std::vector<double>> medialAxisFactorsForInfillings; //list of medialAxis factors, each list should be strictly decreasing
    std::vector<InfillingMode> infillingModes;    //how to deal with infillings
    std::vector<bool> infillingWhole;             //if infilling is rectilinear, this flag decides if the lines are applied per region (slow, but useful for narrow regions), or to the whole contour
    std::vector<bool> infillingRecursive;         //flag to decide if non-filled regions inside infillings will be added to the list of contours, to try to fill them with medial axis and/or higher resolution processes

    VerticalProfilePolyVector profiles;

    //default/derived paramenters (replicated for each resolution)
    std::vector<double>    substeps;
    std::vector<double>    dilatesteps;
    std::vector<double>    safesteps;
    std::vector<double>    maxdists;
    std::vector<double>    gridstepsX;
    std::vector<double>    gridstepsY;
    std::vector<double>    shiftsX;
    std::vector<double>    shiftsY;
    std::vector<bool>      useRadiusesRemoveCommon;
    std::vector<bool>      higherProcessUsesRadiusesRemoveCommon;

    std::vector<SnapToGridSpec> snapspecs;

    bool anyUseRadiusesRemoveCommon;

    MultiSpec(GlobalSpec _global, size_t n = 0) : global(std::move(_global)), anyUseRadiusesRemoveCommon(false) { if (n>0) initializeVectors(n); }

    void initializeVectors(size_t n);
    bool validate();
    bool inline useContoursAlreadyFilled(int k) { return (k > 0) && (!global.addsubWorkflowMode) && (useRadiusesRemoveCommon[k]); }
    std::string populateParameters();
} MultiSpec;


#endif
