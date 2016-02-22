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

typedef struct FeedbackSpec {
    bool feedback;
    bool feedbackMesh;
    std::string feedbackFile;
    FeedbackSpec() : feedback(false) {}
} FeedbackSpec;

//in add/sub mode, "contour fattening" should be computed for high-res features in the first process. This "fattening" can be done in several ways:
//    -by specifying the medialAxisFactors vector of that process to have one or more very low values. For this, flag useGradualFattening should be false
//    -by a gradual fattening, setting flag useGradualFattening to true
//    -for high-res features rich in narrow negative details (such as gratings), setting the flag erasegHighResNegDetails may be useful. This can be used on its own, or combined with any of the two previous methods.
typedef struct FatteningSpec {
    typedef struct GradualStep { double radiusFactor; double inflateFactor; GradualStep() {}; GradualStep(double rad, double inf) : radiusFactor(rad), inflateFactor(inf) {} } GradualStep;
    bool eraseHighResNegDetails;
    bool useGradualFattening;
    clp::cInt eraseHighResNegDetails_radius;
    std::vector<GradualStep> gradual;
} FatteningSpec;

typedef struct AddSubSpec {
    bool addsubWorkflowMode;
    bool ignoreRedundantAdditiveContours; //TODO: make this an explicit parameter, if necessary (used only by 3d scheduling)
    //IMPORTANT: In our current setup, the add/sub workflow is always:
    //           -process 0 of FIRST kind (add or sub)
    //           -all subsequent processes of the OPPOSITE kind
    //because of this, we host all specifications relative the FIRST kind here (there is only one, and it is always the 0th!), but if we were to enable more processes of the FIRST kind, these specifications would probably have to be moved to PerProcessSpec
    FatteningSpec fattening;
    AddSubSpec() : ignoreRedundantAdditiveContours(true) {}
} AddSubSpec;

typedef struct GlobalSpec {
    typedef struct ZNTool { double z; unsigned int ntool; ZNTool() {}; ZNTool(double _z, unsigned int _ntool) : z(_z), ntool(_ntool) {} } ZNTool;
    //currently, having a reference to the Configuration here is useful only for debugging with showContours
    std::shared_ptr<Configuration> config;
    SchedulerMode schedMode;
    FeedbackSpec fb;
    AddSubSpec addsub;
    bool useScheduler;
    bool sliceUpwards; //if slicing is not manual, this sets the direction of the slicing (if true: from bottom to top)
    bool alsoContours;
    bool applyMotionPlanner;
    bool avoidVerticalOverwriting;
    bool correct; //this is to correct the contour orientations (not needed if the input is from slic3r's adapted code)
    std::vector < ZNTool > schedSpec; //this is for manual specification of slices
    std::vector<int> schedTools; //this is for manual selection of tools for scheduling slices
    clp::cInt limitX, limitY;
    bool use_z_base;
    double z_base; //when scheduling mode is uniform: if this parameter is not NaN, it represents the position of the first slice
    double z_uniform_step; //this parameter is the uniform step if useScheduler is false. Unlike most other metric parameters, this is in the mesh's native units!!!!
    double z_epsilon; //epsilon to consider that to Z values are the same.
    //not mean to be read from the command line (for internal use)
    bool substractiveOuter;
    clp::cInt outerLimitX, outerLimitY;
    GlobalSpec(std::shared_ptr<Configuration> _config) : config(std::move(_config)) {}
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

/********************************************************
GLOBAL AND LOCAL PARAMETERS
*********************************************************/

enum InfillingMode { InfillingNone, InfillingJustContours, InfillingConcentric, InfillingRectilinearV, InfillingRectilinearH };

typedef struct PerProcessSpec {
    //required parameters (replicated for each resolution)
    clp::cInt radius;                // radius of the tool
    clp::cInt gridstep;              // grid step of the tool. If used (to snap to grid, mainly), this is expected to be significantly smaller than the radius
    clp::cInt arctolR;               // arcTolerance when doing offseting at the radius scale
    clp::cInt arctolG;               // arcTolerance when doing offseting at the gridstep scale
    clp::cInt burrLength;            // radius to remove too small details (applied when no snap is done)
    clp::cInt radiusRemoveCommon;    // radius to remove shared arcs between contours of different resolutions (applying this in the current, naive way may become quite expensive)
    bool      computeToolpaths;      // flag to effectively compute the toolpaths (alternative: only contours, without taking into account toolpath smoothing effects)
    bool      applysnap;             // flag to snap to grid
    bool      snapSmallSafeStep;     // flag to use a small safeStep if snapping to grid
    bool      addInternalClearance;  // make sure that the toolpath is smooth enough to not write over itself
    std::vector<double> medialAxisFactors; //list of medialAxis factors, each list should be strictly decreasing
    std::vector<double> medialAxisFactorsForInfillings; //list of medialAxis factors, each list should be strictly decreasing
    InfillingMode infillingMode;     //how to deal with infillings
    bool infillingWhole;             //if infilling is rectilinear, this flag decides if the lines are applied per region (slow, but useful for narrow regions), or to the whole contour
    bool infillingRecursive;         //flag to decide if non-filled regions inside infillings will be added to the list of contours, to try to fill them with medial axis and/or higher resolution processes
    bool doPreprocessing;            //flag to decide if preprocessing may be applied
    double noPreprocessingOffset;    //if no preprocessing is done, a morphological opening is done with this value

    std::shared_ptr<VerticalProfile> profile;

    //default/derived paramenters (replicated for each resolution)
    double    substep;
    double    dilatestep;
    double    safestep;
    double    maxdist;
    double    gridstepX;
    double    gridstepY;
    double    shiftX;
    double    shiftY;
    bool      useRadiusRemoveCommon;
    bool      higherProcessUsesRadiusRemoveCommon;

    SnapToGridSpec snapspec;

} PerProcessSpec;

typedef struct MultiSpec {
    GlobalSpec global;
    StartState startState;
    //global required parameters (replicated for each resolution)
    size_t numspecs;

    std::vector<PerProcessSpec> pp;

    bool anyUseRadiusesRemoveCommon;

    MultiSpec(GlobalSpec _global, size_t n = 0) : global(std::move(_global)), anyUseRadiusesRemoveCommon(false) { if (n>0) initializeVectors(n); }

    void initializeVectors(size_t n) { numspecs = n; pp.resize(n); }
    bool validate();
    bool inline useContoursAlreadyFilled(int k) { return (k > 0) && (!global.addsub.addsubWorkflowMode) && (pp[k].useRadiusRemoveCommon); }
    std::string populateParameters();
} MultiSpec;


#endif
