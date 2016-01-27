//this is a simple command line application that organizes the execution of the multislicer

#include "parsing.hpp"
#include "slicermanager.hpp"
#include "3d.hpp"
#include "pathsfile.hpp"
#include <iostream>

//if macro STANDALONE_USEPYTHON is defined, SHOWCONTOUR support is baked in
#ifdef STANDALONE_USEPYTHON
#    include "showcontours.hpp"
#    include "sliceviewer.hpp"
#endif

//utility template for IO functionality
template<typename Function, typename... Args> std::string applyToAllFiles(FILES &files, Function function, Args... args) {
    for (auto file = files.begin(); file != files.end(); ++file) {
        std::string err = function(*file, args...);
        if (!err.empty()) return str("For ", file - files.begin(), "-th file: ", err);
    }
    return std::string();
}

template<typename Function, typename... Args> std::string applyToAllFilesWithIOP(FILES &files, IOPaths iop, Function function, Args... args) {
    for (auto file = files.begin(); file != files.end(); ++file) {
        iop.f = *file;
        if (!((iop.*function)(args...))) {
            return str("For ", file - files.begin(), "-th file: error <", iop.errs[0].message, "> in ", iop.errs[0].function);
        }
    }
    return std::string();
}

inline std::string writeSlices(FILES &files, clp::Paths &paths, PathCloseMode mode, int64 type, int64 ntool, double z, int64 saveFormat, double scaling) {
    return applyToAllFiles(files, writeSlice, SliceHeader(paths, mode, type, ntool, z, saveFormat, scaling), paths, mode);
}

typedef std::pair<po::options_description, po::positional_options_description> MainSpec;

MainSpec mainOptions() {
    po::options_description opts("Main options");
    opts.add_options()
        ("help,h",
            "produce help message")
        ("config,q",
            po::value<std::string>()->default_value("config.txt"),
            "configuration input file (if no file is provided, it is assumed to be config.txt)")
        ("load",
            po::value<std::string>(),
            "input mesh file")
        ("save",
            po::value<std::string>(),
            "output file in *.paths format")
        ("save-format",
            po::value<std::string>()->default_value("integer"),
            "Format of coordinates in the save file, either 'integer' or 'double'. The default is 'integer'")
        ("show,w",
            po::value<std::vector<std::string>>()->multitoken(),
            "show result options using a python script. The first value can be either '2d' or '3d' (the script will use matplotlib or mayavi, respecivey). The second value, if present, should be a python expression for specifying visual appearance of displayed elements for the python script (must be tailored to the show mode (2d or 3d)")
        ("dry-run,y",
            "if this option is specified, the system only shows information about the slices. First, it displays the Z values of the slices to be received from the input mesh file (raw slices). This is useful for crafting feedback pathsfiles to be used with the --feedback option. Then, if --slicing-scheduler was specified, it displays the ordered sequence of slices to be computed, exactly in the same format as the arguments of --slicing-manual (pairs NTool and Z), so this can be used as input for this option. Finally, the application terminates without doing anything else.")
        ;
    po::positional_options_description positional;
    positional.add("load", 1).add("save", 1);
    return MainSpec(opts, positional);
}

const int mainOptsIdx    = 0;
const int globalOptsIdx  = 1;
const int perProcOptsIdx = 2;
std::vector<po::parsed_options> slurpAllOptions(MainSpec &mainSpec, int argc, const char ** argv) {
    std::vector<const po::options_description*> optss;
    optss.reserve(3);
    optss.push_back(&mainSpec.first);
    optss.push_back(globalOptions());
    optss.push_back(perProcessOptions());
    auto args = getArgs(argc, argv);
    return sortOptions(optss, mainSpec.second, mainOptsIdx, NULL, args);
}

void usage(MainSpec &mainSpec) {
    std::cout << "Command line interface to the multislicing engine.\n  Some options have long and short names.\n  If there is no ambiguity, options can be specified as prefixes of their full names.\n"
              << mainSpec.first         << '\n'
              << *(globalOptions())     << '\n'
              << *(perProcessOptions()) << '\n';
}

int main(int argc, const char** argv) {
    char *meshfullpath;

    bool show, use2d, useviewparams;
    std::string viewparams;

    bool save;
    int64 saveFormat;

    std::string singleoutputfilename;

    bool dryrun;
    
    Configuration config;
    MultiSpec multispec(config);
    bool doscale = true;
    MetricFactors factors;

    try {
        MainSpec mainSpec = mainOptions();
        if (argc == 1) {
            usage(mainSpec);
            return 1;
        }
        std::vector<po::parsed_options> optsBySystem = slurpAllOptions(mainSpec, argc, argv);

        po::variables_map mainOpts;
        po::store(optsBySystem[mainOptsIdx], mainOpts);

        if (mainOpts.count("help")) {
            usage(mainSpec);
            return 1;
        }

        dryrun   = mainOpts.count("dry-run")  != 0;
        save     = mainOpts.count("save")     != 0;

        std::string meshfilename;
        if (mainOpts.count("load")) {
            meshfilename = std::move(mainOpts["load"].as<std::string>());
        } else if (!dryrun) {
            fprintf(stderr, "Error: load parameter has not been specified!");
        }

        std::string configfilename = std::move(mainOpts["config"].as<std::string>());

        if (!fileExists(configfilename.c_str())) { fprintf(stderr, "Could not open config file %s!!!!", configfilename.c_str()); return -1; }

        config.load(configfilename.c_str());

        if (config.has_err) { fprintf(stderr, config.err.c_str()); return -1; }

        factors.init(config, doscale);
        if (!factors.err.empty()) { fprintf(stderr, factors.err.c_str()); return -1; }

        std::string err = parseAll(multispec, optsBySystem[globalOptsIdx], optsBySystem[perProcOptsIdx], getScale(factors));
        if (!err.empty()) { fprintf(stderr, err.c_str()); return -1; }

        if (!fileExists(meshfilename.c_str())) { fprintf(stderr, "Could not open input mesh file %s!!!!", meshfilename.c_str()); return -1; }

        //this is necessary because the slicer may have a different working directory
        meshfullpath = fullPath(meshfilename.c_str());
        if (meshfullpath == NULL) {
            fprintf(stderr, "Error trying to resolve canonical path to the input mesh file: %s", meshfilename.c_str());
            return -1;
        }

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

        mainOpts = po::variables_map();
    } catch (std::exception &e) {
        fprintf(stderr, e.what()); return -1;
    }

    if (dryrun) {
        save = show = false;
    } else {
        if (!save && !show) {
            fprintf(stderr, "ERROR: computed contours would be neither saved nor shown!!!!!\n");
            return -1;
        }
    }

    FILES all_files;
    FILE *singleoutput = NULL;
    IOPaths iop;
    if (save) {
        singleoutput = fopen(singleoutputfilename.c_str(), "wb");
        if (singleoutput == NULL) {
            fprintf(stderr, "Error trying to open this file for output: %s\n", singleoutputfilename.c_str());
            return -1;
        }
        all_files.push_back(singleoutput);
    }

    SlicerManager *slicer = getSlicerManager(config, SlicerManagerExternal);
    //SlicerManager *slicer = getSlicerManager(SlicerManagerNative);
#ifdef STANDALONE_USEPYTHON
    SlicesViewer *slicesViewer=NULL;
    if (show) {
        slicesViewer = new SlicesViewer(config, "view slices", use2d, viewparams.c_str());
        std::string err = slicesViewer->start();
        if (!err.empty()) {
            fprintf(stderr, "Error while trying to launch SlicerViewer script: %s\n", err.c_str());
            return -1;
        }
        all_files.push_back(slicesViewer->pipeIN);
    }
#endif

    if (!slicer->start(meshfullpath)) {
        std::string err = slicer->getErrorMessage();
        fprintf(stderr, "Error while trying to start the slicer manager: %s!!!\n", err.c_str());
        free(meshfullpath);
        return -1;
    }
    free(meshfullpath);

    bool write = save || show;
    bool alsoContours = multispec.global.alsoContours;
    clp::Paths rawslice, dummy;
    int64 numoutputs, numsteps;
    int numtools = (int)multispec.numspecs;
    if (write) {
        FileHeader header(multispec, factors);
        applyToAllFiles(all_files, [&header](FILE *f) { return header.writeToFile(f, false); });
    }

    //for now, we do not need to store intermediate results, but let the code live in case we need it later
    bool saveContours = false;
    std::vector<std::shared_ptr<ResultSingleTool>> results;


    if (multispec.global.useScheduler) {

        bool removeUnused = true; //!saveContours;
        SimpleSlicingScheduler sched(removeUnused, multispec);

        double minz = 0, maxz = 0;

        slicer->getZLimits(&minz, &maxz);

        if (multispec.global.schedMode == ManualScheduling) {
            for (auto pair = multispec.global.schedSpec.begin(); pair != multispec.global.schedSpec.end(); ++pair) {
                pair->z *= factors.input_to_internal;
            }
        }

        sched.createSlicingSchedule(minz*factors.input_to_internal, maxz*factors.input_to_internal, multispec.global.z_epsilon, ScheduleTwoPhotonSimple);

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
            printf("dry run:\n\nThese are the Z values of the required slices from the mesh file (raw slices), in request order:\n");
            for (const auto &z : rawZs) {
                printf("%.20g\n", z);
            }
            printf("\nThese are the NTool number and Z value for the slices to be computed, in the required computing order:\n");
            for (const auto &input : sched.input) {
                printf("%d %.20g\n", input.ntool, input.z*factors.internal_to_input);
            }
            slicer->terminate();
            delete slicer;
            return 0;
        }

        if (multispec.global.fb.feedback) {
            std::string err = applyFeedback(config, factors, sched, rawZs, sched.rm.rawZs);
            if (!err.empty()) {
                fprintf(stderr, err.c_str());
                return -1;
            }
        }

        slicer->sendZs(&(rawZs[0]), schednuminputslices);

        if (write) {
            numoutputs = alsoContours ? schednuminputslices + schednumoutputslices * 2 : schednumoutputslices;
            std::string err = applyToAllFilesWithIOP(all_files, iop, &IOPaths::writeInt64, numoutputs);
            if (!err.empty()) {
                fprintf(stderr, err.c_str());
                return -1;
            }
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

            if (write && alsoContours) {
                std::string err = writeSlices(all_files, rawslice, PathLoop, PATHTYPE_RAW_CONTOUR, -1, rawZs[i], saveFormat, factors.internal_to_input);
                if (!err.empty()) { fprintf(stderr, "Error writing raw slice for z=%f: %s\n", rawZs[i], err.c_str()); return -1; }
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
                if (single == NULL) break;
                if (single->has_err) {
                    fprintf(stderr, "Error in giveNextOutputSlice.2: %s\n", single->err.c_str());
                    return -1;
                }
                printf("received output slice %d/%d (ntool=%d, z=%f)\n", single->idx, sched.output.size()-1, single->ntool, single->z);
                if (write) {
                    double z = single->z * factors.internal_to_input;
                    std::string err     = writeSlices(all_files, single->toolpaths,      PathOpen, PATHTYPE_TOOLPATH,          single->ntool, z, saveFormat, factors.internal_to_input);
                    if (!err.empty()) {     fprintf(stderr, "Error writing toolpaths for ntool=%d, z=%f: %s\n", single->ntool, single->z, err.c_str()); return -1; }
                    if (alsoContours) {
                        std::string err = writeSlices(all_files, single->contoursToShow, PathLoop, PATHTYPE_PROCESSED_CONTOUR, single->ntool, z, saveFormat, factors.internal_to_input);
                        if (!err.empty()) { fprintf(stderr, "Error writing contours  for ntool=%d, z=%f: %s\n", single->ntool, single->z, err.c_str()); return -1; }
                    }
                }
            }
        }

    } else {

        Multislicer multi(multispec);
        std::vector<ResultSingleTool> res;
        std::vector<SingleProcessOutput*> ress(numtools);
        double zstep = multispec.global.z_uniform_step;

        std::vector<double> zs;
        if (multispec.global.use_z_base) {
            zs = slicer->prepareSTLSimple(multispec.global.z_base, zstep);
        } else {
            zs = slicer->prepareSTLSimple(zstep);
        }

        if (dryrun) {
            printf("dry run:\n\nThese are the Z values of the required slices from the mesh file (raw slices), in request order:\n");
            for (const auto &z : zs) {
                printf("%.20g\n", z);
            }
            slicer->terminate();
            delete slicer;
            return 0;
        }

        numsteps = (int64)zs.size();
        int numresults = (int) (numsteps * numtools);

        if (saveContours) {
            res.reserve(numtools);
        } else {
            results.reserve(numresults);
        }

        if (write) {
            //numoutputs: raw contours (numsteps), plus processed contours (numsteps*numtools), plus toolpaths (numsteps*numtools)
            numoutputs = alsoContours ? numsteps + numresults * 2 : numresults;
            std::string err = applyToAllFilesWithIOP(all_files, iop, &IOPaths::writeInt64, numoutputs);
            if (!err.empty()) {
                fprintf(stderr, err.c_str());
            }
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
                    res[k].z = zs[i];
                    res[k].ntool = k;
                    res[k].idx = k;
                    ress[k] = &(res[k]);
                }
            }

            slicer->readNextSlice(rawslice); {
                std::string err = slicer->getErrorMessage();
                if (!err.empty()) {
                    fprintf(stderr, "Error while trying to read the %d-th slice from the slicer manager: %s!!!\n", i, err.c_str());
                }
            }
#           ifdef STANDALONE_USEPYTHON
                //SHOWCONTOURS(multispec.global.config, "raw", &rawslice);
#           endif

            if (write && alsoContours) {
                std::string err = writeSlices(all_files, rawslice, PathLoop, PATHTYPE_RAW_CONTOUR, -1, zs[i], saveFormat, factors.internal_to_input);
                if (!err.empty()) { fprintf(stderr, "Error writing raw slice for z=%f: %s\n", zs[i], err.c_str()); return -1; }
            }

            int lastk = multi.applyProcesses(ress, rawslice, dummy);
            if (lastk != numtools) {
                fprintf(stderr, "Error in applyProcesses (raw slice %d, last tool %d): %s\n", i, lastk, ress[lastk]->err.c_str());
                return -1;
            }

            if (write) {
                for (int k = 0; k < numtools; ++k) {
                    std::string err     = writeSlices(all_files, ress[k]->toolpaths,      PathOpen, PATHTYPE_TOOLPATH,          k, zs[i], saveFormat, factors.internal_to_input);
                    if (!err.empty()) {     fprintf(stderr, "Error writing toolpaths for ntool=%d, z=%f: %s\n", k, zs[i], err.c_str()); return -1; }
                    if (alsoContours) {
                        std::string err = writeSlices(all_files, ress[k]->contoursToShow, PathLoop, PATHTYPE_PROCESSED_CONTOUR, k, zs[i], saveFormat, factors.internal_to_input);
                        if (!err.empty()) { fprintf(stderr, "Error writing contours  for ntool=%d, z=%f: %s\n", k, zs[i], err.c_str()); return -1; }
                    }
                }
            }
        }

    }

    for (auto file = all_files.begin(); file != all_files.end(); ++file) {
        fflush(*file);
    }
    if (save) {
        fclose(singleoutput);
    }

    results.clear();

#ifdef STANDALONE_USEPYTHON
    if (show) {
        slicesViewer->wait();
        delete slicesViewer;
    }
#endif

    if (!slicer->finalize()) {
        std::string err = slicer->getErrorMessage();
        fprintf(stderr, "Error while finalizing the slicer manager: %s!!!!", err.c_str());
    }
    delete slicer;

    return 0;
}


