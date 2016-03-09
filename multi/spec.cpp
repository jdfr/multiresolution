#include "spec.hpp"
#include "snapToGrid.hpp"


bool MultiSpec::validate() {
    //do not validate anything for now
    return true;
}

//sqrt(5)/2
#define semidiagFac 1.118033988749895

//the logic implemented here to initialize the parameters is intimately intertwined with multiliscing code
std::string MultiSpec::populateParameters() {
    std::string result;
    for (size_t k = 0; k<numspecs; ++k) {
        pp[k].useRadiusRemoveCommon    = pp[k].radiusRemoveCommon > 0;
        anyUseRadiusesRemoveCommon     = anyUseRadiusesRemoveCommon || pp[k].useRadiusRemoveCommon;

        if (!pp[k].applysnap) continue;

        pp[k].substep    = pp[k].gridstep / 2.0;
        pp[k].dilatestep = pp[k].substep  * 1.05; //play it safe
        pp[k].safestep   = pp[k].gridstep * (semidiagFac*1.1);
        /*our goal here is to allow a point kissing a square to be snapped to the
        opposite grid points in that square. We multiply by a factor because we
        have to give a little slack (there is no doubt an exact mathematical
        formula, but it is not worth to try to derive it).*/
        pp[k].maxdist = pp[k].gridstep * (semidiagFac*1.1); //this has to be smaller than safestep
        if (pp[k].addInternalClearance) {
            pp[k].snapSmallSafeStep = false;
        }
        /*FIXME: These conditions are badly messed up, they will work if "stepradius" is small
        enough relative to "radius", but this may fail in unexpected ways if they are close*/
        if ((!pp[k].snapSmallSafeStep) && ((pp[k].radius * 0.95)>pp[k].maxdist)) {
            pp[k].safestep = (double)pp[k].radius;
            if (pp[k].addInternalClearance) {
                /*if the big toolpath is required to have an internal clearance equal to radius,
                this is needed to make absolutely sure that the inside of toolpath does not get too thin*/
                pp[k].safestep += pp[k].gridstep;
            }
            pp[k].maxdist = pp[k].safestep * 0.95;
        }
        pp[k].gridstepX                = (double)pp[k].gridstep;
        pp[k].gridstepY                = (double)pp[k].gridstep;
        pp[k].shiftX                   = 0; //for now, we assume that the grid is centered on the origin
        pp[k].shiftY                   = 0;
        //pre-build the specifications for snapToGrid
        pp[k].snapspec.mode            = SnapErode;
        pp[k].snapspec.removeRedundant = true;
        pp[k].snapspec.gridstepX       = pp[k].gridstepX;
        pp[k].snapspec.shiftX          = pp[k].shiftX;
        pp[k].snapspec.gridstepY       = pp[k].gridstepY;
        pp[k].snapspec.shiftY          = pp[k].shiftY;
        pp[k].snapspec.maxdist         = pp[k].maxdist;
        pp[k].snapspec.numSquares      = (int)ceil(pp[k].maxdist / fmin(pp[k].gridstepX, pp[k].gridstepY));
    }
    pp[numspecs - 1].higherProcessUsesRadiusRemoveCommon = false;
    for (int k = (int)numspecs - 2; k >= 0; k--) {
        pp[k].higherProcessUsesRadiusRemoveCommon = pp[k+1].higherProcessUsesRadiusRemoveCommon || pp[k+1].useRadiusRemoveCommon;
    }

    //initialize also global parameters
    global.substractiveOuter = (global.limitX>0) && (global.limitY>0);
    if (global.substractiveOuter) {
        global.outerLimitX = global.limitX - (pp[0].radius * 3);
        global.outerLimitY = global.limitY - (pp[0].radius * 3);
    }
    return result;
}

