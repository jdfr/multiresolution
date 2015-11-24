#include "spec.hpp"
#include "config.hpp"
#include <stdlib.h>
#include <string.h>
#include <sstream>

void ParamReader::setState(std::string &args, ParamMode mode) {
    if (mode == ParamString) {
        splitted = split(args, "", " \t\n\v\f\r", "\"", '#', '\n');
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

ParamReader getParamReader(int argc, const char **argv) {
    //first argument is exec's filename
    if (argc == 2) {
        return ParamReader(argv[1], ParamFile);
    } else {
        return ParamReader(0, --argc, ++argv);
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
    const char * schedModeStr;
    if (!rd.readParam(schedModeStr,       "useScheduler (sched/manual/uniform)")) return rd.fmt.str();
    if (strcmp(schedModeStr, "sched") == 0) {
        schedMode = SimpleScheduler;
    } else if (strcmp(schedModeStr, "manual") == 0)  {
        schedMode = ManualScheduling;
    } else if (strcmp(schedModeStr, "uniform") == 0)  {
        schedMode = UniformScheduling;
    } else {
        return str("useScheduler was not recognized (valid values: sched/manual/uniform): ", schedModeStr);
    }
    useScheduler = schedMode != UniformScheduling;
    if (useScheduler) {
        if (!rd.readParam(avoidVerticalOverwriting,     "vcorrect", "avoid vertical overwritting (vcorrect/novcorrect)")) return rd.fmt.str();
        if (!rd.readScaled(z_epsilon,                   "Z epsilon"))                                                     return rd.fmt.str();
        if (schedMode==ManualScheduling) {
            int numlayers;
            if (!rd.readParam(numlayers,                "number of input layers"))                                        return rd.fmt.str();
            if (numlayers <= 0) return str("The number of input layers cannot be ", numlayers, "!!!");
            schedSpec.resize(numlayers);
            for (int k = 0; k < numlayers; ++k) {
                if (!rd.readParam(schedSpec[k].z,       "Z height of input layer ", k, '/', numlayers))                   return rd.fmt.str();
                if (!rd.readParam(schedSpec[k].ntool,      "ntool of input layer ", k, '/', numlayers))                   return rd.fmt.str();
            }
        } else {
            const char *schedToolsMode;
            if (!rd.readParam(schedToolsMode,           "tool scheduling mode ('all'/non-zero number of scheduled tools)")) return rd.fmt.str();
            bool ok = strcmp(schedToolsMode, "all")==0;
            if (!ok) {
                char * next;
                int numtools = (int)strtol(schedToolsMode, &next, 10);
                ok = (*next) == 0;
                if (ok) {
                    if (numtools == 0) {
                        return std::string("tool scheduling mode cannot be 0!");
                    }
                    schedTools.resize(numtools);
                    for (int k = 0; k < numtools; ++k) {
                        if (!rd.readParam(schedTools[k], k, "-th tool to schedule")) return rd.fmt.str();
                    }
                } else {
                    return str("tool scheduling mode must be either 'all' or the non-zero number of scheduled tools, but it was <", schedToolsMode, ">");
                }
            }
        }
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
    rd.errorAppendix =
"\n"
"Specification of configuration parameters: GLOBAL_PARAMETERS NUMBER_OF_PROCESSES PROCESS_PARAMETERS_1 PROCESS_PARAMETERS_2 ...\n\n"
"IMPORTANT: All distance parameters are specified in units with a x1000 factor with respect to input file units, unless stated otherwise. Therefore, if the input file is in millimeters, distance parameters are in microns.\n\n"
"GLOBAL_PARAMETERS: ALSO_CONTOURS CORRECT_INPUT USE_MOTION_PLANNER LIMITX LIMITY SCHEDULING_PARAMETERS ADDSUB_FLAG;\n\n"
"    ALSO_CONTOURS: flag ('contours' if true) to also provide the contours in addition to the toolpaths\n\n"
"    CORRECT_INPUT: flag ('correct' if true) to consistently correct the orientation of each contour in the raw input slices if they were not computed with Slic3r::TriangleMeshSlicer.\n\n"
"    USE_MOTION_PLANNER: flag ('motion_opt' if true) to use a very simple (greedy, and without finding optimal entry points for closed contours) motion planner to order toolpaths\n\n"
"    LIMITX and LIMITY: if non-negative, they specify the semi-lengths for a origin-centered rectangle, which will be added as an enclosing contour. The goal is to simulate subtractive processes without handling them as separate code paths.\n\n"
"    SCHEDULING_PARAMETERS: list of scheduling parameters, with variable specifications:\n\n"
"      -if the first parameter in this list is 'sched': sched VERTICAL_CORRECTION Z_EPSILON Z_SEQUENCE\n\n"
"          *VERTICAL_CORRECTION: flag ('vcorrect' if true) to use memory- and cpu- expensive operations to ensure vertical correctness.\n\n"
"          *Z_EPSILON: maximal absolute distance between scheduled slices to consider them to be at the same Z level (required because of floating point errors: if the voxel heights are just right, some slices for different processes may be scheduled at the same height).\n\n"
"          *Z_SEQUENCE: either 'all' or a list of integers, representing the tools to be scheduled for slicing. The first element is the number of tools to be scheduled, the rest are the sequence of scheduled tools. This is required in case you want to schedule only some of the tools.\n\n"
"      -if the first parameter in this list is 'manual': manual VERTICAL_CORRECTION Z_EPSILON NUMLAYERS Z_1 NTOOL_1 Z_2 NTOOL_2 Z_3 NTOOL_3 ...\n\n"
"          *VERTICAL_CORRECTION and Z_EPSILON: the same as in the previous case.\n\n"
"          *NUMLAYERS: number of layers in a sequence specifying a schedule of layers to be processed.\n\n"
"          *Z_i: Z height of the i-th layer in the schedule (in input file units, usually assumed to be in millimeters).\n\n"
"          *NTOOL_i: number of the process to use for the i-th layer in the schedule.\n\n"
"      -if the first parameter in this list is 'uniform': uniform Z_UNIFORM_STEP\n\n"
"          *Z_UNIFORM_STEP: uniform slicing step in input file units, usually assumed to be in millimeters. Not really required if using the shared library interface, but it is kept for consistency.\n\n"
"    ADDSUB_FLAG: flag ('addsub' if true) to specify the use of additive/subtractive logic that considers the first process to be additive, and all subsequent ones to be subtractive (or vice versa). If false, all processes are supposed to be either additive or subtractive.\n\n"
"NUMBER_OF_PROCESSES: number of processes; after it, there will be a sequence of process parameters for each process.\n\n"
"PROCESS_PARAMETERS_i: sequence of parameters for process 'i': LASER_BEAM_RADIUS VOXEL_PARAMETERS GRID_STEP RADIUS_ARCTOLERANCE GRIDSTEP_ARCTOLERANCE NOSNAP_RESOLUTION RADIUS_REMOVECOMMON SNAP_FLAG SAFESTEP_FLAG CLEARANCE_FLAG MEDIALAXIS_PARAMETERS INFILLING_PARAMETERS\n\n"
"    LASER_BEAM_RADIUS: resolution\n\n"
"    VOXEL_PARAMETERS: only if using the 3D scheduler:\n\n"
"      -voxel shape: either 'ellipsoid' or 'constant'\n\n"
"      -voxel Z radius (in order to define the size of the voxel in Z)\n\n"
"      -voxel Z semi height (in order to define the Z step for slicing the model at that resolution)\n\n"
"    GRID_STEP: X/Y stage step\n\n"
"    RADIUS_ARCTOLERANCE: roundness parameter\n\n"
"    GRIDSTEP_ARCTOLERANCE: roundness parameter\n\n"
"    NOSNAP_RESOLUTION: smoothing radius if snapping is not done\n\n"
"    RADIUS_REMOVECOMMON: radius for clipping contour segments common to previous proceses\n\n"
"    SNAP_FLAG: do/do not snapping ('snap' if true) \n\n"
"    SAFESTEP_FLAG: if true ('safestep'), try to minimize the resolution loss from snapping\n\n"
"    CLEARANCE_FLAG: if true ('clearance'), override NOSNAP_RESOLUTION /SAFESTEP_FLAG and avoid the contour toolpaths from drawing overlapping lines to the maximum possible extent\n\n"
"    MEDIALAXIS_PARAMETERS: list of medial axis factors to fill regions not filled by the contours. The first element is the number of factors, the rest are the factors. Example: 2 1.0 0.5 0.1 means 3 factors, 1.0, 0.5 and 0.1\n\n"
"    INFILLING_PARAMETERS:  list of infilling parameters: it is either a single element ('noinfill', meaning that infilling is not considered) or is a three element list:\n\n"
"      -keyword 'infill'\n\n"
"      -infill mode: either 'concentric', 'lines', or 'justcontour' (the last one is to pass the infillings to AutoCAD for the Hatches)\n\n"
"      -flag ('recursive' is True) to try to fill (or not) areas not properly infilled with other means (medial axis, higher resolution processes). Has no effect for infill mode 'justcontour'\n\n"
"      -list of medial axis factors for infilling, just like the previous list of medial axis factors. The first element is the number of factors, the rest are the factors. ;\n\n"
"\n";
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

    this->multispec = new MultiSpec(std::move(global), numtools);

    err = this->multispec->readFromCommandLine(rd, scale);
    if (!err.empty()) {
        return false;
    }

    return true;
}
