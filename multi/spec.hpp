#ifndef  SPEC_HEADER
#define  SPEC_HEADER

#include "common.hpp"
#include "config.hpp"
#include "snapToGrid.hpp"
#include "motionPlanner.hpp"
#include <memory>
#include <stdexcept>
#include <sstream>

#include <cmath>

/********************************************************
MACROS TO GENERATE BOILERPLATE FOR READING ARGUMENTS
*********************************************************/

#if defined(_MSC_VER)
#  define strtoll _strtoi64
#endif

enum ParamMode { ParamString, ParamFile };

//this class is rather inefficient, it exists in its current form to have less boilerplate when reading arguments and checking them
class ParamReader {
public:
    std::string err;
    std::ostringstream fmt;
    std::string errorAppendix;
    int argidx;
    int argc;
    const char **argv;
    double scale;
    ParamReader(int _argidx, int _argc, const char **_argv) : argidx(_argidx), argc(_argc), argv(_argv), scale(0.0) {}
    ParamReader(std::string &args, ParamMode mode) : scale(0.0) { setState(args, mode); };
    ParamReader(const char *args, ParamMode mode) : scale(0.0) { std::string sargs(args);  setState(sargs, mode); }
#ifdef __GNUC__
    //this is needed because GCC does not implement stringstream's move constructor
    //WARNING: NON_COMPLETELY CLEAN MOVE SEMANTICS DUE TO THE INABILITY TO MOVE A STRINGSTREAM IN GCC. NOT IMPORTANT ANYWAY FOR OUR USE CASE
    ParamReader(ParamReader &&pr) : err(std::move(pr.err)), argidx(pr.argidx), argc(pr.argc), argv(pr.argv), scale(pr.scale), splitted(std::move(pr.splitted)), local_argv(std::move(pr.local_argv)) {}
#endif
    //these methods are templated in order to laziy generate a error message from several arguments if there is a problem
    template<typename... Args> bool checkEnoughParams(Args... args) {
        if (argidx >= argc) {
            fmt << "error reading ";
            int dummy[sizeof...(Args)] = { (fmt << args, 0)... };
            fmt << ", not enough arguments (arg. number " << (argidx + 1) << "/" << argc << ")\n" << errorAppendix;
            return false;
        }
        return true;
    }
    template<typename... Args> bool readParam(double &param,                      Args... args) { if (!checkEnoughParams(args...)) return false; param =      strtod (argv[argidx++], NULL);         return true; }
    template<typename... Args> bool readParam(int64  &param,                      Args... args) { if (!checkEnoughParams(args...)) return false; param =      strtoll(argv[argidx++], NULL, 10);     return true; }
    template<typename... Args> bool readParam(int    &param,                      Args... args) { if (!checkEnoughParams(args...)) return false; param = (int)strtol (argv[argidx++], NULL, 10);     return true; }
    template<typename... Args> bool readParam(bool   &param, const char* trueval, Args... args) { if (!checkEnoughParams(args...)) return false; param =       strcmp(argv[argidx++], trueval) == 0; return true; }
    template<typename... Args> bool readParam(const char* &param,                 Args... args) { if (!checkEnoughParams(args...)) return false; param =              argv[argidx++];                return true; }
    template<typename... Args> bool readScaled(int64  &param, Args... args) {
        if (scale == 0.0)
            return readParam(param, args...);
        else {
            double val;
            bool ok = readParam(val, args...);
            if (ok) param = (int64)(val*scale);
            return ok;
        }
    }
    template<typename... Args> bool readScaled(double  &param, Args... args) {
        bool ok = readParam(param, args...);
        if (ok && (scale!=0.0)) param *= scale;
        return ok;
    }
protected:
    std::vector<std::string> splitted;
    std::vector<const char*> local_argv;
    void setState(std::string &args, ParamMode mode);
};

ParamReader getParamReader(int argc, const char **argv);

/********************************************************
GLOBAL PARAMETERS
*********************************************************/

typedef struct GlobalSpec {
    typedef struct { double z; int ntool; } ZNTool;
    Configuration &config;
    bool useScheduler;
    bool manualScheduler;
    bool addsubWorkflowMode;
    bool alsoContours;
    bool applyMotionPlanner;
    bool avoidVerticalOverwriting;
    bool correct; //this is to correct the contour orientations (not needed if the input is from slic3r's adapted code)
    std::vector < ZNTool > schedSpec;
    clp::cInt limitX, limitY;
    double z_uniform_step; //this parameter is the uniform step if useScheduler is false. Unlike most other metric parameters, this is in the mesh's native units!!!!
    double z_epsilon; //epsilon to consider that to Z values are the same.
    //not mean to be read from the command line (for internal use)
    clp::Paths inputSub; //this is used if flag "addsubWorkflowMode" is set
    bool substractiveOuter;
    clp::cInt outerLimitX, outerLimitY;
    GlobalSpec(Configuration &_config) : config(_config), inputSub(0) { }
    std::string readFromCommandLine(ParamReader &rd, double scale=0.0);
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

enum InfillingMode { InfillingNone, InfillingJustContours, InfillingConcentric, InfillingRectilinear };

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

    //this should be defined in the cpp, it is here for ease of modification
    MultiSpec(GlobalSpec _global, size_t n) :
        global(_global),
        numspecs(n),
        radiuses(n),
        gridsteps(n),
        arctolRs(n),
        arctolGs(n),
        burrLengths(n),
        radiusesRemoveCommon(n),
        applysnaps(n),
        snapSmallSafeSteps(n),
        addInternalClearances(n),
        medialAxisFactors(n),
        medialAxisFactorsForInfillings(n),
        infillingModes(n),
        infillingRecursive(n),
        substeps(n),
        dilatesteps(n),
        safesteps(n),
        maxdists(n),
        gridstepsX(n),
        gridstepsY(n),
        shiftsX(n),
        shiftsY(n),
        useRadiusesRemoveCommon(n),
        higherProcessUsesRadiusesRemoveCommon(n),
        snapspecs(n),
        anyUseRadiusesRemoveCommon(false) {
    }

    bool validate();
    bool inline useContoursAlreadyFilled(int k) { return (k > 0) && (!global.addsubWorkflowMode) && (useRadiusesRemoveCommon[k]); }
    std::string readFromCommandLine(ParamReader &rd, double scale=0.0);
private:
    std::string populateParameters();
} MultiSpec;


/********************************************************
UTILITY CLASS TO READ PARAMETERS FROM VARIOUS SOURCES
*********************************************************/

typedef struct Arguments {
public:
    std::string err;
    MultiSpec *multispec;
    Configuration *config;
    bool freeConfig;

    Arguments(const char *configfile);
    Arguments(Configuration *_config);

    ~Arguments();

    bool readArguments(bool scale, ParamReader &rd);
} Arguments;


#endif
