#ifndef  MULTISLICER_HEADER
#define  MULTISLICER_HEADER

#include "spec.hpp"
#include "auxgeom.hpp"

//these functions are a hackshould be use before and after separateByRadiusCompleteMultiple(), respectively, to the hack to simulate a substractive process
void addOuter(clp::Paths &paths, clp::cInt limitX, clp::cInt limitY);
void removeOuter(clp::Paths &paths, clp::cInt limitX, clp::cInt limitY);

typedef struct SingleProcessOutput {
    std::string err;
    clp::Paths contours;
    clp::Paths contoursToShow;
    clp::Paths ptoolpaths, stoolpaths, itoolpaths;
    clp::Paths infillingAreas;
    clp::Paths medialAxis_toolpaths;
    clp::Paths contours_withexternal_medialaxis;
    clp::Paths unprocessedToolPaths;
    std::vector<clp::Paths> medialAxisIndependentContours;
    std::vector<clp::Paths> infillingsIndependentContours;
    bool alsoInfillingAreas;
    bool phase1complete;
    bool phase2complete;
    bool contours_withexternal_medialaxis_used;
    SingleProcessOutput() : alsoInfillingAreas(false), phase1complete(false), phase2complete(false), contours_withexternal_medialaxis_used(false) {};
#ifdef __GNUC__ //avoid annoying GCC warning about "defaulted move assignment for ResultSingleTool calls a non-trivial move assignment operator for virtual base "SingleProcessOutput" 
    SingleProcessOutput(SingleProcessOutput &&x) = default;
#endif
    SingleProcessOutput(std::string _err) : err(_err) {};
} SingleProcessOutput;

//this is a failsafe to avoid compiler errors, but users should set a default arena chunk size accordingly to the expected usage patterns
#ifndef INITIAL_ARENA_SIZE
#  define INITIAL_ARENA_SIZE (50*1024*1024)
#endif
#ifndef BIGCHUNK_ARENA_SIZE
#  define BIGCHUNK_ARENA_SIZE (5*1024*1024)
#endif


//Common resources for all multislicing subsystems
class ClippingResources {
public: 
    static const bool MemoryManagerPrintDebugMessages = false;
    CLIPPER_MMANAGER manager_offset;
    CLIPPER_MMANAGER manager_clipper;
    CLIPPER_MMANAGER manager_clipper2;
    clp::ClipperOffset offset;
    clp::Clipper clipper;
    clp::Clipper clipper2; //we need this in order to conduct more than one clipping in parallel, if necessary
    std::string *err; //this is a temp. pointer which is set up by applyXXX() methods in MultiSlicer
    std::shared_ptr<MultiSpec> spec;
    template<typename MS = MultiSpec> ClippingResources(typename std::enable_if< CLIPPER_MMANAGER::isArena, std::shared_ptr<MS> >::type _spec) : 
        manager_offset  ("OFFSET",   MemoryManagerPrintDebugMessages, BIGCHUNK_ARENA_SIZE, INITIAL_ARENA_SIZE),
        manager_clipper ("CLIPPER",  MemoryManagerPrintDebugMessages, BIGCHUNK_ARENA_SIZE, INITIAL_ARENA_SIZE),
        manager_clipper2("CLIPPER2", MemoryManagerPrintDebugMessages, BIGCHUNK_ARENA_SIZE, INITIAL_ARENA_SIZE),
        offset(manager_offset), clipper(manager_clipper), clipper2(manager_clipper2), spec(std::move(_spec)), err(NULL) {}
    template<typename MS = MultiSpec> ClippingResources(typename std::enable_if<!CLIPPER_MMANAGER::isArena, std::shared_ptr<MS> >::type _spec) :
        offset(manager_offset), clipper(manager_clipper), clipper2(manager_clipper2), spec(std::move(_spec)), err(NULL) {}

    //// STATELESS, LOW LEVEL HELPER TEMPLATES ////
    template<typename T, typename INFLATEDACCUM> void operateInflatedLinesAndContoursInClipper(clp::ClipType mode, T &res, clp::Paths &lines,                       double radius, clp::Paths *aux, INFLATEDACCUM* inflated_acumulator);
    template<typename T, typename INFLATEDACCUM> void operateInflatedLinesAndContours(         clp::ClipType mode, T &res, clp::Paths &contours, clp::Paths &lines, double radius, clp::Paths *aux, INFLATEDACCUM* inflated_acumulator);
    template<typename Output, typename... Inputs>               void unitePaths(Output &output, Inputs&... inputs);
    template<typename Output, typename Input1, typename Input2> void clipperDo( Output &output, clp::ClipType operation, Input1 &subject, Input2 &clip, clp::PolyFillType subjectFillType, clp::PolyFillType clipFillType);
    template<typename Output, typename Input>                   void offsetDo(  Output &output, double delta,                 Input &input,                  clp::JoinType jointype, clp::EndType endtype);
    template<typename Output, typename Input>                   void offsetDo2( Output &output, double delta1, double delta2, Input &input, clp::Paths &aux, clp::JoinType jointype, clp::EndType endtype);
    //// STATELESS, LOW LEVEL HELPER FUNCTIONS ////
    bool AddPaths(clp::Path  &path, clp::PolyType pt, bool closed);
    bool AddPaths(clp::Paths &paths,               clp::PolyType pt, bool closed);
    int  AddPaths(std::vector<clp::Paths> &pathss, clp::PolyType pt, bool closed);
    void AddPaths(clp::Path  &path,                clp::JoinType jointype, clp::EndType endtype);
    void AddPaths(clp::Paths &paths,               clp::JoinType jointype, clp::EndType endtype);
    bool AddPaths(std::vector<clp::Paths> &pathss, clp::JoinType jointype, clp::EndType endtype);
    //// STATELESS, HIGH LEVEL HELPER TEMPLATES ////
    void removeHighResDetails(size_t k, clp::Paths &contours, clp::Paths &lowres, clp::Paths &opened, clp::Paths &aux1);
    void overwriteHighResDetails(size_t k, clp::Paths &contours, clp::Paths &lowres, clp::Paths &aux1, clp::Paths &aux2);
    void doDiscardCommonToolPaths(size_t k, clp::Paths &toolpaths, clp::Paths &contours_alreadyfilled, clp::Paths &aux1);
    bool generateToolPath(size_t k, bool nextProcessSameKind, clp::Paths &contour, clp::Paths &toolpaths, clp::Paths &temp_toolpath, clp::Paths &aux1);
    bool applyMedialAxisNotAggregated(size_t k, std::vector<double> &medialAxisFactors, std::vector<clp::Paths> &accumContours, clp::Paths &shapes, clp::Paths &medialaxis_accumulator);
};

//here, we include only the templates and functions that are used elsewhere
template<typename Output, typename Input1, typename Input2> void ClippingResources::clipperDo(Output &output, clp::ClipType operation, Input1 &subject, Input2 &clip, clp::PolyFillType subjectFillType, clp::PolyFillType clipFillType) {
    AddPaths(subject, clp::ptSubject, true);
    AddPaths(clip, clp::ptClip, true);
    clipper.Execute(operation, output, subjectFillType, clipFillType);
    if (!std::is_same<Output, clp::PolyTree>::value) clipper.Clear();
}
template<typename Output, typename Input> void ClippingResources::offsetDo(Output &output, double delta, Input &input, clp::JoinType jointype, clp::EndType endtype) {
    AddPaths(input, jointype, endtype);
    offset.Execute(output, delta);
    if (!std::is_same<Output, clp::PolyTree>::value) offset.Clear();
}
inline bool ClippingResources::AddPaths(clp::Path  &path,  clp::PolyType pt, bool closed) { return clipper.AddPath (path,  pt, closed); }
inline bool ClippingResources::AddPaths(clp::Paths &paths, clp::PolyType pt, bool closed) { return clipper.AddPaths(paths, pt, closed); }
inline int  ClippingResources::AddPaths(std::vector<clp::Paths> &pathss, clp::PolyType pt, bool closed) {
    int firstbad = -1;
    for (auto paths = pathss.begin(); paths != pathss.end(); ++paths) {
        //this way to report errors is to add all paths regardless of some intermediate one failing
        if (!clipper.AddPaths(*paths, pt, closed) && (firstbad >= 0)) {
            firstbad = (int)(paths - pathss.begin());
        }
    }
    return firstbad;
}
inline void ClippingResources::AddPaths(clp::Path  &path,  clp::JoinType jointype, clp::EndType endtype) { offset.AddPath( path,  jointype, endtype); }
inline void ClippingResources::AddPaths(clp::Paths &paths, clp::JoinType jointype, clp::EndType endtype) { offset.AddPaths(paths, jointype, endtype); }
inline bool ClippingResources::AddPaths(std::vector<clp::Paths> &pathss, clp::JoinType jointype, clp::EndType endtype) {
    for (auto paths = pathss.begin(); paths != pathss.end(); ++paths) {
        offset.AddPaths(*paths, jointype, endtype);
    }
}


//the functionality in this class is integral part of Multislicer. It is separated mostly for clarity
class Infiller {
public:
    std::shared_ptr<ClippingResources> res;
    Infiller(std::shared_ptr<ClippingResources> _res) : res(std::move(_res)) {}
    void clear() { infillingsIndependentContours = NULL; accumInfillings = NULL; }
    bool applyInfillings(size_t k, bool nextProcessSameKind, InfillingSpec &infillingSpec, std::vector<clp::Paths> &perimetersIndependentContours, clp::Paths &infillingAreas, std::vector<clp::Paths> *_infillingsIndependentContours, clp::Paths &accumInfillingsHolder);
protected:
    //state variables for infilling algorithms (necessary because of recursive implementations, to avoid passing an awful lot of context in each stack frame)
    double       infillingRadius;
    double erodedInfillingRadius;
    double erodedInfillingRadiusBis;
    bool infillingUseClearance, infillingRecursive;
    int numconcentric;
    clp::cInt globalShift; bool useGlobalShift;
    clp::Paths AUX;
    clp::Paths *accumInfillings;
    std::vector<clp::Paths> *infillingsIndependentContours;
    bool processInfillings(size_t k, PerProcessSpec &ppspec, InfillingSpec &infillingSpec, clp::Paths &infillingAreas, clp::Paths &accumInfillingsHolder);
    bool applySnapConcentricInfilling; SnapToGridSpec concentricInfillingSnapSpec; //this is state for the recursive call to processInfillingsConcentricRecursive
    bool processInfillingsConcentricRecursive(HoledPolygon &hp);
    clp::Paths computeClippedLines(BBox &bb, double erodedInfillingRadius, bool horizontal);
    void processInfillingsRectilinear(PerProcessSpec &ppspec, clp::Paths &infillingAreas, BBox &bb, InfillingSpec &ispec);
};

class SaferOverhangingVerySimpleMotionPlanner;

class Multislicer {
protected:
    Infiller infiller;
    //It is convenient that SaferOverhangingVerySimpleMotionPlanner has a reference to ClippingResources. To avoid placing the definition here (SaferOverhangingVerySimpleMotionPlanner belongs logically to motionPlanner.hpp) or doing a major header reorganization, we use a pointer
    SaferOverhangingVerySimpleMotionPlanner *saferoverhangmp;
    //these variables are here to avoid recurring std::vector growing costs, but they are not intended to be used directly, but aliased as method parameters
    clp::Paths AUX1, AUX2, AUX3, AUX4, accumInfillingsHolder, accumInfillingsHolderSurface;
public:
    std::shared_ptr<ClippingResources> res;
    Multislicer(std::shared_ptr<ClippingResources> _res);
    ~Multislicer();
    void clear() { AUX1.clear(); AUX2.clear(); AUX3.clear(); AUX4.clear(); accumInfillingsHolder.clear(); accumInfillingsHolderSurface.clear(); infiller.clear(); }
    //first half of applyProcess()
    bool applyProcessPhase1(SingleProcessOutput &output, clp::Paths &contours_tofill, int k);
    //second half of applyProcess()
    bool applyProcessPhase2(SingleProcessOutput &output, clp::Paths *internalParts, clp::Paths *support, clp::Paths &contours_alreadyfilled, int k);
    bool applyProcess(SingleProcessOutput &output, clp::Paths &contours_tofill, clp::Paths &contours_alreadyfilled, int k);
    int applyProcesses(std::vector<SingleProcessOutput*> &outputs, clp::Paths &contours_tofill, clp::Paths &contours_alreadyfilled, int kinit = -1, int kend = -1);
};


#endif
