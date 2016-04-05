//this is a simple command line application that organizes the execution of the multislicer

#include "parsing.hpp"
#include "slicermanager.hpp"
#include "3d.hpp"
#include "pathwriter_dxf.hpp"
#include "pathwriter_nanoscribe.hpp"
#include <iostream>

//if macro STANDALONE_USEPYTHON is defined, SHOWCONTOUR support is baked in
#ifdef STANDALONE_USEPYTHON
#    include "showcontours.hpp"
#    include "sliceviewer.hpp"
#endif

//this class handles the *save-in-grid options, which must be coordinated acroos global and per-process options
class ParserStandaloneLocalAndGlobal : public ParserAllLocalAndGlobal {
public:
    PathSplitterConfigs saveInGridConf;
    ParserStandaloneLocalAndGlobal(MetricFactors &f, MultiSpec &s, std::shared_ptr<po::options_description> g, std::shared_ptr<po::options_description> l, NanoscribeSpec *n = NULL) : ParserAllLocalAndGlobal(f, s, std::move(g), std::move(l), n) {}
    virtual void globalCallback();
    virtual void perProcessCallback(int k, po::variables_map &processOptions);
    virtual void finishCallback();
    template<bool GLOBAL> static void addOptions(po::options_description &opts);
protected:
    void parseSaveInGridOption(po::variables_map &vm, const char *opt_name, int idx, PathSplitterConfig &config);
    bool saveGlobal;
    std::vector<bool> saveLocals;
};

template<bool GLOBAL> void ParserStandaloneLocalAndGlobal::addOptions(po::options_description &opts) {
    if (GLOBAL) {
        opts.add_options()
            ("save-in-grid",
            po::value<std::vector<double>>()->multitoken(),
            "If this option is specified, --save, --dxf-toopaths and --dxf-contours will generate additional output: the output toolpaths and contours will also be partitioned according to a grid, generating an additonal file for each partition in the grid. The naming convention for these files is to append to the file name the suffix .X.Y, where X and Y are the grid coordinates of each partition element. Essentially, this is a tool to use a similar partitioning scheme as the one used for Nanoscribe output. This is useful to solve the surface alignment problem: often, the surface of the material or the base is not perfectly parallel to the XY plane of movement, so large shapes at very high resolution will fail to be properly printed or machined. An easy workaround for this problem is to partition the shape, so that the surface can be approximately aligned for each element of the partition (Nanoscribe machines take this approach). This option takes a list of parameters: DISPLACEMENT MARGIN [ORIGIN_X ORIGIN_Y]. The shape will be divided in a grid of squares of size DISPLACEMENT+MARGIN, overlapping by MARGIN on each side. If ORIGIN_X and ORIGIN_Y are provided, they represent the origin of coordinates of the grid. Otherwise, the grid will be generated to fit the input mesh file, with rectangular partition elements of size possibly smaler than DISPLACEMENT+MARGIN. All parameters are in mesh file units (usually millimeters). This option will use the same grid for all processes. If you want to use different grid parameters for each process, please see the help of the per-process option --pp--save-in-grid.")
            ;
    } else {
        opts.add_options()
            ("pp-save-in-grid",
            po::value<std::vector<double>>()->multitoken(),
            "This is a per-process version of the option --save-in-grid (please see the help for --save-in-grid for details on why use this option, and a description of the parameters it takes). If --save-in-grid is used, all processes will be partitioned with the same grid. If --pp--save-in-grid is used, one --pp--save-in-grid must be specified for each process, so that each process will have a different grid. Usage of --save-in-grid and pp-save-in-grid are mutually exclusive.")
            ;

    }
}

void ParserStandaloneLocalAndGlobal::globalCallback() {
    ParserAllLocalAndGlobal::globalCallback();
    saveInGridConf.clear();
    saveLocals.clear();
    saveLocals.resize(spec.numspecs, false);
    saveGlobal = globalOptions.count("save-in-grid") != 0;
    if (saveGlobal) {
        saveInGridConf.resize(1);
        parseSaveInGridOption(globalOptions, "save-in-grid", -1, saveInGridConf[0]);
    }
}

void ParserStandaloneLocalAndGlobal::perProcessCallback(int k, po::variables_map &processOptions) {
    ParserAllLocalAndGlobal::perProcessCallback(k, processOptions);
    saveLocals[k] = processOptions.count("pp-save-in-grid") != 0;
    if (saveLocals[k]) {
        if (saveGlobal) throw po::error(str("error: if option --save-in-grid is used, --pp--save-in-grid cannot be used for any process, but it was specified for process ", k, "!"));
        if (saveInGridConf.empty()) {
            saveInGridConf.resize(spec.numspecs);
        }
        parseSaveInGridOption(processOptions, "pp-save-in-grid", k, saveInGridConf[k]);
    }
}

void ParserStandaloneLocalAndGlobal::finishCallback() {
    ParserAllLocalAndGlobal::finishCallback();
    if (!saveGlobal) {
        bool first = saveLocals[0];
        for (auto local = saveLocals.begin() + 1; local != saveLocals.end(); ++local) {
            if (*local != first) {
                int n = (int)(local - saveLocals.begin());
                throw po::error(str("If --pp--save-in-grid is specified for one process, it must be specified FOR ALL processes! It was specifed for process ", first ? 0 : n, " but not for process ", first ? n : 0, "!"));
            }
        }
    }
}

void ParserStandaloneLocalAndGlobal::parseSaveInGridOption(po::variables_map &vm, const char *opt_name, int idx, PathSplitterConfig &conf) {
    const std::vector<double> &vals = vm[opt_name].as<std::vector<double>>();
    int s = (int)vals.size();
    if (!((s == 2) || (s == 4)))  {
        std::string opt = std::string(opt_name);
        if (idx >= 0) {
            opt += str(" (for process ", idx, ")");
        }
        throw po::error(str("option --", opt, " requires either 2 or 4 arguments, but it has ", s));
    }
    double displacement = vals[0];
    double margin       = vals[1];
    conf.wallAngle      = 90; //no need to set conf[0].zmin
    conf.displacement.X =
    conf.displacement.Y = (clp::cInt)(vals[0] * factors.input_to_internal);
    conf.margin         = (clp::cInt)(vals[1] * factors.input_to_internal);
    conf.useOrigin      = s==4;
    if (conf.useOrigin) {
        conf.origin.X   = (clp::cInt)(vals[2] * factors.input_to_internal);
        conf.origin.Y   = (clp::cInt)(vals[3] * factors.input_to_internal);
    }
}


typedef struct MainSpec {
    std::vector<const char *>                             opts_toparse_names;
    std::vector<std::shared_ptr<po::options_description>> opts_toshow;
    std::vector<std::shared_ptr<po::options_description>> opts_toparse;
    std::vector<const po::options_description*>           opts_toparse_naked;
    std::vector<po::parsed_options>                       optsBySystem;
    int mainOptsIdx;
    int dxfOptsIdx;
    int globalOptsIdx;
    int perProcOptsIdx;
    MainSpec();
    void slurpAllOptions(int argc, const char ** argv);
    void usage();
    inline po::variables_map getMap(int idx) {
        po::variables_map map;
        try {
            po::store(optsBySystem[idx], map);
        } catch (std::exception &e) {
            throw po::error(str("Error reading ", opts_toparse_names[idx], ": ", e.what()));
        }
        return map;
    }
    inline bool nonEmptyOpts(int idx) {
        return !optsBySystem[idx].options.empty();
    }
} MainSpec;

MainSpec::MainSpec() {
    opts_toshow       .reserve(6);
    opts_toparse      .reserve(4);
    opts_toparse_names.reserve(4);
    opts_toparse_naked.reserve(4);
    optsBySystem      .reserve(4);

    mainOptsIdx = (int)opts_toparse.size();
    opts_toparse_names.push_back("Main options");
    opts_toparse.emplace_back(std::make_shared<po::options_description>(opts_toparse_names.back()));
    opts_toparse.back()->add_options()
        ("help",
            "produce help message")
        ("config",
            po::value<std::string>()->default_value("config.txt"),
            "configuration input file (if no file is provided, it is assumed to be config.txt)")
        ("load",
            po::value<std::string>()->value_name("filename"),
            "input mesh file")
        ("save",
            po::value<std::string>()->value_name("filename"),
            "output file in *.paths format")
        ("save-format",
            po::value<std::string>()->default_value("integer"),
            "Format of coordinates in the save file, either 'integer' or 'double'. The default is 'integer'")
        ("show",
            po::value<std::vector<std::string>>()->multitoken(),
            "show result options using a python script. The first value can be either '2d' or '3d' (the script will use matplotlib or mayavi, respecivey). The second value, if present, should be a python expression for specifying visual appearance of displayed elements for the python script (must be tailored to the show mode (2d or 3d)")
        ("dry-run",
            "if this option is specified, the system only shows information about the slices. First, it displays the Z values of the slices to be received from the input mesh file (raw slices). This is useful for crafting feedback pathsfiles to be used with the --feedback option. Then, if --slicing-scheduler was specified, it displays the ordered sequence of slices to be computed, exactly in the same format as the arguments of --slicing-manual (pairs NTool and Z), so this can be used as input for this option. Finally, the application terminates without doing anything else.")
        ;
    addResponseFileOption(*opts_toparse.back());

    dxfOptsIdx = (int)opts_toparse.size();
    opts_toparse_names.push_back("DXF options");
    opts_toparse.emplace_back(std::make_shared<po::options_description>(opts_toparse_names.back()));
    opts_toparse.back()->add_options()
        ("dxf-toolpaths",
            po::value<std::string>()->value_name("filename"),
            "Output toolpaths in a *.dxf file")
        ("dxf-contours",
            po::value<std::string>()->value_name("filename"),
            "Output contours in a *.dxf file (if both this and --dxf-toolpaths are specified, the file names MUST be different)")
        ("dxf-format",
            po::value<std::string>()->default_value("binary"),
            "Format of the output DXF files: either 'binary' or 'ascii'. The default is 'binary'")
        ("dxf-by-z",
            "If this option is specified, a different DXF output file is generated for each Z value")
        ("dxf-by-tool",
            "If this option is specified, a different DXF output file is generated for each process. Note: if --dxf-by-z is also specified, a different file is generated for each combination of Z and process")
        ;

    globalOptsIdx  = (int)opts_toparse.size();
    opts_toparse_names.push_back("Global options");
    opts_toparse.emplace_back(std::make_shared<po::options_description>(std::move(    globalOptionsGenerator(YesAddNano, NotAddResponseFile))));
    ParserStandaloneLocalAndGlobal::addOptions<true >(*opts_toparse.back());

    perProcOptsIdx = (int)opts_toparse.size();
    opts_toparse_names.push_back("Per-process options");
    opts_toparse.emplace_back(std::make_shared<po::options_description>(std::move(perProcessOptionsGenerator(YesAddNano))));
    ParserStandaloneLocalAndGlobal::addOptions<false>(*opts_toparse.back());

    for (auto &opt : opts_toparse) {
        opts_toparse_naked.push_back(opt.get());
    }

    opts_toshow.push_back(opts_toparse[mainOptsIdx]);
    opts_toshow.push_back(opts_toparse[dxfOptsIdx]);
    opts_toshow.emplace_back(std::make_shared<po::options_description>(std::move(        globalOptionsGenerator(NotAddNano, NotAddResponseFile))));
    ParserStandaloneLocalAndGlobal::addOptions<true >(*opts_toshow.back());
    opts_toshow.emplace_back(std::make_shared<po::options_description>(std::move(    perProcessOptionsGenerator(NotAddNano))));
    ParserStandaloneLocalAndGlobal::addOptions<false>(*opts_toshow.back());
    opts_toshow.emplace_back(std::make_shared<po::options_description>(std::move(    nanoGlobalOptionsGenerator())));
    opts_toshow.emplace_back(std::make_shared<po::options_description>(std::move(nanoPerProcessOptionsGenerator())));
}

void MainSpec::slurpAllOptions(int argc, const char ** argv) {
    auto args    = getArgs(argc, argv);
    optsBySystem = sortOptions(opts_toparse_naked, po::positional_options_description(), mainOptsIdx, NULL, args);
}

void MainSpec::usage() {
    std::cout << "Command line interface to the multislicing engine.\n  If there is no ambiguity, options can be specified as prefixes of their full names.\n";
    for (auto opts : opts_toshow) {
        std::cout << *opts << "\n";
    }
}

int Main(int argc, const char** argv) {
    std::string meshfullpath;

    bool show, use2d, useviewparams;
    std::string viewparams;

    bool save;
    bool saveDXF  = false;
    bool saveNano = false;
    int64 saveFormat;

    DXFWMode dxfmode;
    bool dxf_generic_by_ntool, dxf_generic_by_z;
    std::string dxf_filename_toolpaths, dxf_filename_contours;

    double epsilon_meshunits;

    PathSplitterConfigs saveInGridConf;

    std::string singleoutputfilename;

    bool dryrun;
    
    std::shared_ptr<Configuration> config = std::make_shared<Configuration>();
    std::shared_ptr<MultiSpec>  multispec = std::make_shared<MultiSpec>(config);
    bool doscale = true;
    MetricFactors factors;

    std::vector<std::shared_ptr<PathWriter>> pathwriters_arefiles;    //everything which has to be closed
    std::vector<std::shared_ptr<PathWriter>> pathwriters_raw;         //everything receiving raw slices
    std::vector<std::shared_ptr<PathWriter>> pathwriters_contour;     //everything receiving contours
    std::vector<std::shared_ptr<PathWriter>> pathwriters_toolpath;    //everything receiving toolpaths
    std::shared_ptr<PathsFileWriter> pathwriter_viewer;               //PathWriter in native format for the viewer

    NanoscribeSpec nanoSpec;

    try {
        MainSpec mainSpec;
        if (argc == 1) {
            mainSpec.usage();
            return 1;
        }
        mainSpec.slurpAllOptions(argc, argv);

        po::variables_map mainOpts = mainSpec.getMap(mainSpec.mainOptsIdx);

        if (mainOpts.count("help")) {
            mainSpec.usage();
            return 1;
        }

        dryrun = mainOpts.count("dry-run") != 0;
        save   = mainOpts.count("save") != 0;

        std::string meshfilename;
        if (mainOpts.count("load")) {
            meshfilename = std::move(mainOpts["load"].as<std::string>());
        } else if (!dryrun) {
            fprintf(stderr, "Error: load parameter has not been specified!");
        }

        std::string configfilename = std::move(mainOpts["config"].as<std::string>());

        if (!fileExists(configfilename.c_str())) { fprintf(stderr, "Could not open config file %s!!!!", configfilename.c_str()); return -1; }

        config->load(configfilename.c_str());

        if (config->has_err) { fprintf(stderr, config->err.c_str()); return -1; }

        factors.init(*config, doscale);
        if (!factors.err.empty()) { fprintf(stderr, factors.err.c_str()); return -1; }

        {
            ParserStandaloneLocalAndGlobal parser(factors, *multispec, mainSpec.opts_toparse[mainSpec.globalOptsIdx], mainSpec.opts_toparse[mainSpec.perProcOptsIdx], &nanoSpec);
            parser.setParsedOptions(mainSpec.optsBySystem[mainSpec.globalOptsIdx], mainSpec.optsBySystem[mainSpec.perProcOptsIdx]);
            saveInGridConf = std::move(parser.saveInGridConf);
        }
        saveNano = !dryrun && nanoSpec.useSpec;

        epsilon_meshunits = multispec->global.z_epsilon*factors.internal_to_input;

        if (!fileExists(meshfilename.c_str())) { fprintf(stderr, "Could not open input mesh file %s!!!!", meshfilename.c_str()); return -1; }

        //this is necessary because the slicer may have a different working directory
        char *meshfullpath_c = fullPath(meshfilename.c_str());
        if (meshfullpath_c == NULL) {
            fprintf(stderr, "Error trying to resolve canonical path to the input mesh file: %s", meshfilename.c_str());
            return -1;
        }
        meshfullpath = meshfullpath_c;
        free(meshfullpath_c);

        show = mainOpts.count("show") != 0;
        if (show) {
            const std::vector<std::string> &vals = mainOpts["show"].as<std::vector<std::string>>();
            use2d = vals[0].compare("2d") == 0;
            useviewparams = vals.size() > 1;
            if (useviewparams) {
                viewparams = std::move(vals[1]);
            }
        }

        saveFormat = PATHFORMAT_DOUBLE;
        if (save) {
            singleoutputfilename = std::move(mainOpts["save"].as<std::string>());
            std::string savef = std::move(mainOpts["save-format"].as<std::string>());
            char t = tolower(savef[0]);
            if (t == 'i') {
                saveFormat = PATHFORMAT_INT64;
            } else if (t == 'f') {
                saveFormat = PATHFORMAT_DOUBLE;
            } else {
                fprintf(stderr, "save format parameter must start by either 'i' (integer) or 'f' (float): <%s>\n", savef.c_str());
                return -1;
            }
        }

        if (!dryrun && mainSpec.nonEmptyOpts(mainSpec.dxfOptsIdx)) {
            po::variables_map dxfOpts = mainSpec.getMap(mainSpec.dxfOptsIdx);
            std::string dxfm = std::move(dxfOpts["dxf-format"].as<std::string>());
            char t = tolower(dxfm[0]);
            DXFWMode dxfmode;
            if (t == 'b') {
                dxfmode = DXFBinary;
            } else if (t == 'a') {
                dxfmode = DXFAscii;
            } else {
                fprintf(stderr, "DXF format parameter must start by either 'b' (binary) or 'a' (ascii): <%s>\n", dxfm.c_str());
                return -1;
            }
            dxf_generic_by_ntool = dxfOpts.count("dxf-by-tool") == 0;
            dxf_generic_by_z     = dxfOpts.count("dxf-by-z")    == 0;
            if (dxfOpts.count("dxf-toolpaths")) dxf_filename_toolpaths = std::move(dxfOpts["dxf-toolpaths"].as<std::string>());
            if (dxfOpts.count("dxf-contours"))  dxf_filename_contours  = std::move(dxfOpts["dxf-contours" ].as<std::string>());

            const bool generic_type = true;

            auto createDXFWriter = [dxfmode, epsilon_meshunits, generic_type, dxf_generic_by_ntool, dxf_generic_by_z](std::string &fname) {
                std::shared_ptr<PathWriter> w;
                if (dxfmode == DXFAscii) {
                    w = std::make_shared<DXFAsciiPathWriter >(fname, epsilon_meshunits, generic_type, dxf_generic_by_ntool, dxf_generic_by_z);
                } else {
                    w = std::make_shared<DXFBinaryPathWriter>(fname, epsilon_meshunits, generic_type, dxf_generic_by_ntool, dxf_generic_by_z);
                }
                return w;
            };

            if (!dxf_filename_toolpaths.empty()) {
                std::shared_ptr<PathWriter> w = createDXFWriter(dxf_filename_toolpaths);
                pathwriters_arefiles.push_back(w);
                pathwriters_toolpath.push_back(w);
                saveDXF = true;
            }

            if (!dxf_filename_contours.empty()) {
                std::shared_ptr<PathWriter> w = createDXFWriter(dxf_filename_contours);
                pathwriters_arefiles.push_back(w);
                pathwriters_contour .push_back(w);
                saveDXF = true;
            }
        }

    } catch (std::exception &e) {
        fprintf(stderr, e.what()); return -1;
    }

    if (dryrun) {
        save = show = false;
    } else {
        if (!save && !show && !saveDXF && !saveNano) {
            fprintf(stderr, "ERROR: computed contours would be neither saved nor shown!!!!!\n");
            return -1;
        }
    }

    std::shared_ptr<SlicerManager> slicer = getSlicerManager(*config, factors, SlicerManagerExternal);
    //SlicerManager *slicer = getSlicerManager(SlicerManagerNative);
#ifdef STANDALONE_USEPYTHON
    std::shared_ptr<SlicesViewer> slicesViewer;
    if (show) {
        slicesViewer = std::make_shared<SlicesViewer>(*config, "view slices", use2d, viewparams.c_str());
        std::string err = slicesViewer->start();
        if (!err.empty()) {
            fprintf(stderr, "Error while trying to launch SlicerViewer script: %s\n", err.c_str());
            return -1;
        }
    }
#endif

    if (!slicer->start(meshfullpath.c_str())) {
        std::string err = slicer->getErrorMessage();
        fprintf(stderr, "Error while trying to start the slicer manager: %s!!!\n", err.c_str());
        return -1;
    }

    double minx, maxx, miny, maxy, minz, maxz;
    slicer->getLimits(&minx, &maxx, &miny, &maxy, &minz, &maxz);

    double slicer_to_input = 1 / factors.input_to_slicer;
    if (std::fabs(slicer->getScalingFactor() - slicer_to_input) > (slicer_to_input*1e-3)) {
        fprintf(stderr, "Error while trying to start the slicer manager: the scalingFactor from the slicer is %f while the factor from the configuration is different: %f!!!\n", slicer->getScalingFactor(), slicer_to_input);
        return -1;
    }

    clp::IntPoint bbmn, bbmx;
    bbmn.X = (clp::cInt) (minx * factors.input_to_internal);
    bbmn.Y = (clp::cInt) (miny * factors.input_to_internal);
    bbmx.X = (clp::cInt) (maxx * factors.input_to_internal);
    bbmx.Y = (clp::cInt) (maxx * factors.input_to_internal);

    if (nanoSpec.useSpec) {
        //we need to give a bounding box to the Splitter. The easiest (if not most correct) thing to do is to use the bounding box of the mesh file
        for (auto & split : nanoSpec.splits) {
            split.min = bbmn;
            split.max = bbmx;
        }
        std::shared_ptr<NanoscribeSplittingPathWriter> pathsplitter = std::make_shared<NanoscribeSplittingPathWriter>(*multispec, std::move(nanoSpec.nanos), std::move(nanoSpec.splits), std::move(nanoSpec.filename), nanoSpec.generic_ntool, nanoSpec.generic_z);
        pathwriters_arefiles.push_back(pathsplitter);
        pathwriters_toolpath.push_back(pathsplitter);
    }

    bool alsoContours = multispec->global.alsoContours;
    clp::Paths rawslice, dummy;
    int64 numoutputs, numsteps;
    int numtools = (int)multispec->numspecs;
    {
        std::shared_ptr<FileHeader> header;
        if (save || show) header = std::make_shared<FileHeader>(*multispec, factors);
#ifdef STANDALONE_USEPYTHON
        if (show) {
            pathwriter_viewer = std::make_shared<PathsFileWriter>("sliceViewerStream", slicesViewer->pipeIN, header, PATHFORMAT_INT64);
            pathwriters_toolpath   .push_back(pathwriter_viewer);
            if (alsoContours) {
                pathwriters_raw    .push_back(pathwriter_viewer);
                pathwriters_contour.push_back(pathwriter_viewer);
            }
        }
#endif
        if (save) {
            pathwriters_arefiles   .push_back(std::make_shared<PathsFileWriter>(singleoutputfilename, (FILE*)NULL, header, saveFormat));
            pathwriters_toolpath   .push_back(pathwriters_arefiles.back());
            if (alsoContours) {
                pathwriters_raw    .push_back(pathwriters_arefiles.back());
                pathwriters_contour.push_back(pathwriters_arefiles.back());
            }
        }

        if (!saveInGridConf.empty() && (save || saveDXF)) {
            for (auto &conf : saveInGridConf) {
                conf.min = bbmn;
                conf.max = bbmx;
            }
            SplittingSubPathWriterCreator callback = [save, saveFormat, saveDXF, dxfmode, &dxf_filename_toolpaths, &dxf_filename_contours, epsilon_meshunits, &singleoutputfilename, &header](int idx, PathSplitter& splitter, std::string &fname, std::string suffix, bool generic_type, bool generic_ntool, bool generic_z) {
                std::shared_ptr<PathWriter> d = std::make_shared<PathWriterDelegator>(fname+suffix);
                PathWriterDelegator *delegator = static_cast<PathWriterDelegator*>(d.get());
                if (save) {
                    delegator->addWriter(std::make_shared<PathsFileWriter>(singleoutputfilename+suffix, (FILE*)NULL, header, saveFormat), [](int type, int ntool, double z) {return true; });
                }
                if (saveDXF) {
                    auto dxfCreator = [dxfmode, epsilon_meshunits, generic_type, generic_ntool, generic_z](std::string fn) {
                        std::shared_ptr<PathWriter> w;
                        if (dxfmode == DXFAscii) {
                            w = std::make_shared<DXFAsciiPathWriter >(fn, epsilon_meshunits, generic_type, generic_ntool, generic_z);
                        } else {
                            w = std::make_shared<DXFBinaryPathWriter>(fn, epsilon_meshunits, generic_type, generic_ntool, generic_z);
                        }
                        return w;
                    };
                    if (!dxf_filename_toolpaths.empty()) {
                        delegator->addWriter(dxfCreator(dxf_filename_toolpaths + suffix),
                            [](int type, int ntool, double z) { return (type == PATHTYPE_TOOLPATH_PERIMETER) || (type == PATHTYPE_TOOLPATH_INFILLING); });
                    }
                    if (!dxf_filename_contours.empty()) {
                        delegator->addWriter(dxfCreator(dxf_filename_contours + suffix),
                            [](int type, int ntool, double z) { return type == PATHTYPE_PROCESSED_CONTOUR; });
                    }
                }
                return d;
            };
            bool saveInGridConf_justone = saveInGridConf.size() == 1;
            std::shared_ptr<PathWriter> w = std::make_shared<SplittingPathWriter>(*multispec, callback, saveInGridConf, "SPLITTING_DELEGATOR");
            pathwriters_arefiles.push_back(w);
            if (save || (!dxf_filename_toolpaths.empty())) {
                pathwriters_toolpath.push_back(w);
            }
            if (save && alsoContours && saveInGridConf_justone) {
                //if we do not include !saveInGridConf_justone in the condition, errors will happen down the writer pipeline. It is just easier not writing raw contours in this case...
                pathwriters_raw.push_back(w);
            }
            if ((save && alsoContours) || (!dxf_filename_contours.empty())) {
                pathwriters_contour.push_back(w);
            }
        }
    }

    //for now, we do not need to store intermediate results, but let the code live in case we need it later
    bool saveContours = false;
    std::vector<std::shared_ptr<ResultSingleTool>> results;

    try {

        if (multispec->global.useScheduler) {

            bool removeUnused = true; //!saveContours;
            SimpleSlicingScheduler sched(removeUnused, multispec);

            if (multispec->global.schedMode == ManualScheduling) {
                for (auto pair = multispec->global.schedSpec.begin(); pair != multispec->global.schedSpec.end(); ++pair) {
                    pair->z *= factors.input_to_internal;
                }
            }

            sched.createSlicingSchedule(minz*factors.input_to_internal, maxz*factors.input_to_internal, multispec->global.z_epsilon, ScheduleTwoPhotonSimple);

            if (sched.has_err) {
                fprintf(stderr, "Error while trying to create the slicing schedule: %s\n", sched.err.c_str());
                return -1;
            }

            int schednuminputslices = (int)sched.rm.raw.size();
            int schednumoutputslices = (int)sched.output.size();

            std::vector<double> rawZs = sched.rm.rawZs;
            for (auto z = rawZs.begin(); z != rawZs.end(); ++z) {
                *z *= factors.internal_to_input;
            }

            if (dryrun) {
                printf("dry run:\n\nThese are the %d Z values of the required slices from the mesh file (raw slices), in request order:\n", rawZs.size());
                for (const auto &z : rawZs) {
                    printf("%.20g\n", z);
                }
                printf("\nThese are the %d pairs of NTool number and Z value for the slices to be computed, in the required computing order:\n", sched.input.size());
                for (const auto &input : sched.input) {
                    printf("%d %.20g\n", input.ntool, input.z*factors.internal_to_input);
                }
                slicer->terminate();
                return 0;
            }

            if (multispec->global.fb.feedback) {
                std::string err = applyFeedback(*config, factors, sched, rawZs, sched.rm.rawZs);
                if (!err.empty()) {
                    fprintf(stderr, err.c_str());
                    return -1;
                }
            }

            slicer->sendZs(&(rawZs.front()), schednuminputslices);

            if (show) {
                numoutputs = alsoContours ? schednuminputslices + schednumoutputslices * 3 : schednumoutputslices * 2;
                pathwriter_viewer->setNumRecords(numoutputs);
            }

            if (saveContours) {
                results.reserve(schednumoutputslices);
            }

            for (int i = 0; i < schednuminputslices; ++i) {
                printf("reading raw slice %d/%d\n", i, schednuminputslices - 1);
                rawslice.clear();

                slicer->readNextSlice(rawslice); {
                    std::string err = slicer->getErrorMessage();
                    if (!err.empty()) {
                        fprintf(stderr, "Error while trying to read the %d-th slice from the slicer manager: %s!!!\n", i, err.c_str());
                    }
                }

                for (auto &w : pathwriters_raw) {
                    if (!w->writePaths(rawslice, PATHTYPE_RAW_CONTOUR, 0, -1, rawZs[i], factors.internal_to_input, true)) {
                        fprintf(stderr, "Error writing raw contour for z=%f: %s\n", rawZs[i], w->err.c_str());
                    }
                }

                //after this, sched.rm takes ownership of the contents of rawslice, so our variable is in an undefined state!!!!
                sched.rm.receiveNextRawSlice(rawslice);

                sched.computeNextInputSlices();
                if (sched.has_err) {
                    fprintf(stderr, "Error in computeNextInputSlices: %s", sched.err.c_str());
                    return -1;
                }
                while (1) {
                    if (sched.output_idx >= sched.output.size()) break;
                    std::shared_ptr<ResultSingleTool> single = sched.giveNextOutputSlice(); //this method will return slices in the ordering
                    if (saveContours) results.push_back(single);
                    if (sched.has_err) {
                        fprintf(stderr, "Error in giveNextOutputSlice.1: %s\n", sched.err.c_str());
                        return -1;
                    }
                    if (!single) break;
                    if (single->has_err) {
                        fprintf(stderr, "Error in giveNextOutputSlice.2: %s\n", single->err.c_str());
                        return -1;
                    }
                    printf("received output slice %d/%d (ntool=%d, z=%f)\n", single->idx, sched.output.size()-1, single->ntool, single->z);
                    double zscaled = single->z                          * factors.internal_to_input;
                    double rad     = multispec->pp[single->ntool].radius * factors.internal_to_input;
                    for (auto &pathwriter : pathwriters_toolpath) {
                        if (!pathwriter->writePaths(single->ptoolpaths, PATHTYPE_TOOLPATH_PERIMETER, rad, single->ntool, zscaled, factors.internal_to_input, false)) {
                            fprintf(stderr, "Error writing perimeter toolpaths for ntool=%d, z=%f: %s\n", single->ntool, zscaled, pathwriter->err.c_str());
                            return -1;
                        }
                        if (!pathwriter->writePaths(single->itoolpaths, PATHTYPE_TOOLPATH_INFILLING, rad, single->ntool, zscaled, factors.internal_to_input, false)) {
                            fprintf(stderr, "Error writing infilling toolpaths for ntool=%d, z=%f: %s\n", single->ntool, zscaled, pathwriter->err.c_str());
                            return -1;
                        }
                    }
                    for (auto &pathwriter : pathwriters_contour) {
                        if (!pathwriter->writePaths(single->contoursToShow, PATHTYPE_PROCESSED_CONTOUR, rad, single->ntool, zscaled, factors.internal_to_input, true)) {
                            fprintf(stderr, "Error writing contours  for ntool=%d, z=%f: %s\n", single->ntool, zscaled, pathwriter->err.c_str());
                            return -1;
                        }
                    }
                }
            }

        } else {

            Multislicer multi(multispec);
            std::vector<ResultSingleTool> res;
            std::vector<SingleProcessOutput*> ress(numtools);
            double zstep = multispec->global.z_uniform_step;

            std::vector<double> zs;
            if (multispec->global.use_z_base) {
                zs = prepareSTLSimple(minz, maxz, multispec->global.z_base, zstep);
            } else {
                zs = prepareSTLSimple(minz, maxz, zstep);
            }
            slicer->sendZs(&zs.front(), (int)zs.size());

            if (dryrun) {
                printf("dry run:\n\nThese are the %d Z values of the required slices from the mesh file (raw slices), in request order:\n", zs.size());
                for (const auto &z : zs) {
                    printf("%.20g\n", z);
                }
                slicer->terminate();
                return 0;
            }

            numsteps = (int64)zs.size();
            int numresults = (int) (numsteps * numtools);

            if (saveContours) {
                res.reserve(numtools);
            } else {
                results.reserve(numresults);
            }

            if (show) {
                //numoutputs: raw contours (numsteps), plus processed contours (numsteps*numtools), plus toolpaths (numsteps*numtools)
                numoutputs = alsoContours ? numsteps + numresults * 3 : numresults * 2;
                pathwriter_viewer->setNumRecords(numoutputs);
            }

            for (int i = 0; i < numsteps; ++i) {
                printf("processing raw slice %d/%d\n", i, numsteps - 1);

                rawslice.clear();
                dummy.clear();
                if (saveContours) {
                    for (int k = 0; k < numtools; ++k) {
                        results.push_back(std::make_shared<ResultSingleTool>(zs[i], k, (int)results.size()));
                        ress[k] = &*results.back();
                    }
                } else {
                    res.clear();
                    res.resize(numtools);
                    for (int k = 0; k < numtools; ++k) {
                        res[k].z     = zs[i];
                        res[k].ntool = k;
                        res[k].idx   = k;
                        ress[k]      = &(res[k]);
                    }
                }

                slicer->readNextSlice(rawslice); {
                    std::string err = slicer->getErrorMessage();
                    if (!err.empty()) {
                        fprintf(stderr, "Error while trying to read the %d-th slice from the slicer manager: %s!!!\n", i, err.c_str());
                    }
                }

                for (auto &w : pathwriters_raw) {
                    if (!w->writePaths(rawslice, PATHTYPE_RAW_CONTOUR, 0, -1, zs[i], factors.internal_to_input, true)) {
                        fprintf(stderr, "Error writing raw contour for z=%f: %s\n", zs[i], w->err.c_str());
                    }
                }

                int lastk = multi.applyProcesses(ress, rawslice, dummy);
                if (lastk != numtools) {
                    fprintf(stderr, "Error in applyProcesses (raw slice %d, last tool %d): %s\n", i, lastk, ress[lastk]->err.c_str());
                    return -1;
                }

                for (int k = 0; k < numtools; ++k) {
                    double rad     = multispec->pp[k].radius * factors.internal_to_input;
                    for (auto &pathwriter : pathwriters_toolpath) {
                        if (!pathwriter->writePaths(ress[k]->ptoolpaths, PATHTYPE_TOOLPATH_PERIMETER, rad, k, zs[i], factors.internal_to_input, false)) {
                            fprintf(stderr, "Error writing perimeter toolpaths  for ntool=%d, z=%f: %s\n", k, zs[i], pathwriter->err.c_str());
                            return -1;
                        }
                        if (!pathwriter->writePaths(ress[k]->itoolpaths, PATHTYPE_TOOLPATH_INFILLING, rad, k, zs[i], factors.internal_to_input, false)) {
                            fprintf(stderr, "Error writing infilling toolpaths  for ntool=%d, z=%f: %s\n", k, zs[i], pathwriter->err.c_str());
                            return -1;
                        }
                    }
                    for (auto &pathwriter : pathwriters_contour) {
                        if (!pathwriter->writePaths(ress[k]->contoursToShow, PATHTYPE_PROCESSED_CONTOUR, rad, k, zs[i], factors.internal_to_input, true)) {
                            fprintf(stderr, "Error writing contours  for ntool=%d, z=%f: %s\n", k, zs[i], pathwriter->err.c_str());
                            return -1;
                        }
                    }
                }
            }
        }
    } catch (clp::clipperException &e) {
        std::string err = handleClipperException(e);
        fprintf(stderr, err.c_str());
    } catch (std::exception &e) {
        fprintf(stderr, "Unhandled exception while computing the output.\n   Exception    type: %s\n   Exception message: %s\n", typeid(e).name(), e.what()); return -1;
    }

    results.clear();

    if (!slicer->finalize()) {
        std::string err = slicer->getErrorMessage();
        fprintf(stderr, "Error while finalizing the slicer manager: %s!!!!", err.c_str());
    }

    for (auto &pathwriter : pathwriters_arefiles) {
        if (!pathwriter->close()) {
            fprintf(stderr, "Error trying to close writer <%s>: %s\n", pathwriter->filename.c_str(), pathwriter->err.c_str());
        }
    }

#ifdef STANDALONE_USEPYTHON
    if (show) {
        fflush(slicesViewer->pipeIN);
        slicesViewer->wait();
    }
#endif

    return 0;
}

int main(int argc, const char** argv) {
    try {
        return Main(argc, argv);
    } catch (std::exception &e) {
        fprintf(stderr, "Unhandled exception NOT while computing the output.\n   Exception    type: %s\n   Exception message: %s\n", typeid(e).name(), e.what()); return -1;
    } catch (std::string &e) {
        fprintf(stderr, "Unhandled exception NOT while computing the output (string literal): %s\n", e.c_str());
    } catch (const char *e) {
        fprintf(stderr, "Unhandled exception NOT while computing the output (string literal): ");
        fflush( stderr);
        fprintf(stderr, "%s\n", e);
    }
    return -1;
}
