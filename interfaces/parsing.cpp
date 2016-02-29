#include "parsing.hpp"
#include "pathsfile.hpp"

inline double    getScaled(double    val, double scale, bool doscale) { return doscale ? val*scale : val; }
inline clp::cInt getScaled(clp::cInt val, double scale, bool doscale) { return doscale ? (clp::cInt)(val*scale) : val; }

po::options_description globalOptionsGenerator() {
    po::options_description opts("Slicing engine options (global)");
    opts.add_options()
        ("save-contours,j",
            "If this option is specified, the processed and raw contours will be provided as output (in addition to the toolpaths)")
        ("correct-input",
            "If this option is specified, the orientation of raw contours will be corrected. Useful if the raw contours are not generated with Slic3r::TriangleMeshSlicer")
        ("motion-planner,k",
            "If this option is specified, a very simple motion planner will be used to order the toolpaths (in a greedy way, and without any optimization to select circular contour entry points)")
        ("subtractive-box-mode",
            po::value<std::vector<int>>()->multitoken()->value_name("lx [ly]"),
            "If specified, it takes two numbers: LIMIT_X and LIMIT_Y, which are the semi-lengths in X and Y of a box centered on the origin of coordinates (if absent, LIMIT_Y WILL BE ASSUMED TO BE THE SAME AS LIMIT_X). Toolpaths will be generated in the box, EXCEPT for the input mesh file. This can be used as a crude way to generate a shape in a subtractive process. If the input mesh file is not contained within the limits, results are undefined.")
        ("slicing-uniform,u",
            po::value<double>()->value_name("z_step"),
            "The input mesh will be sliced uniformly at the specified slicing step. All processes will be applied to every slice. Slices will be processed independently. Should not be used for true multislicing. The slicing step may be negative.")
        ("slicing-scheduler,s",
            po::value<std::vector<int>>()->multitoken()->zero_tokens()->value_name("[ntool_list]"),
            "Slices for each process will be scheduled according to the Z resolution of each process. Slices of lower-resolution processes will be taken into account for slices of higher-resolution processes. If no values are provided, all specified processes are used in the multislicing process. Otherwise, the values are the indexes of the processes to be used (so that some processes can be specified but not actually used), starting from 0.")
        ("slicing-manual,m",
            po::value<std::vector<double>>()->multitoken()->value_name("[ntool_1 z_1 ntool2 z_2 ...]"),
            "Same as slicing-scheduler, but the executing order is specified manually: values are NTOOL_1, Z_1, NTOOL_2, Z_2, NTOOL_3, Z_3 ..., such that for each the i-th scheduled slice is at height Z_i, and is computed with process NTOOL_i.")
        ("slicing-zbase",
            po::value<double>()->value_name("z_base"),
            "If --slicing-uniform is specified, and this parameter is specified, it is the Z position of the first slice, in mesh file units.")
        ("slicing-direction",
            po::value<std::string>()->default_value("up")->value_name("(up|down)"),
            "If --slicing-scheduler is specified, this specifies if the slicing is done from the bottom-up ('up'), or vice versa (for --slicing-uniform, the direction is implicit in the sign of the z step). It also determines the order of the output slices, even if using --slicing-manual")
        ("vertical-correction",
            "If specified, the algorithm takes care to avoid toolpaths with big voxels if the object is too thin in Z (only relevant for slicing-scheduler or slicing-manual)")
        ("z-epsilon,l",
            po::value<double>()->default_value(1e-6)->value_name("z_epsilon"),
            "For slicing-scheduler or slicing-manual, Z values are considered to be the same if they differ less than this, in the mesh file units")
        ("addsub",
            "If not specified, the engine considers all processes to be of the same type (i.e., all are either additive or subtractive). If specified, the engine operates in add/sub mode: the first process is considered additive, and all subsequent processes are subtractive (or vice versa). By itself, addsub mode does not work: more options must be set. For high-res negative details, set the global option 'neg-closing'. For high-res positive details, either set the global option 'overwrite-gradual' or (if 'clearance' is not being used) set 'infill-medialaxis-radius' for process 0 to one or several very low values (0.5 to 0.01).")
        ("neg-closing",
            po::value<double>()->value_name("radius"),
            "If addsub mode is activated, high-res details should be processed in process 0. This option applies a morphological closing before any other operation to contours for the first process, with the idea of overwriting all high-res negative details, which should be re-created later by other processes. The value is the radius of the dilation in mesh file units x 1000 (the factor can be modified in the config file), and it can be tuned to make the operation to overwrite more or less negative details.")
        ("overwrite-gradual",
            po::value<std::vector<double>>()->multitoken()->value_name("[rad_1 inf_1 rad_2 inf_2 ...]"),
            "If addsub mode is activated, high-res details should be processed in process 0. This option overwrites high-res positive details trying to minimize the overwritten area. Values are given as pairs of factors in the range [0,1] of the radius of the process 0 (or twice the radius, if clearance is being used). The first elements of the pairs are widths and should decrease in the range (1,0], while the second elements are inflation ratios and should increase in the range [0,1]. The members of each pair should add up to at least 1. In effect, the sequence of pairs determines a sequence of partially inflated segments. As more steps are used, the overwriting is more gradual, but also more expensive to compute. The fastest setting is to use just the pair 0 1; while this creates very smooth toolpaths (w.r.t. more complex pair sequences), it also generates really big overwrites everywhere. For geometries with long protusions that are narrow at the base, the first pairs should add up substantially over 1, in order to be able to overwrite these protusions (unfortunately, this may result in subtle small overwritings elsewhere). The longer the pair sequence, the less overwriting is generated. In general, this option is quite versatile, but may require a trial-and-error process to settle on a pair sequence that works correctly for some geometries. PLEASE NOTE: using this option renders unnecessary the use of --medialaxis-radius (but not --infill-medialaxis-radius)")
        ("feedback,b",
            po::value<std::vector<std::string>>()->multitoken(),
            "If the first manufacturing process has low fidelity (thus, effectively containing errors at high-res), we need as feedback the true manufactured shape, up to date. With this option, the feedback can be provided offline (i.e., low-res processes have been computed and carried out before using offline feedback). This option takes two values. The first is the format of the feedback file: either 'mesh' (stl) or 'paths' (*.paths format). The second is the feedback file name itself.")
        ("response-file",
            po::value<std::string>(),
            "file with additional parameters (for any purpose, not only local/global stuff), can be specified with '@filename', too. Parameters are inserted in-line, so please pay attention to positional parameters")
        ;
    return opts;
}

#define PER_PROCESS_NANOPREFIX "pp-"
#define PREFIXNANONAME(x) (GLOBAL ? x :  PER_PROCESS_NANOPREFIX x)
#define PREFIXNANODESC(x) (GLOBAL ? x : "per-process " x)
#define GET_GLOBALNAME_IN_LOCALCONTEXT(x) (x+sizeof(PER_PROCESS_NANOPREFIX)-1)

template<bool GLOBAL> void nanoOptionsGenerator(po::options_description &opts) {
    if (GLOBAL) {
        opts.add_options()
            ("nanoscribe",
                po::value<std::string>()->value_name("filename"),
                "save toolpaths in *.gwl format. The slices are split to allow objects bigger than 300um to be built. A different GWL file for each region is created (or as many as processes if --nano-global is not specified). These are called 'regional' GWL files. Also, one global GWL file is created (or one for each process if --nano-global is not specified), that includes all corresponding regional GWL files. For each global GWL file, there is also a global debug GWL file, which adds delimiting squares and text to be able to identify individual regional GWL files. If this option is used, the GWL output configuration must be specified with other options, all having prefix nano-*, or pp-nano-* if specified per process.")
            ("nano-global",
                "If this option is specified, all nano-* options described below are accepted, and the nanoscribe configuration is global for all processes. Otherwise, each process has its own nanoscribe configuration, which can be customized with pp-nano-* options. Nanoscribe options pp-nano-tool-* must be set separately for each process even if this option is specified. If this option IS NOT specified, nano-by-tool is IMPLICITLY specified.")
            ("nano-by-tool",
                "If this option is specified, a different GWL file is generated for each process. This is IMPLICITLY specified if nano-global IS NOT specified. This option can be set only globally, not per process.")
            ("nano-by-z",
                "If this option is specified, a different GWL file is generated for each Z value. This option can be set only globally, not per process.")
            ;
    } else {
        opts.add_options()
            (PREFIXNANONAME("nano-tool-begin"),
                po::value<std::string>()->value_name("filename"),
                PREFIXNANODESC("GWL commands to be written in each regional GWL file if a toolchange happens. This is only relevant if (a) nano-global IS specified, AND (b) nano-by-tool IS NOT specified."))
            (PREFIXNANONAME("nano-tool-end"),
                po::value<std::string>()->value_name("filename"),
                PREFIXNANODESC("GWL commands to be written in each regional GWL file if a toolchange happens. This is only relevant if (a) nano-global IS specified, AND (b) nano-by-tool IS NOT specified."))
            ;
    }
    opts.add_options()
        (PREFIXNANONAME("nano-file-begin"),
            po::value<std::string>()->value_name("script"),
            PREFIXNANODESC("GWL commands to be written at the beginning of each REGIONAL GWL file, but AFTER doing any positioning with StageGoto* commands (useful to insert findInterfaceAt commands)."))
        (PREFIXNANONAME("nano-file-end"),
            po::value<std::string>()->value_name("script"),
            PREFIXNANODESC("GWL commands to be written at the end of each REGIONAL GWL file"))
        (PREFIXNANONAME("nano-global-file-begin"),
            po::value<std::string>()->value_name("script"),
            PREFIXNANODESC("GWL commands to be written at the beginning of each GLOBAL GWL file, but AFTER doing any positioning with StageGoto* commands (useful to insert findInterfaceAt commands)."))
        (PREFIXNANONAME("nano-global-file-end"),
            po::value<std::string>()->value_name("script"),
            PREFIXNANODESC("GWL commands to be written at the end of each GLOBAL GWL file"))
        (PREFIXNANONAME("nano-scanmode"),
            po::value<std::string>()->value_name("piezo|galvo"),
            PREFIXNANODESC("nanoscribe scan mode, either 'piezo' or 'galvo'. Default value is 'galvo'"))
        (PREFIXNANONAME("nano-galvocenter"),
            po::value<std::string>()->value_name("always|minimize"),
            PREFIXNANODESC("nanoscribe galvo centering mode, either 'always' or 'minimize'. Default value is 'always'."))
        (PREFIXNANONAME("nano-angle"),
            po::value<double>()->value_name("angle"),
            PREFIXNANODESC("stitching angle (IN DEGREES) between blocks. Default is 90."))
        (PREFIXNANONAME("nano-spacing"),
            po::value<double>()->value_name("length"),
            PREFIXNANODESC("spacing (in nanoscribe units) for each writing subdomain, in nanoscribe units. The effective size of each writing subdomain is nano-spacing+2*nano-margin. If nano-scanmode is 'galvo', this effective size should be configured according to the range of the galvo (taking into account the lens being used). If nano-angle is not 90, this effective size should be configured according to the maximum height of the slices (otherwise, suboptimal movements may occur)."))
        (PREFIXNANONAME("nano-margin"),
            po::value<double>()->value_name("length"),
            PREFIXNANODESC("nanoscribe ovewrite margin, in nanoscribe units. Because the stage positioning of the nanoscribe has a repeat accuracy in the micrometer range, it is advisable to overwrite a little bit the borders of each block, to facilitate the binding between blocks."))
        (PREFIXNANONAME("nano-maxsquarelen"),
            po::value<double>()->value_name("length"),
            PREFIXNANODESC("maximum extent of the addressable area (in nanoscribe units). This MUST be equal or greater than nano-spacing+2*nano-margin, but it is recommended to be strictly greater than it, to avoid errors due to rounding and snapping of coordinates."))
        (PREFIXNANONAME("nano-origin"),
            po::value<std::vector<double>>()->multitoken()->value_name("X Y"),
            PREFIXNANODESC("nanoscribe block origin coordinates (in nanoscribe units). This option determines the partition of the toolpaths into blocks (because of nanoscribe's limited range in galvo/piezo modes). If this option is specified, it is the coordinates of the origin of a grid of blocks, spaced with size nano-spacing. If this option is not specified, the grid is defined to have blocks spaced with a size at most nano-spacing, such that the blocks are fitted to the bounding box of the 3D mesh."))
        (PREFIXNANONAME("nano-gridstep"),
            po::value<double>()->value_name("length"),
            PREFIXNANODESC("nanoscribe grid step (in nanoscribe units). The multislicer computes the slices with a different (usually bigger) resolution than nanoscribe. As a result, it may output a large amount of points which cannot be resolved separately in nanoscribe. If this option is specified, the paths are snapped to a grid centered on the origin, and the grid step is the value of this option."))
        ;
}

po::options_description perProcessOptionsGenerator() {
    po::options_description opts("Slicing engine options (per process)");
    opts.add_options()
        ("process,p",
            po::value<int>()->required()->value_name("ntool"),
            "Multiple fabrication processes can be specified, each one with a series of parameters. Each process is identified by a number, starting from 0, without gaps (i.e., if processes with identifiers 0 and 2 are defined, process 1 should also be specified). Processes should be ordered by resolution, so higher-resolution processes should have bigger identifiers. All metric parameters below are specified in mesh units x 1000 (the factor can be modified in the config file) so, if mesh units are millimeters, these are specified in micrometers. See below for an example")
        ("no-preprocessing",
            po::value<double>()->implicit_value(0.0)->value_name("rad"),
            "If specified, the raw contours are not pre-processed before generating the toolpaths. If a non-zero value 'rad' is specified, two consecutive offsets are done, the first with '-rad', the second with 'rad'. Useful in some cases such as avoiding corner rounding in low-res processes, but may introduce errors in other cases")
        ("no-toolpaths",
            "If specified, the toolpaths are not computed, and the contours are computed without taking into account the toolpaths (they are not smoothed out by the tool radius). This is useful if the toolpaths are not relevant, and it is better to have the full contour as output.")
        ("radx,x",
            po::value<double>()->required()->value_name("length"),
            "radius of the voxel for the current process in the XY plane")
        ("voxel-profile,v",
            po::value<std::string>()->value_name("(constant|ellipsoid)"),
            "required if slicing-scheduler or slicing-manual are specified: the voxel profile can be either 'constant' or 'ellipsoid'")
        ("voxel-z,z",
            po::value<std::vector<double>>()->multitoken()->value_name("length extent"),
            "required if slicing-scheduler or slicing-manual are specified: the first value is the voxel radius in Z. The second value is the semiheight in Z (used to the define the slicing step for slicing-scheduler). If the second value is not present, it is implied to be the same as the first value.")
        ("gridstep,g",
            po::value<double>()->required()->value_name("step"),
            "grid step for the current process (this is the minimal amount the head can be moved in XY)")
        ("snap,n",
            "If specified, contours are snapped to a grid centered in the origin and with the step specified in gridstep. Otherwise, no snapping is done.")
        ("safestep,f",
            "If specified, and gridstep is specified, the engine tries to minimize the resolution loss caused by snapping")
        ("clearance,c",
            "If specified, the current process is computed such that toolpaths cannot overlap")
        ("smoothing,o",
            po::value<double>()->value_name("length"),
            "If snap is not specified and clearance is not specified for the current process, this MUST be specified, and it is the smoothing radius for the computed contours")
        ("tolerances,t",
            po::value<std::vector<double>>()->multitoken()->value_name("tol_radx tol_gridstep"),
            "Values of the roundness parameters for the current process. The first value is for the XY radius scale, the second for the gridstep scale (if the second value is omitted, it is copied from the first one).")
        ("radius-removecommon,e",
            po::value<double>()->default_value(0.0)->value_name("length"),
            "If a positive value is specified, contours are clipped in zones where already-computed low-res contours are nearer than this value")
        ("medialaxis-radius,a",
            po::value<std::vector<double>>()->multitoken()->value_name("list of 0..1 factors"),
            "If specified, it is a series of factors in the range 0.0-1.0. The following algorithm is applied for each factor: toolpaths following the medial axis of the contours are generated in regions of the raw contours that are not covered by the processed contours, in order to minimize such non-covered regions. The lower the factor, the more likely the algorithm is to add a toolpath.")
        ("infill,i",
            po::value<std::string>()->value_name("(linesh|linesv|concentric|justcontour)"),
            "If specified, the value must be either 'linesh'/'linesv' (infilling is done with horizontal/vertical lines), 'concentric', (infilling is done with concentric toolpaths), or 'justcontour' (this is useful for the shared-library use case: infillings will be generated outside the engine; the engine just provides the contours to be infilled)")
        ("infill-byregion",
            "If specified, and --infill (lines|hlinesv) is specified, the infill lines are computed separately for each different region (slower, but more regular results may be obtained), instead of for all of them at once (faster, but infillings may be irregular in some cases)")
        ("infill-recursive,r",
            "If specified, infilling with higher resolution processes is applied recursively in the parts of the processed contours not convered by infilling toolpaths for the current process (useful only for --infill (linesh|linesv|concentric))")
        ("infill-medialaxis-radius,d",
            po::value<std::vector<double>>()->multitoken()->value_name("list of 0..1 factors"),
            "Same as medialaxis-radius, but applied to regions not covered by infillings inside processed contours, if --infill and --infill-recursive are specified")
        ;
    nanoOptionsGenerator<false>(opts);
    return opts;
}

po::options_description nanoGlobalOptionsGenerator() {
    po::options_description opts("Nanoscribe global options");
    nanoOptionsGenerator<true>(opts);
    return opts;
}


//it would be good practice to have these variables as state in a thread-safe singleton, to be created only if needed.
static const po::options_description     globalOptionsStatic = globalOptionsGenerator();
static const po::options_description perProcessOptionsStatic = perProcessOptionsGenerator();
static const po::options_description nanoGlobalOptionsStatic = nanoGlobalOptionsGenerator();

const po::options_description *     globalOptions() { return &globalOptionsStatic; }
const po::options_description * perProcessOptions() { return &perProcessOptionsStatic; }
const po::options_description * nanoGlobalOptions() { return &nanoGlobalOptionsStatic; }

// Additional command line parser which interprets '@something' as a
// option "config-file" with the value "something"
std::pair<std::string, std::string> at_option_parser(std::string const&s) {
    if ('@' == s[0])
        return std::make_pair(std::string("response-file"), s.substr(1));
    else
        return std::pair<std::string, std::string>();
}

po::parsed_options parseOptions(po::options_description &opts, const po::positional_options_description &posit, std::vector<std::string> &args) {
    auto parser = po::command_line_parser(args).options(opts).extra_parser(at_option_parser);
    if (posit.max_total_count() > 0) {
        parser.positional(posit);
    }
    return parser.run();
}

std::string parseAndInsertResponseFileOptions(po::options_description &opts, const po::positional_options_description &posit, std::vector<std::string> &args, const char * CommandLineOrigin, po::parsed_options &result) {
    po::parsed_options original = parseOptions(opts, posit, args);
    result.m_options_prefix = original.m_options_prefix;
    for (auto &option : original.options) {
        if (strcmp("response-file", option.string_key.c_str()) == 0) {
            if (option.value.empty()) return str("error ", CommandLineOrigin, ": cannot use response-file option without filename value");
            bool ok;
            const bool binary = false;
            std::string responsecontents = get_file_contents(option.value[0].c_str(), binary, ok);
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

//helper method for parseNano()
template<bool GLOBAL> void validateGlobal(ContextToParseNanoOptions *context, const char * OPTION_NAME) {
    if (!GLOBAL && context->spec.isGlobal) throw po::error(str("Error: global option 'nano-global' was specified so option --", OPTION_NAME, " cannot be specified"));
}

//helper method for parseNano()
template<bool GLOBAL, typename... Args> void throw_error_specific(int ntool, Args... args) {
    if (GLOBAL) throw po::error(str(args...));
    else        throw po::error(str(args..., " for process ", ntool));
}

//helper method for parseNano()
template<bool GLOBAL, typename T> bool nanoOptionGetIfPresent(ContextToParseNanoOptions *context, po::variables_map &current, T& option, const char * OPTION_NAME) {
    if (current.count(OPTION_NAME)) {
        validateGlobal<GLOBAL>(context, OPTION_NAME);
        option = std::move(current[OPTION_NAME].as<T>());
        return true;
    }
    return false;
}

//helper method for parseNano()
template<bool GLOBAL, typename T> bool nanoBoolOptionGetWithDefault(ContextToParseNanoOptions *context, po::variables_map &current, T& option, const char * OPTION_NAME, T Default, const char * default_name, T alternative, const char * alternative_name) {
    bool assigned = false;
    if (GLOBAL) {
        option    = Default;
        assigned  = true;
    }
    if (current.count(OPTION_NAME)) {
        assigned  = true;
        validateGlobal<GLOBAL>(context, OPTION_NAME);
        std::string mode = std::move(current[OPTION_NAME].as<std::string>());
        if      (mode.compare(default_name))     option = Default;
        else if (mode.compare(alternative_name)) option = alternative;
        else                                  throw po::error(str("Error: valid values for --", OPTION_NAME, " are either '", default_name, "' or '", alternative_name, "', but the value was: ", mode));
    }
    return assigned;
}

//helper method for parseNano()
template<bool GLOBAL, typename T> bool nanoOptionMandatory(int ntool, ContextToParseNanoOptions *context, po::variables_map &current, T& option, const char * OPTION_NAME) {
    if (current.count(OPTION_NAME)) {
        validateGlobal<GLOBAL>(context, OPTION_NAME);
        option = std::move(current[OPTION_NAME].as<T>());
        return true;
    } else {
        bool must_be_specified;
        if (GLOBAL) {
            must_be_specified = context->spec.isGlobal;
        } else {
            bool notInGlobal  = context->nanoGlobal->count(GET_GLOBALNAME_IN_LOCALCONTEXT(OPTION_NAME)) == 0;
            must_be_specified = notInGlobal;
        }
        if (must_be_specified) {
            throw_error_specific<GLOBAL>(ntool, "Error: if --nanoscribe option is specified --", OPTION_NAME, " MUST also be specified");
        }
        return false;
    }
}

//helper method for parseNano()
void validateNoLocalNanoOptions(po::variables_map &current, const char *endErrStr) {
    const bool GLOBAL = false;
    const char * names[] = {
        PREFIXNANONAME("nano-tool-begin"),
        PREFIXNANONAME("nano-tool-end"),
        PREFIXNANONAME("nano-file-begin"),
        PREFIXNANONAME("nano-file-end"),
        PREFIXNANONAME("nano-scanmode"),
        PREFIXNANONAME("nano-galvocenter"),
        PREFIXNANONAME("nano-angle"),
        PREFIXNANONAME("nano-spacing"),
        PREFIXNANONAME("nano-margin"),
        PREFIXNANONAME("nano-maxsquarelen"),
        PREFIXNANONAME("nano-origin"),
        PREFIXNANONAME("nano-gridstep")
    };
    for (int i = 0; i < sizeof(names); ++i) {
        if (current.count(names[i])) {
            throw po::error(str("Error: option --", names[i], endErrStr));
        }
    }
}

//generic method to parse Nanoscribe options both in global mode and in per-process mode. Some considerations:
//      -the global mode is parsed first, but when it is called, arguments ntool and numtools are invalid (we do not know numtools until later)
//      -except for --pp-nano-tool-*, per-process options are not allowed if the global option --nano-global is used
//      -of --nano-global is not used, global options can still be used, and they will be copied to all per-process configurations
template<bool GLOBAL> void parseNano(int ntool, int numtools, po::variables_map &current, ContextToParseNanoOptions *context) {
    int idx;
    if (GLOBAL) {
        context->spec.useSpec  = current.count("nanoscribe")!=0;
        if (!context->spec.useSpec) throw po::error(str("Error: option --", current.begin()->first, " was specified, but option '--nanoscribe FILENAME' was not specified!"));
        context->spec.filename = std::move(current["nanoscribe"].as<std::string>());
        if (context->spec.filename.empty()) {
            throw po::error("nanoscribe filename must not be empty!");
        }
        context->spec.isGlobal = current.count("nano-global") != 0;
        idx = 0;
        context->spec.nanos .resize(1);
        context->spec.splits.resize(1);
        context->spec.nanos[0]      = std::make_shared<SimpleNanoscribeConfig>();
        context->spec.generic_ntool = current.count("nano-by-tool") == 0;
        context->spec.generic_z     = current.count("nano-by-z")    == 0;
    } else {
        if (context == NULL) {
            validateNoLocalNanoOptions(current, " is invalid!");
            return;
        } else if (!context->spec.useSpec) {
            validateNoLocalNanoOptions(current, " was specified, but option '--nanoscribe FILENAME' was not specified!");
            return;
        }
        context->spec.toolChanges = context->spec.nanos[0]->toolChanges = std::make_shared<ToolChanges>(numtools);
        if (!context->spec.isGlobal) {
            context->spec.nanos .resize(numtools);
            context->spec.splits.resize(numtools);
            for (int i = 1; i < numtools; ++i) {
                context->spec.splits[i] = context->spec.splits[0];
                context->spec.nanos[i]  = std::make_shared<SimpleNanoscribeConfig>(*context->spec.nanos[0]);
            }
        }
        idx = ntool;
    }

    std::string string;
    if (current.count(PREFIXNANONAME("nano-tool-begin"))) {
        string = std::move(current[PREFIXNANONAME("nano-tool-begin")].as<std::string>());
        if (!string.empty()) string += "\n";
        (*context->spec.toolChanges)[ntool].first = std::move(string);
    }
    if (current.count(PREFIXNANONAME("nano-tool-end"))) {
        string = std::move(current[PREFIXNANONAME("nano-tool-end")].as<std::string>());
        if (!string.empty()) string += "\n";
        (*context->spec.toolChanges)[ntool].second = std::move(string);
    }

    if (nanoOptionGetIfPresent<GLOBAL, std::string>(context, current, string, PREFIXNANONAME("nano-file-begin"))) {
        if (!string.empty()) string += "\n";
        context->spec.nanos[idx]->beginScript       = std::move(string);
    }
    if (nanoOptionGetIfPresent<GLOBAL, std::string>(context, current, string, PREFIXNANONAME("nano-file-end"))) {
        if (!string.empty()) string += "\n";
        context->spec.nanos[idx]->endScript         = std::move(string);
    }
    if (nanoOptionGetIfPresent<GLOBAL, std::string>(context, current, string, PREFIXNANONAME("nano-global-file-begin"))) {
        if (!string.empty()) string += "\n";
        context->spec.nanos[idx]->beginGlobalScript = std::move(string);
    }
    if (nanoOptionGetIfPresent<GLOBAL, std::string>(context, current, string, PREFIXNANONAME("nano-global-file-end"))) {
        if (!string.empty()) string += "\n";
        context->spec.nanos[idx]->endGlobalScript   = std::move(string);
    }

    NanoscribeScanMode scanmode;
    if (nanoBoolOptionGetWithDefault<GLOBAL, NanoscribeScanMode>(context, current, scanmode,
        PREFIXNANONAME("nano-scanmode"), GalvoScanMode, "galvo", PiezoScanMode, "piezo")) {
        context->spec.nanos[idx]->scanmode = scanmode;
    }

    GalvoPositionMode galvomode;
    if (nanoBoolOptionGetWithDefault<GLOBAL, GalvoPositionMode>(context, current, galvomode,
        PREFIXNANONAME("nano-galvocenter"), GalvoAlwaysCenter, "always", GalvoMinimizeMovements, "minimize")) {
        context->spec.nanos[idx]->galvomode = galvomode;
    }

    if (GLOBAL) context->spec.splits[idx].wallAngle = 90.0;
    if (current.count(PREFIXNANONAME("nano-angle"))) {
        validateGlobal<GLOBAL>(context, PREFIXNANONAME("nano-angle"));
        context->spec.splits[idx].wallAngle = current[PREFIXNANONAME("nano-angle")].as<double>();
    }

    bool assigned; double val;

    assigned = nanoOptionMandatory<GLOBAL, double>(ntool, context, current, val, PREFIXNANONAME("nano-spacing"));
    if (assigned) {
        context->spec.splits[idx].displacement.X =
        context->spec.splits[idx].displacement.Y = (clp::cInt)(val * context->factors.nanoscribe_to_internal);
    }

    assigned = nanoOptionMandatory<GLOBAL, double>(ntool, context, current, val, PREFIXNANONAME("nano-margin"));
    if (assigned) {
        context->spec.splits[idx].margin = (clp::cInt)(val * context->factors.nanoscribe_to_internal);
    }

    assigned = nanoOptionMandatory<GLOBAL, double>(ntool, context, current, val, PREFIXNANONAME("nano-maxsquarelen"));
    if (assigned) {
        context->spec.nanos[idx]->maxSquareLen = val * context->factors.nanoscribe_to_internal;

        if ((GLOBAL && context->spec.isGlobal) || (!GLOBAL && !context->spec.isGlobal)) {
            val = (double)(context->spec.splits[idx].displacement.X + 2 * context->spec.splits[idx].margin);
            if (context->spec.nanos[idx]->maxSquareLen < val) {
                throw_error_specific<GLOBAL>(ntool, "Error: ", PREFIXNANONAME("nano-maxsquarelen"), " MUST be greater than or equal to nano-spacing+2*nano-margin, but it was lower (", context->spec.nanos[idx]->maxSquareLen, '<', val, ')');
            }
        }
    }

    std::vector<double> origin;
    if (GLOBAL) context->spec.splits[idx].useOrigin = false;
    if (nanoOptionGetIfPresent<GLOBAL, std::vector<double>>(context, current, origin, PREFIXNANONAME("nano-origin"))) {
        if (origin.size() != 2) {
            throw_error_specific<GLOBAL>(ntool, "Error: ", PREFIXNANONAME("nano-origin"), " MUST have two values (X and Y), but it had ", origin.size());
        }
        context->spec.splits[idx].useOrigin = true;
        context->spec.splits[idx].origin.X  = (clp::cInt)(origin[0] * context->factors.nanoscribe_to_internal);
        context->spec.splits[idx].origin.Y  = (clp::cInt)(origin[1] * context->factors.nanoscribe_to_internal);
    }

    if (GLOBAL) context->spec.nanos[idx]->snapToGrid = false;
    if (nanoOptionGetIfPresent<GLOBAL, double>(context, current, val, PREFIXNANONAME("nano-gridstep"))) {
        context->spec.nanos[idx]->gridStep   = (clp::cInt)(val * context->factors.nanoscribe_to_internal);
        context->spec.nanos[idx]->snapToGrid = context->spec.nanos[idx]->gridStep != 0;
    }

    if (GLOBAL) {
        context->spec.splits[idx].zmin = 0.0;
        context->spec.nanos[idx]->nanoscribeNumberFormatting = "%.4f";
    }

    if (GLOBAL) {
        if (context->spec.isGlobal) {
            context->spec.nanos[idx]->init(context->factors);
        }
    } else {
        if (!context->spec.isGlobal) {
            context->spec.nanos[idx]->init(context->factors);
        }
    }
}

void parseNanoGlobal(ContextToParseNanoOptions *nanoContext) {
    parseNano<true>(0, 0, *nanoContext->nanoGlobal, nanoContext);
}

std::string parseGlobal(GlobalSpec &spec, po::parsed_options &optionList, MetricFactors &factors) {
    bool doscale = factors.doparamscale;
    double scale = factors.doparamscale ? factors.param_to_internal : 0.0;
    po::variables_map vm;
    try {
        po::store(optionList, vm);
    } catch (std::exception &e) {
        return std::string(e.what());
    }
    spec.alsoContours              = vm.count("save-contours")       != 0;
    spec.correct                   = vm.count("correct-input")       != 0;
    spec.applyMotionPlanner        = vm.count("motion-planner")      != 0;
    spec.avoidVerticalOverwriting  = vm.count("vertical-correction") != 0;

    spec.addsub.addsubWorkflowMode = vm.count("addsub") != 0;
    if (spec.addsub.addsubWorkflowMode) {
        spec.addsub.fattening.eraseHighResNegDetails = vm.count("neg-closing") != 0;
        spec.addsub.fattening.useGradualFattening    = vm.count("overwrite-gradual") != 0;
        if (spec.addsub.fattening.eraseHighResNegDetails) {
            spec.addsub.fattening.eraseHighResNegDetails_radius = (clp::cInt)getScaled(vm["neg-closing"].as<double>(), scale, doscale);
            spec.addsub.fattening.eraseHighResNegDetails = spec.addsub.fattening.eraseHighResNegDetails_radius != 0.0;
        }
        if (spec.addsub.fattening.useGradualFattening) {
            auto &vals = vm["overwrite-gradual"].as<std::vector<double>>();
            if ((vals.size() % 2) != 0) {
                return str("overwrite-gradual must have an even number of values, but it has ", vals.size());
            }
            spec.addsub.fattening.gradual.reserve(vals.size() / 2);
            for (auto val = vals.begin(); val != vals.end(); ++val) {
                double rad = *(val++);
                double inf = *val;
                if (rad < 0) return str("In overwrite-gradual, the ", spec.addsub.fattening.gradual.size(), "-th pair has a negative radius factor: ", rad);
                if (inf < 0) return str("In overwrite-gradual, the ", spec.addsub.fattening.gradual.size(), "-th pair has a negative inflation factor: ", inf);
                //maybe issue warnings if rad+inf<1? If so, how to issue them?
                spec.addsub.fattening.gradual.push_back(FatteningSpec::GradualStep(rad, inf));
            }
        }

    }

    if (vm.count("subtractive-box-mode")) {
        auto &vals = vm["subtractive-box-mode"].as<std::vector<int>>();
        if (vals.size() == 0) {
            return std::string("subtractive-box-mode was specified without values!");
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
        spec.use_z_base = vm.count("slicing-zbase") != 0;
        if (spec.use_z_base) {
            spec.z_base = vm["slicing-zbase"].as<double>();
        }
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
        auto &vals     = vm["slicing-manual"].as<std::vector<double>>();
        if ((vals.size() % 2) != 0) {
            return str("slicing-manual must have an even number of values, but it has ", vals.size());
        }
        spec.schedSpec.reserve(vals.size() / 2);
        for (auto val = vals.begin(); val != vals.end(); ++val) {
            double ntoolv = *val;
            int ntool = (int)ntoolv;
            double z  = *(++val);
            if (ntool != ntoolv) return str("Invalid slicing-manual value in position ", val-1-vals.begin(), ": for Z ", z, "the tool is not an integer: ", *val);
            spec.schedSpec.push_back(GlobalSpec::ZNTool(z, ntool));
        }
        schedSet = true;
    };
    if (!schedSet) return std::string("Exactly one of these options must be set: slicing-uniform, slicing-scheduler, slicing-manual");
    spec.useScheduler = spec.schedMode != UniformScheduling;
    if (vm.count("z-epsilon")) {
        spec.z_epsilon = vm["z-epsilon"].as<double>() * factors.input_to_internal;
    };

    const std::string &direction = vm["slicing-direction"].as<std::string>();
    if      (direction.compare("up")   == 0) spec.sliceUpwards = true;
    else if (direction.compare("down") == 0) spec.sliceUpwards = false;
    else                                     return str("value for option slicing-direction must be either 'up' or 'down', but it was '", direction, "'");

    spec.fb.feedback = vm.count("feedback") != 0;
    if (spec.fb.feedback) {
        const std::vector<std::string> &vals = vm["feedback"].as<std::vector<std::string>>();
        spec.fb.feedbackMesh = strcmp(vals[0].c_str(), "mesh") == 0;
        if ((!spec.fb.feedbackMesh) && (strcmp(vals[0].c_str(), "paths") != 0)) {
            return str("Error: If feedback option is specified, the first value is the feedback mode, either 'mesh' or 'paths', but it was <", vals[0], ">\n");
        }
        if (vals.size() == 1) { return std::string("Error: If feedback is specified, the second value must be the feedback file name, but it was not present"); }
        spec.fb.feedbackFile = std::move(vals[1]);
        if (!fileExists(spec.fb.feedbackFile.c_str())) {
            return str("Error: feedback file <", spec.fb.feedbackFile, "> does not exist!");
        }
        if (!spec.useScheduler) {
            const char *schedmode;
            switch (spec.schedMode) {
            case SimpleScheduler:   schedmode = "sched"; break;
            case UniformScheduling: schedmode = "uniform"; break;
            case ManualScheduling:  schedmode = "manual"; break;
            default:                schedmode = "unknown";
            }
            return str("Error: feedback file was specified (", spec.fb.feedbackFile, "), but the scheduling mode '", schedmode, "' does not allow feedback!!!!");
        }
    }

    return std::string();
}

//this method CANNOT be called until GlobalSpec::parseOptions has been called
std::string parsePerProcess(MultiSpec &spec, po::parsed_options &optionList, MetricFactors &factors, ContextToParseNanoOptions *nanoContext) {
    bool doscale = factors.doparamscale;
    double scale = factors.doparamscale ? factors.param_to_internal : 0.0;
    typedef std::pair<int, po::parsed_options> OptionsByTool;
    std::vector<OptionsByTool> optionsByTool;
    optionsByTool.reserve(3); //reasonable number to reserve
    std::vector<int> processIds;
    int maxProcess = 0;
    int currentProcess;
    //separate global options, and sort per-process options according to the previous --process option
    for (auto & option : optionList.options) {
        if (option.string_key.compare("process") == 0) {
            if (option.value.empty()) return std::string("process option must have a value!");
            char *endptr; int param = (int)strtol(option.value[0].c_str(), &endptr, 10);
            if (param<0) return std::string("process option cannot have negative value: ", param);
            if ((*endptr) != 0) return str("process option value must be a non-negative integer: ", option.value[0]);
            bool found = false;
            for (auto id = processIds.begin(); id != processIds.end();  ++id) {
                if (*id == param) {
                    currentProcess = (int)(id - processIds.begin());
                    found = true;
                    break;
                }
            }
            if (found) continue;
            processIds.push_back(param);
            optionsByTool.push_back(OptionsByTool(param, po::parsed_options(perProcessOptions(), optionList.m_options_prefix)));
            currentProcess = (int)optionsByTool.size() - 1;
            if (maxProcess < processIds.back()) maxProcess = processIds.back();
            continue;
        }
        if (optionsByTool.empty()) {
            return str("option ", option.string_key, " cannot be specified before option --process");
        }
        optionsByTool[currentProcess].second.options.push_back(std::move(option));
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

    for (auto & optionsListByTool : optionsByTool) {
        po::variables_map vm;
        try {
            po::store(optionsListByTool.second, vm);
        } catch (std::exception &e) {
            return str("for process ", optionsListByTool.first, ": ", e.what());
        }

        int k = optionsListByTool.first;

        spec.pp[k].radius             = (clp::cInt)getScaled(vm["radx"]               .as<double>(), scale, doscale);
        spec.pp[k].gridstep           = (clp::cInt)getScaled(vm["gridstep"]           .as<double>(), scale, doscale);
        spec.pp[k].radiusRemoveCommon = (clp::cInt)getScaled(vm["radius-removecommon"].as<double>(), scale, doscale);
        if (vm.count("tolerances")) {
            const std::vector<double> & val = vm["tolerances"].as<std::vector<double>>();
            spec.pp[k].arctolR = (clp::cInt)getScaled(val[0], scale, doscale);
            spec.pp[k].arctolG = val.size() == 1 ? spec.pp[k].arctolR : (clp::cInt)getScaled(val[1], scale, doscale);
        } else {
            spec.pp[k].arctolR = (spec.pp[k].radius / 100);
            spec.pp[k].arctolG = (spec.pp[k].radius / 10);
        }
        spec.pp[k].burrLength = (clp::cInt) (vm.count("smoothing") ? getScaled(vm["smoothing"].as<double>(), scale, doscale) : spec.pp[k].arctolR);

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
                spec.pp[k].profile = std::make_shared<EllipticalProfile>((double)spec.pp[k].radius, ZRadius, 2 * ZSemiHeight);
            } else {
                spec.pp[k].profile = std::make_shared<ConstantProfile>  ((double)spec.pp[k].radius, ZRadius, 2 * ZSemiHeight);
            }
        }
        spec.pp[k].applysnap            = vm.count("snap")      != 0;
        spec.pp[k].snapSmallSafeStep    = vm.count("safestep")  != 0;
        spec.pp[k].addInternalClearance = vm.count("clearance") != 0;
        spec.pp[k].doPreprocessing      = vm.count("no-preprocessing") == 0;
        spec.pp[k].computeToolpaths     = vm.count("no-toolpaths") == 0;
        
        if (!spec.pp[k].doPreprocessing) {
            spec.pp[k].noPreprocessingOffset = getScaled(vm["no-preprocessing"].as<double>(), scale, doscale);
        }

        if (vm.count("medialaxis-radius")) {
            spec.pp[k].medialAxisFactors = std::move(vm["medialaxis-radius"].as<std::vector<double>>());
        }

        bool useinfill = vm.count("infill") != 0;
        if (useinfill) {
            const std::string & val = vm["infill"].as<std::string>();
            if      (val.compare("concentric")  == 0) spec.pp[k].infillingMode = InfillingConcentric;
            else if (val.compare("linesh")      == 0) spec.pp[k].infillingMode = InfillingRectilinearH;
            else if (val.compare("linesv")      == 0) spec.pp[k].infillingMode = InfillingRectilinearV;
            else if (val.compare("justcontour") == 0) spec.pp[k].infillingMode = InfillingJustContours;
            else                                      return str("For process ", k, ": invalid infill mode: ", val);
            spec.pp[k].infillingRecursive = vm.count("infill-recursive") != 0;
            spec.pp[k].infillingWhole     = vm.count("infill-byregion")  == 0;
            if (vm.count("infill-medialaxis-radius")) {
                spec.pp[k].medialAxisFactorsForInfillings = std::move(vm["infill-medialaxis-radius"].as<std::vector<double>>());
            }
        } else {
            spec.pp[k].infillingMode = InfillingNone;
        }

        parseNano<false>(k, (int)spec.numspecs, vm, nanoContext);
    }
    return spec.populateParameters();
}

std::string parseAll(MultiSpec &spec, po::parsed_options &globalOptionList, po::parsed_options &perProcOptionList, MetricFactors &factors, ContextToParseNanoOptions *nanoContext) {
    std::string err = parseGlobal(spec.global, globalOptionList, factors);
    if (!err.empty()) return err;
    return parsePerProcess(spec, perProcOptionList, factors, nanoContext);
}

std::string parseAll(MultiSpec &spec, const char *CommandLineOrigin, std::vector<std::string> &args, MetricFactors &factors, ContextToParseNanoOptions *nanoContext) {
    try {
        std::vector<const po::options_description*> optss = { globalOptions(), perProcessOptions() };
        po::positional_options_description emptypos;
        auto sorted = sortOptions(optss, emptypos, 0, CommandLineOrigin, args);
        return parseAll(spec, sorted[0], sorted[1], factors, nanoContext);
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
