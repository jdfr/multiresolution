#ifndef  MOTIONPLANNER_HEADER
#define  MOTIONPLANNER_HEADER

#include "multislicer.hpp"

//does not try to find optimal ways to start in each closed contour, the optimizer uses a straightforward greedy algorithm
void verySimpleMotionPlanner(StartState &startState, PathCloseMode mode, clp::Paths &paths);

//the algorithm is quite complex and has several repeated subalgorithms. To avoid a long function with long lambdas, we organize it as a class with shared state
class SaferOverhangingVerySimpleMotionPlanner {
    clp::Paths in, out, output;
    int idx_in, idx_out;
    bool isfront_in, isfront_out;
    std::vector<bool> valid_in, valid_out;
    int numvalid_in, numvalid_out, numvalid;
    ClippingResources &res;
    StartState &startState;
    bool keepStartInsideSupport;
    typedef void (*function_to_compute_nearest)(clp::IntPoint, clp::Paths &, std::vector<bool> &, int &, bool &);
    function_to_compute_nearest nearest_path;
    void clear() { in.clear(); out.clear(); output.clear(); valid_in.clear(); valid_out.clear(); }
    void addInPath();
    bool tryToConcat(clp::Paths &paths, std::vector<bool> &valid, int &this_numvalid, int &idx, bool &isfront);
public:
    SaferOverhangingVerySimpleMotionPlanner(ClippingResources &_res) : res(_res), startState(_res.spec->startState) {}
    void saferOverhangingVerySimpleMotionPlanner(int ntool, clp::Paths &support, PathCloseMode mode, clp::Paths &paths);
};

#endif
