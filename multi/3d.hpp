#ifndef THREED_HEADER
#define THREED_HEADER

#include "multislicer.hpp"
#include "spec.hpp"
#include <math.h>
#include <exception>
#include <map>

typedef struct ResultSingleTool : public virtual SingleProcessOutput {
    double z;
    int ntool;
    int idx;
    bool has_err;
    bool used;
    ResultSingleTool(std::string _err, double _z = NAN) : SingleProcessOutput(_err), z(_z), has_err(true) {};
    ResultSingleTool(clp::Paths &&_contours, clp::Paths &&_toolpaths, clp::Paths &&_infillingAreas, double _z, int _ntool, int _idx, bool _alsoInfillingAreas) : SingleProcessOutput(std::move(_contours), std::move(_toolpaths), std::move(_infillingAreas), _alsoInfillingAreas), z(_z), ntool(_ntool), idx(_idx), has_err(false), used(false) {};
    ResultSingleTool(double _z, int _ntool, int _idx) : SingleProcessOutput(), z(_z), ntool(_ntool), idx(_idx), has_err(false), used(false) {}
    ResultSingleTool() : SingleProcessOutput(), has_err(false), idx(-1), ntool(-1), z(NAN), used(true) {}
} ResultSingleTool;

//enum ZRemoveMode { RemoveAboveZ, RemoveBelowZ };

//typedef void (*ResultConsumer)(SeparateResult &result, bool computeResult, MultiSpec &spec);

class SimpleSlicingScheduler;

/*this class applies multislicing, keeping track of generated slices at different heights,
and adjusting accordingly the contours to be built. The functionality here is semantically part
of the scheduler. However, as the scheduler is already quite complex on its own, all (or
hopefully most) of the logic to manage previous toolpaths is contained here*/
class ToolpathManager {
    clp::Paths auxUpdate, auxInitial, contours_alreadyfilled;
    clp::Clipper clipper2; //we need this in addition to the multislicer's clipper in order to conduct more than one clipping in parallel
    //this function is the body of the inner loop in updateInputWithProfilesFromPreviousSlices(), parametrized in the contour
    void applyContours(clp::Paths &contours, int k, bool processIsAdditive, bool computeContoursAlreadyFilled, double diffwidth);
    void inline applyContours(std::vector<clp::Paths> &contourss, int k, bool processIsAdditive, bool computeContoursAlreadyFilled, double diffwidth) {
        for (auto contours = contourss.begin(); contours != contourss.end(); ++contours) {
            if (!contours->empty()) applyContours(*contours, k, processIsAdditive, computeContoursAlreadyFilled, diffwidth);
        }

    }
public:
    friend class SimpleSlicingScheduler;
    std::string err;
    //the outer vector has one element for each process. The inner vectors are previous slices with their z values
    std::vector<std::vector<std::shared_ptr<ResultSingleTool>>> slicess;
    void updateInputWithProfilesFromPreviousSlices(clp::Paths &initialContour, clp::Paths &rawSlice, double z, int ntool);
    MultiSpec &spec;
    Multislicer multi;
    ToolpathManager(MultiSpec &s) : spec(s), slicess(s.numspecs), multi(s) {}
    bool multislice(clp::Paths &input, double z, int ntool, int output_index);
    //std::vector<std::vector<int>> getOverlappingSlices(double z);
    //void removeSlicesInRange(double zmin, double zmax);
    //void removeSlicesZ(double z, ZRemoveMode mode);
    void removeUsedSlicesNotReachableInZ(double z);
    void removeUsedSlicesBelowZ(double z);
};

typedef struct InputSliceData {
    double z;
    int ntool;
    int mapInputToOutput; //one to one mapping
    int mapInputToRaw; //many to one mapping
    std::vector<int> requiredRawSlices; //this is only required if flag avoidVerticalOverwriting is set
    InputSliceData(double _z, int _ntool) : z(_z), ntool(_ntool) {}
} InputSliceData;

typedef struct OutputSliceData {
    double z;
    int ntool;
    int mapOutputToInput; //one to one mapping
    bool computed;
} OutputSliceData;

typedef struct RawSliceData {
    double z;
    int numRemainingUses; //to keep track of the number of times each raw slice has to be used (initilized before the slice is in use, so this is the reason we need the flag inUse)
    bool inUse;           //flag: if set, the raw slice is in use (redudant with previous
    bool wasUsed;         //flag to catch error conditions
    clp::Paths slice;
    std::vector<int> mapRawToInput; //one to many mapping
} RawSliceData;

/*this class keeps track of raw slices. Its functionality is semantically part of
the scheduler (it even requires a reference to the scheduler!), but most of it is
contained here, because the scheduler is already quite complex on its own*/
class RawSlicesManager {
    SimpleSlicingScheduler &sched;
public:
    clp::Paths auxRawSlice, auxaux;
    int raw_idx;
    std::vector<RawSliceData> raw;
    std::vector<double> rawZs; //this is required in computeSlicesZs()
    RawSlicesManager(SimpleSlicingScheduler &s) : sched(s) {}
    void removeUsedRawSlicesBelowZ(double z);
    void clear() { raw.clear();  rawZs.clear();  auxRawSlice.clear();  auxaux.clear();  raw_idx = 0; }
    bool singleRawSliceReady(int raw_idx, int input_idx);
    bool rawReady(int input_idx);
    clp::Paths *getRawContour(int raw_idx, int input_idx);
    void receiveNextRawSlice(clp::Paths &input); //this method has to trust that the input slice will be according to the list of Z input values
};

enum SchedulingMode { ScheduleTwoPhotonSimple, ScheduleLaserSimple };

/*This scheduler controls the main workflow. It is quite complex,
because of the need to keep track of a heck of a lot of things:

-a list of previously computed toolpaths, which influence the generation
of successive toolpaths

-a list of raw slices

-a list of input slices (a single tool for each input slice) to be
processed (not the same as raw slices, since one raw slice may be related
to multiple input slices). The input slices are computed from raw slices and
previous toolpaths at the same or nearby heights. The ordering reflects the
hierarchical nature of the processes (low res processes first than neighbouring
high res processes).

-a list of output slices (toolpaths for a single process at a specific height).
The ordering is by height, and within the same height, by process.

Note that the orderings for input and output slices are different!

this class is intended for very simple use cases, such as two-photon absorption.
Specifically, voxels are always supposed to be symmetric along the Z axis, and
to be centered on the Z slice plane. 

If regular laser ablation is used (specifically, no two-photon absorption effect),
voxels are neither symmetric along the Z axis nor centered on the Z slice
plane, and this code has not been crafted with this in mind, so it is probably
incorrect in that case, requiring adjustments (namely, on the precise Z positioning
of the layers, and the way operations are ordered before being converted to g-code).*/
class SimpleSlicingScheduler {
    void recursiveSimpleInputScheduler(int process, std::vector<double> &z, double ztop);
    void computeSimpleOutputOrderForInputSlices();
    void pruneInputZsAndCreateRawZs(double epsilon);
    bool getContourToAvoidVerticalOverwriting(clp::Paths &output);
public:
    std::string err;
    bool has_err;
    bool removeUnused;
    double zmin, zmax;
    ToolpathManager tm;
    RawSlicesManager rm;
    size_t input_idx, output_idx;
    std::vector<InputSliceData> input;
    std::vector<OutputSliceData> output;

    void clear() { input.clear(); output.clear(); err = std::string(); has_err = false; input_idx = output_idx = 0; zmin = zmax = 0.0; rm.clear(); }

    SimpleSlicingScheduler(bool _removeUnused, MultiSpec &s) : removeUnused(_removeUnused), has_err(false), tm(s), rm(*this) {}
    void createSlicingSchedule(double minz, double maxz, double epsilon, SchedulingMode mode);

    void computeNextInputSlices();
    std::shared_ptr<ResultSingleTool> giveNextOutputSlice(); //this method will return slices in the correct order
};

#endif
