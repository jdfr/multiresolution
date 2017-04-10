#include "spec.hpp"
#include "snapToGrid.hpp"
#include <algorithm>
#include <limits>

void InfillingSpec::computeCUSTOMINFILLINGS() {
    CUSTOMINFILLINGS = (infillingMode == InfillingConcentric)    ||
                       (infillingMode == InfillingRectilinearH)  ||
                       (infillingMode == InfillingRectilinearV)  ||
                       (infillingMode == InfillingRectilinearVH) ||
                       (infillingMode == InfillingRectilinearAlternateVH);
}


bool MultiSpec::validate() {
    //do not validate anything for now
    return true;
}

//sqrt(5)/2
#define semidiagFac 1.118033988749895

//the logic implemented here to initialize the parameters is intimately intertwined with multiliscing code
std::string MultiSpec::populateParameters() {
    std::string result;
    global.anyDifferentiateSurfaceInfillings = false;
    global.anyAlwaysSupported                = false;
    global.anyOverhangAlwaysSupported        = false;
    global.anyUseRadiusesRemoveCommon        = false;
    global.anyEnsureAttachmentOffset         = false;
    for (size_t k = 0; k<numspecs; ++k) {
        
        pp[k].useRadiusRemoveCommon              = pp[k].radiusRemoveCommon > 0;
        
        global.anyDifferentiateSurfaceInfillings = global.anyDifferentiateSurfaceInfillings || pp[k].differentiateSurfaceInfillings;
        global.anyAlwaysSupported                = global.anyAlwaysSupported                || pp[k].alwaysSupported;
        global.anyOverhangAlwaysSupported        = global.anyOverhangAlwaysSupported        || pp[k].overhangAlwaysSupported;
        global.anyUseRadiusesRemoveCommon        = global.anyUseRadiusesRemoveCommon        || pp[k].useRadiusRemoveCommon;
        global.anyEnsureAttachmentOffset         = global.anyEnsureAttachmentOffset         || (pp[k].ensureAttachmentOffset != 0);

        pp[k].internalInfilling.computeCUSTOMINFILLINGS();
        pp[k]. surfaceInfilling.computeCUSTOMINFILLINGS();
        pp[k].any_CUSTOMINFILLINGS      =  pp[k].internalInfilling.CUSTOMINFILLINGS                        ||  pp[k].surfaceInfilling.CUSTOMINFILLINGS;
        pp[k].any_InfillingJustContours = (pp[k].internalInfilling.infillingMode == InfillingJustContours) || (pp[k].surfaceInfilling.infillingMode == InfillingJustContours);
        pp[k].any_isnot_InfillingNone   = (pp[k].internalInfilling.infillingMode != InfillingNone)         || (pp[k].surfaceInfilling.infillingMode != InfillingNone);

        //TODO: currently, we rely on testing applysnap before using gristep, substep, dilatestep or safestep.
        //Because of the complex slicing logic, this is quite unsafe. Think of some way to avoid this without speed penalties...
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
    
    //to have sane defaults, just in case some downstream code does not test for pp[k].differentiateSurfaceInfillings
    if (global.anyDifferentiateSurfaceInfillings) {
        for (size_t k = 0; k<numspecs; ++k) {
            if (!pp[k].differentiateSurfaceInfillings) {
                pp[k].surfaceInfilling = pp[k].internalInfilling;
            }
        }
    }

    //initialize also global parameters
    global.substractiveOuter = (global.limitX>0) && (global.limitY>0);
    if (global.substractiveOuter) {
        global.outerLimitX = global.limitX - (pp[0].radius * 3);
        global.outerLimitY = global.limitY - (pp[0].radius * 3);
    }
    
    return result;
}

bool DoublePointComparator(const clp::DoublePoint &a, const clp::DoublePoint &b) {
    return a.X < b.X;
}

LinearlyApproximatedProfile::LinearlyApproximatedProfile(VerticalProfileSpec s, double slh, double ap, VerticalProfileRecomputeSpec r) {
    spec = std::move(s);
    std::sort(spec.profile.begin(), spec.profile.end(), DoublePointComparator);
    if (r.recomputeMinZ) spec.minZ =  std::numeric_limits<double>::infinity();
    if (r.recomputeMaxZ) spec.maxZ = -std::numeric_limits<double>::infinity();
    double radius                  = -std::numeric_limits<double>::infinity();
    bool recomputeRadiusOrApplicationPoint = r.recomputeRadius | r.recomputeApplicationPoint;
    for (auto &point : spec.profile) {
        double z = point.X;
        double x = std::abs(point.Y);
        if (r.recomputeMinZ && (spec.minZ > z)) spec.minZ = z;
        if (r.recomputeMaxZ && (spec.maxZ < z)) spec.maxZ = z;
        if (recomputeRadiusOrApplicationPoint && (radius < x)) {
            bool validZ = (r.recomputeMinZ || (spec.minZ <= z)) &&
                          (r.recomputeMaxZ || (spec.maxZ >= z));
            if (validZ) {
                radius = x;
                if (r.recomputeApplicationPoint) applicationPoint = z;
            }
        }
    }
    if (!r.recomputeApplicationPoint) applicationPoint = ap;
    //the 3d logic implicitly assumes that the profile coordinates start at minZ=0, so we translate the profile
    if (std::abs(spec.minZ) > spec.epsilon) {
        spec.maxZ        -= spec.minZ;
        applicationPoint -= spec.minZ;
        for (auto &point : spec.profile) {
            point.X      -= spec.minZ;
        }
        spec.minZ         = 0;
    }
    sliceHeight = (r.recomputeSliceHeight) ? spec.maxZ : 2 * slh;
    if (r.recomputeZRadius) spec.zradius = sliceHeight / 2;
    setup(sliceHeight, applicationPoint);
    if (r.recomputeRadius)  {
        spec.radius = r.recomputeApplicationPoint ? radius : getWidth(0);
    }
}

double LinearlyApproximatedProfile::getWidth(double zshift) {
    //zshift has its origin in applicationPoint, so we convert it to be based in the same coordinate system as the other spec values
    clp::DoublePoint pos(zshift + applicationPoint, 0);
    if (pos.X < spec.minZ) return 0;
    if (pos.X > spec.maxZ) return 0;
    //now, let's find it in the ordered profile
    auto element = std::lower_bound(spec.profile.begin(), spec.profile.end(), pos, DoublePointComparator);
    if (element == spec.profile.end()) {
        auto element1 = element - 1;
        if (std::abs(element1->X - pos.X) < spec.epsilon) return element1->Y;
        return 0;
    }
    if (std::abs(element->X  - pos.X) < spec.epsilon)     return element->Y;
    if ((element == spec.profile.begin()))                return 0;
    auto element1 = element - 1;
    if (std::abs(element1->X - pos.X) < spec.epsilon)     return element1->Y;
    double dStepZ = element->X - element1->X;
    double dz     =      pos.X - element1->X;
    double ratio  = dz / dStepZ;
    double dStepX = element->Y - element1->Y;
    pos.Y         = element1->Y + dStepX * ratio;
    return pos.Y;
}

