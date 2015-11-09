#include "spec.hpp"
#include "config.hpp"
#include <stdlib.h>
#include <string.h>
#include <sstream>

void ParamReader::setState(std::string &args, ParamMode mode) {
    if (mode == ParamString) {
        auto spec = escaped_list_separator<char>("", " \t\n\v\f\r", "\"");
        splitted = split(args, spec);
        local_argv.resize(splitted.size());
        for (int k = 0; k<local_argv.size(); ++k) {
            local_argv[k] = splitted[k].c_str();
        }
        argv = &(local_argv[0]);
        argc = (int)local_argv.size();
        argidx = 0;
    } else {
        bool ok = true;
        std::string contents = get_file_contents(args.c_str(), ok);
        if (ok) {
            setState(contents, ParamString);
        } else {
            argc = 0;
            argv = NULL;
            argidx = 0;
            err = std::move(contents);
        }
    }
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

std::string MultiSpec::readFromCommandLine(ParamReader &rd, double scale) {
    //example of parameters: 500000 100000 5000 1000 100 snap smallsafestep addclearance 1000 -1 10 10 100 nosnap nosss noclearance

    rd.scale = scale;

    for (size_t k = 0; k<numspecs; ++k) {
        if (!rd.readScaled   (radiuses[k],               "slicing radius for process ", k)) return rd.fmt.str();
        if (global.useScheduler) {
            bool voxelIsEllipsoid;
            double ZRadius, ZSemiHeight;
            if (!rd.readParam(voxelIsEllipsoid, "ellipsoid", "voxel shape for process", k)) return rd.fmt.str();
            if (!rd.readScaled(ZRadius,                        "Z radius for process ", k)) return rd.fmt.str();
            if (!rd.readScaled(ZSemiHeight,         "Z slice semi-height for process ", k)) return rd.fmt.str();
            if (voxelIsEllipsoid) {
                profiles.push_back(std::make_shared<EllipticalProfile>((double)radiuses[k], ZRadius, 2*ZSemiHeight));
            } else {
                profiles.push_back(std::make_shared<ConstantProfile>  ((double)radiuses[k], ZRadius, 2*ZSemiHeight));
            }
        }
        if (!rd.readScaled(gridsteps[k],            "gridstep for process ",                        k)) return rd.fmt.str();
        if (!rd.readScaled(arctolRs[k],             "arctolR for process ",                         k)) return rd.fmt.str();
        if (!rd.readScaled(arctolGs[k],             "arctolG for process ",                         k)) return rd.fmt.str();
        if (!rd.readScaled(burrLengths[k],          "burrLength for process ",                      k)) return rd.fmt.str();
        if (!rd.readScaled(radiusesRemoveCommon[k], "radius for clipping common arcs for process ", k)) return rd.fmt.str();
        bool p;
        if (!rd.readParam(p, "snap",      "snapToGrid for process ",           k)) return rd.fmt.str(); applysnaps[k]            = p;
        if (!rd.readParam(p, "safestep",  "snapSmallSafeStep for process ",    k)) return rd.fmt.str(); snapSmallSafeSteps[k]    = p;
        if (!rd.readParam(p, "clearance", "addInternalClearance for process ", k)) return rd.fmt.str(); addInternalClearances[k] = p;
        int numMedialAxisFactors;
        if (!rd.readParam(numMedialAxisFactors,     "number of medial axis factors for process ",      k)) return rd.fmt.str();
        medialAxisFactors[k].resize(numMedialAxisFactors);
        for (int subk = 0; subk != numMedialAxisFactors; ++subk) {
            if (!rd.readParam(medialAxisFactors[k][subk], subk, "-th medial axis factor for process ", k)) return rd.fmt.str();
        }
        bool useinfill;
        if (!rd.readParam(useinfill,                "infill", "useInfill for process ",                k)) return rd.fmt.str();
        if (useinfill) {
            if (!rd.checkEnoughParams("infillingMode for process ", k)) return rd.fmt.str();
            if      (strcmp(rd.argv[rd.argidx], "concentric")  == 0)  infillingModes[k] = InfillingConcentric;
            else if (strcmp(rd.argv[rd.argidx], "lines")       == 0)  infillingModes[k] = InfillingRectilinear;
            else if (strcmp(rd.argv[rd.argidx], "justcontour") == 0)  infillingModes[k] = InfillingJustContours;
            else                                                      infillingModes[k] = InfillingNone;
            rd.argidx++;
            if (!rd.readParam(p, "recursive",                             "considerUnfilledInfilling for process ", k))               return rd.fmt.str(); infillingRecursive[k] = p;
            int numInfillingMedialAxisFactors;
            if (!rd.readParam(numInfillingMedialAxisFactors,              "number of infilling medial axis factors for process ", k)) return rd.fmt.str();
            this->medialAxisFactorsForInfillings[k].resize(numInfillingMedialAxisFactors);
            for (int subk = 0; subk != numInfillingMedialAxisFactors; ++subk) {
                if (!rd.readParam(medialAxisFactorsForInfillings[k][subk], subk, "-th infilling medial axis factor for process ", k)) return rd.fmt.str();
            }
        } else {
            this->infillingModes[k]     = InfillingNone;
            this->infillingRecursive[k] = false;
        }
    }
    return populateParameters();
}

std::string GlobalSpec::readFromCommandLine(ParamReader &rd, double scale) {
    rd.scale = scale;
    if (!rd.readParam(alsoContours,       "contours",   "alsoContours parameter"))       return rd.fmt.str();
    if (!rd.readParam(correct,            "correct",    "mode (correct/nocorrect)"))     return rd.fmt.str();
    if (!rd.readParam(applyMotionPlanner, "motion_opt", "order optimization parameter")) return rd.fmt.str();
    if (!rd.readScaled(limitX,            "limitX"))                                     return rd.fmt.str();
    if (!rd.readScaled(limitY,            "limitY"))                                     return rd.fmt.str();
    if (!rd.readParam(useScheduler,       "sched",      "useScheduler (sched/nosched)")) return rd.fmt.str();
    if (useScheduler) {
        if (!rd.readParam(avoidVerticalOverwriting,     "vcorrect", "avoid vertical overwritting (vcorrect/novcorrect)")) return rd.fmt.str();
        if (!rd.readScaled(z_epsilon,                   "Z epsilon"))                                                     return rd.fmt.str();
    } else {
        if (!rd.readParam(z_uniform_step,               "Z uniform step"))                                                return rd.fmt.str();
    }
    if (!rd.readParam(addsubWorkflowMode, "addsub",     "worflow mode (simple/addsub)")) return rd.fmt.str();
    return std::string();
}

/////////////////////////////////////////////////
//WORKING WITH ARGUMENTS
/////////////////////////////////////////////////

Arguments::Arguments(const char *configfile) : freeConfig(true), multispec(NULL) {
    config = new Configuration(configfile);
}

Arguments::Arguments(Configuration *_config) : freeConfig(false), config(_config), multispec(NULL) {}

Arguments::~Arguments() {
    if (this->multispec) {
        delete this->multispec;
    }
    if (freeConfig && (config!=NULL)) {
        delete this->config;
    }
}

bool Arguments::readArguments(bool doscale, ParamReader &rd) {
    double scale = 0.0;
    if (doscale) {
        if (config->hasKey("PARAMETER_TO_INTERNAL_FACTOR")) {
            std::string val = config->getValue("PARAMETER_TO_INTERNAL_FACTOR");
            scale = strtod(val.c_str(), NULL);
        } else {
            err = "doscale flag is true, but there was not PARAMETER_TO_INTERNAL_FACTOR in the configuration";
            return false;
        }
    }
    GlobalSpec global(*config);
    err = global.readFromCommandLine(rd, scale);
    if (!err.empty()) {
        return false;
    }

    int numtools;
    if (!rd.readParam(numtools, "number of processes")) {
        err = rd.fmt.str();
        return false;
    }

    this->multispec = new MultiSpec(global, numtools);

    err = this->multispec->readFromCommandLine(rd, scale);
    if (!err.empty()) {
        return false;
    }

    return true;
}
