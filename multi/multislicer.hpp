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
    clp::Paths toolpaths;
    clp::Paths infillingAreas;
    std::vector<clp::Paths> medialAxisIndependentContours;
    std::vector<clp::Paths> infillingsIndependentContours;
    bool alsoInfillingAreas;
    SingleProcessOutput() = default;
#ifdef __GNUC__ //avoid annoying GCC warning about "defaulted move assignment for ResultSingleTool calls a non-trivial move assignment operator for virtual base "SingleProcessOutput" 
    SingleProcessOutput(SingleProcessOutput &&x) = default;
#endif
    SingleProcessOutput(std::string _err) : err(_err) {};
    SingleProcessOutput(clp::Paths &&_contours, clp::Paths &&_toolpaths, clp::Paths &&_infillingAreas, bool _alsoInfillingAreas) : contours(std::move(_contours)), toolpaths(std::move(_toolpaths)), infillingAreas(std::move(_infillingAreas)), alsoInfillingAreas(_alsoInfillingAreas) {};
} SingleProcessOutput;

class Multislicer {
protected:
    MultiSpec &spec;
    std::string *err; //this must be set up by applyXXX() methods
    //these variables are here to avoid recurring std::vector growing costs, but they are not intended to be used directly, but aliased as method parameters
    clp::Paths AUX1, AUX2, AUX3, AUX4;
    //state variables for infilling algorithms (necessary because of recursive implementations, to avoid passing an awful lot of context in each stack frame)
    double infillingRadius; bool infillingUseClearance, infillingRecursive;
    clp::Paths accumInfillingsHolder, *accumInfillings, accumInflatedMedialAxis, accumNonCoveredByInfillings;
    std::vector<clp::Paths> *infillingsIndependentContours;
    bool applySnapConcentricInfilling; SnapToGridSpec concentricInfillingSnapSpec; //this is state for the recursive call to processInfillingsConcentricRecursive
public:
    clp::ClipperOffset offset;
    clp::Clipper clipper;
    clp::Clipper clipper2; //we need this in order to conduct more than one clipping in parallel, if necessary
    Multislicer(MultiSpec &_spec) : spec(_spec) {}
    void clear() { AUX1.clear(); AUX2.clear(); AUX3.clear(); accumInfillingsHolder.clear();  accumInflatedMedialAxis.clear(); accumNonCoveredByInfillings.clear();  infillingsIndependentContours = NULL; }
    // contours_tofill is an in-out parameter, it starts with the contours to fill, it ends with the 
    //contours_alreadyfilled should already have been carved out from contours_tofill; it has to be provided as an additional argument just in case it is needed by the doDiscardCommonToolPaths sub-algorithm
    bool applyProcess(SingleProcessOutput &output, clp::Paths &contours_tofill, clp::Paths &contours_alreadyfilled, int k);
    //ditto for applyProcess
    int applyProcesses(std::vector<SingleProcessOutput*> &outputs, clp::Paths &contours_tofill, clp::Paths &contours_alreadyfilled, int kinit = -1, int kend = -1);

protected:
    void removeHighResDetails(size_t k, clp::Paths &contours, clp::Paths &lowres, clp::Paths &opened, clp::Paths &aux1);
    void overwriteHighResDetails(size_t k, clp::Paths &contours, clp::Paths &lowres, clp::Paths &aux1, clp::Paths &aux2);

    void doDiscardCommonToolPaths(size_t k, clp::Paths &toolpaths, clp::Paths &contours_alreadyfilled, clp::Paths &aux1);

    bool generateToolPath(size_t k, bool nextProcessSameKind, clp::Paths &contour, clp::Paths &toolpaths, clp::Paths &temp_toolpath, clp::Paths &aux1);

    bool processInfillings(size_t k, clp::Paths &infillingAreas, clp::Paths &contoursToBeInfilled);
    bool processInfillingsConcentricRecursive(HoledPolygon &hp);
    void processInfillingsRectilinear(size_t k, clp::Paths &infillingAreas, BBox bb, bool horizontal);

    void applyMedialAxisNotAggregated(size_t k, std::vector<double> &medialAxisFactors, std::vector<clp::Paths> &accumContours, clp::Paths &shapes, clp::Paths &medialaxis_accumulator);

    template<typename T, typename INFLATEDACCUM> void operateInflatedLinesAndContoursInClipper(clp::ClipType mode, T &res, clp::Paths &lines, double radius, clp::Paths *aux, INFLATEDACCUM* inflated_acumulator);
    template<typename T, typename INFLATEDACCUM> void operateInflatedLinesAndContours(clp::ClipType mode, T &res, clp::Paths &contours, clp::Paths &lines, double radius, clp::Paths *aux, INFLATEDACCUM* inflated_acumulator);

};


#endif
