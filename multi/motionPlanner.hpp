#ifndef  MOTIONPLANNER_HEADER
#define  MOTIONPLANNER_HEADER

#include "common.hpp"

typedef struct StartState {
    clp::IntPoint start_near;
    bool notinitialized;
} StartState;

//does not try to find optimal ways to start in each closed contour, the optimizer uses a straightforward greedy algorithm
void verySimpleMotionPlanner(StartState &startState, PathCloseMode mode, clp::Paths &paths);


#endif
