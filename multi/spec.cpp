#include "spec.hpp"
#include "snapToGrid.hpp"


///////////////////////////////////////////


bool MultiSpec::validate() {
    //do not validate anything for now
    return true;
}

//sqrt(5)/2
#define semidiagFac 1.118033988749895

//the logic implemented here to initialize the parameters is intimately intertwined with multiliscing code
std::string MultiSpec::populateParameters() {
    std::string result;
    for (size_t k = 0; k<this->numspecs; ++k) {
        this->substeps[k] = this->gridsteps[k] / 2.0;
        this->dilatesteps[k] = this->substeps[k] * 1.05; //play it safe
        this->safesteps[k] = this->gridsteps[k] * (semidiagFac*1.1);
        /*our goal here is to allow a point kissing a square to be snapped to the
        opposite grid points in that square. We multiply by a factor because we
        have to give a little slack (there is no doubt an exact mathematical
        formula, but it is not worth to try to derive it).*/
        this->maxdists[k] = this->gridsteps[k] * (semidiagFac*1.1); //this has to be smaller than safestep
        if (this->addInternalClearances[k]) {
            this->snapSmallSafeSteps[k] = false;
        }
        /*FIXME: These conditions are badly messed up, they will work if "stepradius" is small
        enough relative to "radius", but this may fail in unexpected ways if they are close*/
        if ((!this->snapSmallSafeSteps[k]) && ((this->radiuses[k] * 0.95)>this->maxdists[k])) {
            this->safesteps[k] = (double)this->radiuses[k];
            if (this->addInternalClearances[k]) {
                /*if the big toolpath is required to have an internal clearance equal to radius,
                this is needed to make absolutely sure that the inside of toolpath does not get too thin*/
                this->safesteps[k] += this->gridsteps[k];
            }
            this->maxdists[k] = this->safesteps[k] * 0.95;
        }
        this->gridstepsX[k] = (double)this->gridsteps[k];
        this->gridstepsY[k] = (double)this->gridsteps[k];
        this->shiftsX[k] = 0; //for now, we assume that the grid is centered on the origin
        this->shiftsY[k] = 0;
        this->useRadiusesRemoveCommon[k] = this->radiusesRemoveCommon[k] > 0;
        this->anyUseRadiusesRemoveCommon = this->anyUseRadiusesRemoveCommon || this->useRadiusesRemoveCommon[k];
        //pre-build the specifications for snapToGrid
        this->snapspecs[k].mode = SnapErode;
        this->snapspecs[k].removeRedundant = true;
        this->snapspecs[k].gridstepX = this->gridstepsX[k];
        this->snapspecs[k].shiftX = this->shiftsX[k];
        this->snapspecs[k].gridstepY = this->gridstepsY[k];
        this->snapspecs[k].shiftY = this->shiftsY[k];
        this->snapspecs[k].maxdist = this->maxdists[k];
        this->snapspecs[k].numSquares = (int)ceil(this->maxdists[k] / fmin(this->gridstepsX[k], this->gridstepsY[k]));
    }
    this->higherProcessUsesRadiusesRemoveCommon[this->numspecs - 1] = false;
    for (int k = (int)this->numspecs - 2; k >= 0; k--) {
        this->higherProcessUsesRadiusesRemoveCommon[k] = this->higherProcessUsesRadiusesRemoveCommon[k + 1] || this->useRadiusesRemoveCommon[k + 1];
    }

    //initialize also global parameters
    global.substractiveOuter = (global.limitX>0) && (global.limitY>0);
    if (global.substractiveOuter) {
        global.outerLimitX = global.limitX - (this->radiuses[0] * 3);
        global.outerLimitY = global.limitY - (this->radiuses[0] * 3);
    }
    return result;
}

void MultiSpec::initializeVectors(size_t n) {
    numspecs = n;
    radiuses.resize(numspecs);
    gridsteps.resize(numspecs);
    arctolRs.resize(numspecs);
    arctolGs.resize(numspecs);
    burrLengths.resize(numspecs);
    radiusesRemoveCommon.resize(numspecs);
    applysnaps.resize(numspecs);
    snapSmallSafeSteps.resize(numspecs);
    addInternalClearances.resize(numspecs);
    medialAxisFactors.resize(numspecs);
    medialAxisFactorsForInfillings.resize(numspecs);
    infillingModes.resize(numspecs);
    infillingWhole.resize(numspecs);
    infillingRecursive.resize(numspecs);
    profiles.resize(numspecs);
    substeps.resize(numspecs);
    dilatesteps.resize(numspecs);
    safesteps.resize(numspecs);
    maxdists.resize(numspecs);
    gridstepsX.resize(numspecs);
    gridstepsY.resize(numspecs);
    shiftsX.resize(numspecs);
    shiftsY.resize(numspecs);
    useRadiusesRemoveCommon.resize(numspecs);
    higherProcessUsesRadiusesRemoveCommon.resize(numspecs);
    snapspecs.resize(numspecs);
}
