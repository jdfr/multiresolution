#include "parsing.hpp"
#include <algorithm>

po::options_description globalOptionsGenerator() {
    po::options_description opts("Slicing engine options (global)");
    opts.add_options()
        ("save-contours,j", "If this option is specified, the processed and raw contours will be provided as output (in addition to the toolpaths)")
        ("correct-input", "If this option is specified, the orientation of raw contours will be corrected. Useful if the raw contours are not generated with Slic3r::TriangleMeshSlicer")
        ("motion-planner,k", "If this option is specified, a very simple motion planner will be used to order the toolpaths (in a greedy way, and without any optimization to select circular contour entry points)")
        ("subtractive-box-mode", po::value<std::vector<int>>()->multitoken()->value_name("lx [ly]"), "If specified, it takes two numbers: LIMIT_X and LIMIT_Y, which are the semi-lengths in X and Y of a box centered on the origin of coordinates (if absent, LIMIT_Y WILL BE ASSUMED TO BE THE SAME AS LIMIT_X). Toolpaths will be generated in the box, EXCEPT for the input mesh file. This can be used as a crude way to generate a shape in a subtractive process. If the input mesh file is not contained within the limits, results are undefined.")
        ("slicing-uniform,u", po::value<double>()->value_name("z_step"), "The input mesh will be sliced uniformly at the specified slicing step. All processed will be applied to every slice. Slices will be processed independently. Should not be used for true multislicing.")
        ("slicing-scheduler,s", po::value<std::vector<int>>()->multitoken()->zero_tokens()->value_name("[ntool_list]"), "Slices for each process will be scheduled according to the Z resolution of each process. Slices of lower-resolution processes will be taken into account for slices of higher-resolution processes. If no values are provided, all specified processes are used in the multislicing process. Otherwise, the values are the indexes of the processes to be used (so that some processes can be specified but not actually used), starting from 0.")
        ("slicing-manual,m", po::value<std::vector<double>>()->multitoken()->value_name("[ntool_1 z_1 ntool2 z_2 ...]"), "Same as slicing-scheduler, but the executing order is specified manually: values are Z_1, NTOOL_1, Z_2, NTOOL_2, Z_3 NTOOL_3, ..., such that for each the i-th scheduled slice is at height Z_i, and is computed with process NTOOL_i.")
        ("vertical-correction", "If specified, the algorithm takes care to avoid toolpaths with big voxels if the object is too thin in Z (only relevant for slicing-scheduler or slicing-manual)")
        ("z-epsilon,l", po::value<double>()->default_value(1e-6)->value_name("z_epsilon"), "For slicing-scheduler or slicing-manual, Z values are considered to be the same if they differ less than this, in the mesh file units")
        ("addsub", "If not specified, the engine considers all processes to be of the same type (i.e., all are either additive or subtractive). If specified, the engine operates in add/sub mode: the first process is considered additive, and all subsequent processes are subtractive (or vice versa)")
        ;
    return opts;
}

po::options_description perProcessOptionsGenerator() {
    po::options_description opts("Slicing engine options (per process)");
    opts.add_options()
        ("process,p", po::value<int>()->required()->value_name("ntool"), "Multiple fabrication processes can be specified, each one with a series of parameters. Each process is identified by a number, starting from 0, without gaps (i.e., if processes with identifiers 0 and 2 are defined, process 1 should also be specified). Processes should be ordered by resolution, so higher-resolution processes should have bigger identifiers. All metric parameters below are specified in mesh units x 1000 (so, if mesh units are millimeters, these are specified in micrometers. See below for an example")
        ("no-preprocessing,c", "If specified, the raw contours are not pre-processed before generating the toolpaths. Useful in some cases such as avoiding corner rounding in low-res processes, but may introduce errors in other cases")
        ("radx,x", po::value<double>()->required()->value_name("length"), "radius of the voxel for the current process in the XY plane")
        ("voxel-profile,v", po::value<std::string>()->value_name("(constant|ellipsoid)"), "required if slicing-scheduler or slicing-manual are specified: the voxel profile can be either 'constant' or 'ellipsoid'")
        ("voxel-z,z", po::value<std::vector<double>>()->multitoken()->value_name("length extent"), "required if slicing-scheduler or slicing-manual are specified: the first value is the voxel radius in Z. The second value is the semiheight in Z (used to the define the slicing step for slicing-scheduler). If the second value is not present, it is implied to be the same as the first value.")
        ("gridstep,g", po::value<double>()->required()->value_name("step"), "grid step for the current process (this is the minimal amount the head can be moved in XY)")
        ("snap,n", "If this is specified, contours are snapped to a grid centered in the origin and with the step specified in gridstep. Otherwise, no snapping is done.")
        ("safestep,f", "If specified, and gridstep is specified, the engine tries to minimize the resolution loss caused by snapping")
        ("clearance,c", "If specified, the current process is computed such that toolpaths cannot overlap")
        ("smoothing,o", po::value<double>()->value_name("length"), "If snap is not specified and clearance is not specified for the current process, this MUST be specified, and it is the smoothing radius for the computed contours")
        ("tolerances,t", po::value<std::vector<double>>()->multitoken()->value_name("tol_radx tol_gridstep"), "Values of the roundness parameters for the current process. The first value is for the XY radius scale, the second for the gridstep scale (if the second value is omitted, it is copied from the first one).")
        ("radius-removecommon,e", po::value<double>()->default_value(0.0)->value_name("length"), "If a positive value is specified, contours are clipped in zones where already-computed low-res contours are nearer than this value")
        ("medialaxis-radius,a", po::value<std::vector<double>>()->multitoken()->value_name("list of 0..1 factors"), "If specified, it is a series of factors in the range 0.0-1.0. The following algorithm is applied for each factor: toolpaths following the medial axis of the contours are generated in regions of the raw contours that are not covered by the processed contours, in order to minimize such non-covered regions. The lower the factor, the more likely the algorithm is to add a toolpath.")
        ("infill,i", po::value<std::string>()->value_name("(linesh|linesv|concentric|justcontour)"), "If specified, the value must be either 'linesh'/'linesv' (infilling is done with horizontal/vertical lines), 'concentric', (infilling is done with concentric toolpaths), or 'justcontour' (this is useful for the shared-library use case: infillings will be generated outside the engine; the engine just provides the contours to be infilled)")
        ("infill-byregion", "If specified, and --infill (lines|hlinesv) is specified, the infill lines are computed separately for each different region (slower, but more regular results may be obtained), instead of for all of them at once (faster, but infillings may be irregular in some cases)")
        ("infill-recursive,r", "If specified, infilling with higher resolution processes is applied recursively in the parts of the processed contours not convered by infilling toolpaths for the current process (useful only for --infill (linesh|linesv|concentric))")
        ("infill-medialaxis-radius,d", po::value<std::vector<double>>()->multitoken()->value_name("list of 0..1 factors"), "Same as medialaxis-radius, but applied to regions not covered by infillings inside processed contours, if --infill and --infill-recursive are specified")
        ;
    return opts;
}

//it would be good practice to have these variables as state in a thread-safe singleton, to be created only if needed.
static const po::options_description     globalOptionsStatic = globalOptionsGenerator();
static const po::options_description perProcessOptionsStatic = perProcessOptionsGenerator();

const po::options_description *     globalOptions() { return &globalOptionsStatic; }
const po::options_description * perProcessOptions() { return &perProcessOptionsStatic; }

// Additional command line parser which interprets '@something' as a
// option "config-file" with the value "something"
std::pair<std::string, std::string> at_option_parser(std::string const&s) {
    if ('@' == s[0])
        return std::make_pair(std::string(RESPONSE_FILE_OPTION), s.substr(1));
    else
        return std::pair<std::string, std::string>();
}

std::string parseAndInsertResponseFileOptions(po::options_description &opts, const po::positional_options_description &posit, std::vector<std::string> &args, const char * CommandLineOrigin, po::parsed_options &result) {
    po::parsed_options original = po::command_line_parser(args).options(opts).positional(posit).extra_parser(at_option_parser).run();
    result.m_options_prefix = original.m_options_prefix;
    for (auto &option : original.options) {
        if (strcmp(RESPONSE_FILE_OPTION, option.string_key.c_str()) == 0) {
            if (option.value.empty()) return str("error ", CommandLineOrigin, ": cannot use response-file option without filename value");
            bool ok;
            std::string responsecontents = get_file_contents(option.value[0].c_str(), ok);
            if (!ok) return str("error: ", responsecontents);
            auto addArgs = normalizedSplit(responsecontents);
            std::string origin = str("in file ", option.value[0]);
            std::string res = parseAndInsertResponseFileOptions(opts, posit, addArgs, origin.c_str(), result);
            if (!res.empty()) return res;
        } else {
            result.options.push_back(std::move(option));
        }
    }
    return std::string();
}

po::parsed_options parseCommandLine(po::options_description &opts, const po::positional_options_description &posit, const char *CommandLineOrigin, std::vector<std::string> &args) {
    po::parsed_options result(&opts);
    if (CommandLineOrigin == NULL) CommandLineOrigin = "while parsing parameters";
    std::string res = parseAndInsertResponseFileOptions(opts, posit, args, CommandLineOrigin, result);
    if (!res.empty()) throw std::runtime_error(res);
    return result;
}

bool charArrayComparer(const char *a, const char *b) { return strcmp(a, b) < 0; }

std::vector<po::parsed_options> sortOptions(std::vector<const po::options_description*> &optss, const po::positional_options_description &posit, int positionalArgumentsIdx, const char *CommandLineOrigin, std::vector<std::string> &args) {
    po::options_description cmdline_options;
    for (auto &opts : optss) {
        cmdline_options.add(*opts);
    }
    po::parsed_options alloptions = parseCommandLine(cmdline_options, posit, CommandLineOrigin, args);
    std::vector<po::parsed_options>  sortedoptions;
    std::vector<std::vector<const char *>> sortedoptionNames;
    sortedoptions    .reserve(optss.size());
    sortedoptionNames.reserve(optss.size());
    for (auto &opts : optss) {
        sortedoptions.push_back(po::parsed_options(opts, alloptions.m_options_prefix));
        sortedoptions.back().options.reserve(alloptions.options.size());
        sortedoptionNames.push_back(std::vector<const char *>());
        auto ops = opts->options();
        sortedoptionNames.back().reserve(ops.size());
        for (auto & op : ops) {
            sortedoptionNames.back().push_back(op->long_name().c_str());
        }
        std::sort(sortedoptionNames.back().begin(), sortedoptionNames.back().end(), charArrayComparer);
    }
    for (auto &option : alloptions.options) {
        if (option.unregistered) {
            throw std::runtime_error(str("Unrecognized option: ", option.string_key));
        }
        if (option.position_key >= 0) {
            sortedoptions[positionalArgumentsIdx].options.push_back(std::move(option));
        } else {
            bool found = false;
            int k = 0;
            //for (auto &opts : optss) {
            //    found = opts->find_nothrow(option.string_key, false) != NULL;
            for (auto &names : sortedoptionNames) {
                found = std::binary_search(names.begin(), names.end(), option.string_key.c_str(), charArrayComparer);
                if (found) {
                    sortedoptions[k].options.push_back(std::move(option));
                    break;
                }
                ++k;
            }
            //this exception should never be thrown
            if (!found) throw std::runtime_error(str("Option recognized but not processed: ", option.string_key));
        }
    }
    return sortedoptions;
}

std::string parseGlobal(GlobalSpec &spec, po::parsed_options &optionList, double scale) {
    bool doscale = scale != 0.0;
    po::variables_map vm;
    try {
        po::store(optionList, vm);
    } catch (std::exception &e) {
        return std::string(e.what());
    }
    spec.alsoContours             = vm.count("save-contours")       != 0;
    spec.correct                  = vm.count("correct-input")       != 0;
    spec.applyMotionPlanner       = vm.count("motion-planner")      != 0;
    spec.addsubWorkflowMode       = vm.count("addsub")              != 0;
    spec.avoidVerticalOverwriting = vm.count("vertical-correction") != 0;
    if (vm.count("subtractive-box-mode")) {
        auto &vals = vm["subtractive-box-mode"].as<std::vector<int>>();
        if (vals.size() == 0) {
            return std::string("Error: specified subtractive-box-mode without values!");
        } else {
            spec.limitX = vals[0];
            spec.limitY = vals.size() > 1 ? vals[1] : vals[0];
            if (doscale) {
                spec.limitX = (clp::cInt)(spec.limitX*scale);
                spec.limitY = (clp::cInt)(spec.limitY*scale);
            }
        }
    } else {
        spec.limitX = spec.limitY = -1;
    }
    bool schedSet = false;
    const char * schedRepErr = "trying to specify more than one of these options: slicing-uniform, slicing-scheduler, slicing-manual";
    if (vm.count("slicing-uniform")) {
        if (schedSet) return std::string(schedRepErr);
        spec.schedMode      = UniformScheduling;
        spec.z_uniform_step = vm["slicing-uniform"].as<double>();
        schedSet            = true;
    };
    if (vm.count("slicing-scheduler")) {
        if (schedSet) return std::string(schedRepErr);
        spec.schedMode = SimpleScheduler;
        spec.schedTools = std::move(vm["slicing-scheduler"].as<std::vector<int>>());
        schedSet        = true;
    };
    if (vm.count("slicing-manual")) {
        if (schedSet) return std::string(schedRepErr);
        spec.schedMode = ManualScheduling;
        auto &vals     = std::move(vm["slicing-manual"].as<std::vector<double>>());
        if ((vals.size() % 2) != 0) {
            return str("slicing-manual must have an even number of values, but it has ", vals.size());
        }
        spec.schedSpec.reserve(vals.size() / 2);
        for (auto val = vals.begin(); val != vals.end(); ++val) {
            double z = *(val++);
            int ntool = (int)*val;
            if (ntool != *val) return str("Invalid slicing-manual value: for Z ", z, "the tool is not an integer: ", *val);
            spec.schedSpec.push_back(GlobalSpec::ZNTool(z, ntool));
        }
        schedSet = true;
    };
    if (!schedSet) return std::string("Exactly one of these options must be set: slicing-uniform, slicing-scheduler, slicing-manual");
    spec.useScheduler = spec.schedMode != UniformScheduling;
    if (vm.count("z-epsilon")) {
        spec.z_epsilon = getScaled(vm["z-epsilon"].as<double>(), scale, doscale);
    };
    return std::string();
}

//this method CANNOT be called until GlobalSpec::parseOptions has been called
std::string parsePerProcess(MultiSpec &spec, po::parsed_options &optionList, double scale) {
    typedef std::pair<int, po::parsed_options> OptionsByTool;
    std::vector<OptionsByTool> optionsByTool;
    optionsByTool.reserve(3); //reasonable number to reserve
    std::vector<int> processIds;
    int maxProcess = 0;
    //separate global options, and sort per-process options according to the previous --process option
    for (auto & option : optionList.options) {
        if (option.string_key.compare("process") == 0) {
            if (option.value.empty()) return std::string("process option must have a value!");
            char *endptr; int param = (int)strtol(option.value[0].c_str(), &endptr, 10);
            if (param<0) return std::string("process option cannot have negative value: ", param);
            if ((*endptr) != 0) return str("process option value must be a non-negative integer: ", option.value[0]);
            processIds.push_back(param);
            optionsByTool.push_back(OptionsByTool(param, po::parsed_options(perProcessOptions(), optionList.m_options_prefix)));
            if (maxProcess < processIds.back()) maxProcess = processIds.back();
        }
        if (optionsByTool.empty()) {
            return str("option ", option.string_key, " cannot be specified before option --process");
        }
        optionsByTool.back().second.options.push_back(std::move(option));
    }
    //some sanity checks
    if (optionsByTool.empty()) {
        return std::string("Cannot work without process parameters");
    }
    {
        std::vector<bool> visited(maxProcess + 1, false);
        for (int k = 0; k < visited.size(); ++k) {
            int i = processIds[k];
            if (!visited[i]) visited[i] = true;
        }
        int notvisited = -1;
        for (int k = 0; k < visited.size(); ++k) {
            if (!visited[k]) {
                notvisited = k;
                break;
            }
        }
        if (notvisited >= 0) {
            return str("process with id ", maxProcess, "was specified, but process with id ", notvisited, " was not specified!");
        }
    }

    spec.initializeVectors(maxProcess + 1);

    bool doscale = scale != 0.0;
    for (auto & optionsListByTool : optionsByTool) {
        po::variables_map vm;
        try {
            po::store(optionsListByTool.second, vm);
        } catch (std::exception &e) {
            return str("for process ", optionsListByTool.first, ": ", e.what());
        }

        int k = optionsListByTool.first;

        spec.radiuses            [k] = (clp::cInt)getScaled(vm["radx"]               .as<double>(), scale, doscale);
        spec.gridsteps           [k] = (clp::cInt)getScaled(vm["gridstep"]           .as<double>(), scale, doscale);
        spec.radiusesRemoveCommon[k] = (clp::cInt)getScaled(vm["radius-removecommon"].as<double>(), scale, doscale);
        if (vm.count("tolerances")) {
            const std::vector<double> & val = vm["tolerances"].as<std::vector<double>>();
            spec.arctolRs[k] = (clp::cInt)getScaled(val[0], scale, doscale);
            spec.arctolGs[k] = val.size() == 1 ? spec.arctolRs[k] : (clp::cInt)getScaled(val[1], scale, doscale);
        } else {
            spec.arctolRs[k] = (spec.radiuses[k] / 100);
            spec.arctolGs[k] = (spec.radiuses[k] / 10);
        }
        spec.burrLengths[k] = (clp::cInt) (vm.count("smoothing") ? getScaled(vm["smoothing"].as<double>(), scale, doscale) : spec.arctolRs[k]);

        if (spec.global.useScheduler) {
            auto requireds = { "voxel-profile", "voxel-z" };
            for (auto &required : requireds) if (vm.count(required) == 0) return str("slicing-manual or slicing-scheduler are specified, but ", required, " is missing for process ", k);
            const std::string &valp = vm["voxel-profile"].as<std::string>();
            bool voxelIsEllipsoid = valp.compare("ellipsoid") == 0;
            bool voxelIsConstant = valp.compare("constant") == 0;
            if ((!voxelIsEllipsoid) && (!voxelIsConstant)) return str("for process ", k, ": invalid value for voxel-profile: ", valp);
            const std::vector<double> &valz = vm["voxel-z"].as<std::vector<double>>();
            double ZRadius = getScaled(valz[0], scale, doscale);
            double ZSemiHeight = valz.size() == 1 ? ZRadius : getScaled(valz[1], scale, doscale);
            if (voxelIsEllipsoid) {
                spec.profiles[k] = std::make_shared<EllipticalProfile>((double)spec.radiuses[k], ZRadius, 2 * ZSemiHeight);
            } else {
                spec.profiles[k] = std::make_shared<ConstantProfile>  ((double)spec.radiuses[k], ZRadius, 2 * ZSemiHeight);
            }
        }
        spec.applysnaps[k]            = vm.count("snap")      != 0;
        spec.snapSmallSafeSteps[k]    = vm.count("safestep")  != 0;
        spec.addInternalClearances[k] = vm.count("clearance") != 0;
        spec.doPreprocessing[k]       = vm.count("no-preprocessing,") == 0;

        //if (vm.count("smoothing")) {
        //    spec.burrLengths[k] = (clp::cInt)getScaled(vm["smoothing"].as<double>(), scale, doscale);
        //} else {
        //    if ((!spec.applysnaps[k]) && (!spec.addInternalClearances[k])) {
        //        return str("For process ", k, ": If option --snap is not specified and option --clearance is not specified, option --smoothing MUST be specified");
        //    }
        //}

        if (vm.count("medialaxis-radius")) {
            spec.medialAxisFactors[k] = std::move(vm["medialaxis-radius"].as<std::vector<double>>());
        }

        bool useinfill = vm.count("infill") != 0;
        if (useinfill) {
            const std::string & val = vm["infill"].as<std::string>();
            if      (val.compare("concentric")  == 0) spec.infillingModes[k] = InfillingConcentric;
            else if (val.compare("linesh")      == 0) spec.infillingModes[k] = InfillingRectilinearH;
            else if (val.compare("linesv")      == 0) spec.infillingModes[k] = InfillingRectilinearV;
            else if (val.compare("justcontour") == 0) spec.infillingModes[k] = InfillingJustContours;
            else                                      return str("For process ", k, ": invalid infill mode: ", val);
            spec.infillingRecursive[k] = vm.count("infill-recursive") != 0;
            spec.infillingWhole    [k] = vm.count("infill-byregion")  == 0;
            if (vm.count("infill-medialaxis-radius")) {
                spec.medialAxisFactorsForInfillings[k] = std::move(vm["infill-medialaxis-radius"].as<std::vector<double>>());
            }
        } else {
            spec.infillingModes[k] = InfillingNone;
        }
    }
    return spec.populateParameters();
}

std::string parseAll(MultiSpec &spec, po::parsed_options &globalOptionList, po::parsed_options &perProcOptionList, double scale) {
    std::string err = parseGlobal(spec.global, globalOptionList, scale);
    if (!err.empty()) return err;
    return parsePerProcess(spec, perProcOptionList, scale);
}

std::string parseAll(MultiSpec &spec, const char *CommandLineOrigin, std::vector<std::string> &args, double scale) {
    try {
        std::vector<const po::options_description*> optss = { globalOptions(), perProcessOptions() };
        po::positional_options_description emptypos;
        auto sorted = sortOptions(optss, emptypos, 0, CommandLineOrigin, args);
        return parseAll(spec, sorted[0], sorted[1], scale);
    } catch (std::exception & e) {
        return std::string(e.what());
    }
}

void composeParameterHelp(bool globals, bool perProcess, bool example, std::ostream &output) {
    if (globals || perProcess) output << "The multislicing engine is very flexible.\n  It takes parameters as if it were a command line application.\n  Some options have long and short names.\n  If there is no ambiguity, options can be specified as prefixes of their full names.\n";
    if (globals)        globalOptions()->print(output);
    if (perProcess) perProcessOptions()->print(output);
    if (example && (globals || perProcess)) {
        output << "\nExample:";
        if (globals) output << " --save-contours --motion-planner --slicing-scheduler";
        if (perProcess) output << " --process 0 --radx 75 --voxel-profile constant --voxel-z 75 67.5 --gridstep 0.1 --snap --smoothing 0.01 --tolerances 0.75 0.01 --safestep --clearance --medialaxis-radius 1.0  --process 1 --radx 10 --voxel-profile constant --voxel-z 10 9 --gridstep 0.1 --snap --smoothing 0.1 --tolerances 0.1 0.001 --safestep --clearance --medialaxis-radius 1.0";
        output << "\n";
    }
}

void composeParameterHelp(bool globals, bool perProcess, bool example, std::string &output) {
    std::ostringstream fmt;
    composeParameterHelp(globals, perProcess, example, fmt);
    output = fmt.str();
}

std::vector<std::string> getArgs(int argc, const char ** argv, int numskip) {
    std::vector<std::string> args;
    if (argc > numskip) {
        args.reserve(argc - numskip);
        for (int k = numskip; k < argc; ++k) args.push_back(std::string(argv[k]));
    }
    return args;
}

std::string getScale(bool doscale, Configuration &config, double &scale) {
    scale = 0.0;
    if (doscale) {
        if (config.hasKey("PARAMETER_TO_INTERNAL_FACTOR")) {
            std::string val = config.getValue("PARAMETER_TO_INTERNAL_FACTOR");
            char *endptr;
            scale = strtod(val.c_str(), &endptr);
            if (*endptr != 0) return str("cannot parse <", val, "> into a double value");
        } else {
            return std::string("doscale flag is true, but there was not PARAMETER_TO_INTERNAL_FACTOR in the configuration");
        }
    }
    return std::string();
}
