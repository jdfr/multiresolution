//this is a simple command line application that organizes the execution of the multislicer

#include "parsing.hpp"
#include "slicermanager.hpp"
#include "3d.hpp"
#include "pathwriter.hpp"
#include <iostream>

//if macro STANDALONE_USEPYTHON is defined, SHOWCONTOUR support is baked in
#ifdef STANDALONE_USEPYTHON
#    include "showcontours.hpp"
#    include "sliceviewer.hpp"
#endif

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
            po::value<std::string>()->value_name("filename"),
            "input mesh file")
        ("save",
            po::value<std::string>()->value_name("filename"),
            "output file in *.paths format")
        ("save-format",
            po::value<std::string>()->default_value("integer"),
            "Format of coordinates in the save file, either 'integer' or 'double'. The default is 'integer'")
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
    std::string meshfullpath;

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

    std::vector<std::shared_ptr<PathWriter>> pathwriters_arefiles;    //everything which has to be closed
    std::vector<std::shared_ptr<PathWriter>> pathwriters_raw;         //everything receiving raw slices
    std::vector<std::shared_ptr<PathWriter>> pathwriters_contour;     //everything receiving contours
    std::vector<std::shared_ptr<PathWriter>> pathwriters_toolpath;    //everything receiving toolpaths
    std::vector<std::shared_ptr<PathsFileWriter>> pathwriters_native; //everything outputting in native format
    std::shared_ptr<PathsFileWriter> pathwriter_viewer;               //PathWriter in native format for the viewer

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

        std::string dxfm = std::move(mainOpts["dxf-format"].as<std::string>());
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
        bool generic_by_ntool = mainOpts.count("dxf-by-tool") == 0;
        bool generic_by_z     = mainOpts.count("dxf-by-z")    == 0;
        auto dxf_modes = { "dxf-toolpaths", "dxf-contours" };
        std::vector<std::shared_ptr<PathWriter>>* specific_pathwriter_vector [] = { &pathwriters_toolpath, &pathwriters_contour };

        int k = 0;
        for (auto &dxf_mode : dxf_modes) {
            if (mainOpts.count(dxf_mode) != 0) {
                std::shared_ptr<PathWriter> w;
                std::string fn          = std::move(mainOpts[dxf_mode].as<std::string>());
                const bool generic_type = true;
                double epsilon          = multispec.global.z_epsilon*factors.internal_to_input;
                if (dxfmode == DXFAscii) {
                    w = std::make_shared<DXFAsciiPathWriter>(std::move(fn), epsilon, generic_type, generic_by_ntool, generic_by_z);
                } else {
                    w = std::make_shared<DXFBinaryPathWriter>(std::move(fn), epsilon, generic_type, generic_by_ntool, generic_by_z);
                }
                pathwriters_arefiles.push_back(w);
                specific_pathwriter_vector[k]->push_back(w);
            }
            ++k;
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

    std::shared_ptr<SlicerManager> slicer = getSlicerManager(config, SlicerManagerExternal);
    //SlicerManager *slicer = getSlicerManager(SlicerManagerNative);
#ifdef STANDALONE_USEPYTHON
    std::shared_ptr<SlicesViewer> slicesViewer;
    if (show) {
        slicesViewer = std::make_shared<SlicesViewer>(config, "view slices", use2d, viewparams.c_str());
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

    bool alsoContours = multispec.global.alsoContours;
    clp::Paths rawslice, dummy;
    int64 numoutputs, numsteps;
    int numtools = (int)multispec.numspecs;
    if (save || show) {
        std::shared_ptr<FileHeader> header = std::make_shared<FileHeader>(multispec, factors);
#ifdef STANDALONE_USEPYTHON
        if (show) {
            pathwriter_viewer = std::make_shared<PathsFileWriter>("sliceViewerStream", slicesViewer->pipeIN, header, PATHFORMAT_INT64);
            pathwriters_native     .push_back(pathwriter_viewer);
            pathwriters_toolpath   .push_back(pathwriters_native.back());
            if (alsoContours) {
                pathwriters_raw    .push_back(pathwriters_native.back());
                pathwriters_contour.push_back(pathwriters_native.back());
            }
        }
#endif
        if (save) {
            pathwriters_native     .push_back(std::make_shared<PathsFileWriter>(singleoutputfilename, (FILE*)NULL, header, saveFormat));
            pathwriters_arefiles   .push_back(pathwriters_native.back());
            pathwriters_toolpath   .push_back(pathwriters_native.back());
            if (alsoContours) {
                pathwriters_raw    .push_back(pathwriters_native.back());
                pathwriters_contour.push_back(pathwriters_native.back());
            }
        }
    }

    //for now, we do not need to store intermediate results, but let the code live in case we need it later
    bool saveContours = false;
    std::vector<std::shared_ptr<ResultSingleTool>> results;

    try {

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

            if (multispec.global.fb.feedback) {
                std::string err = applyFeedback(config, factors, sched, rawZs, sched.rm.rawZs);
                if (!err.empty()) {
                    fprintf(stderr, err.c_str());
                    return -1;
                }
            }

            slicer->sendZs(&(rawZs[0]), schednuminputslices);

            if (!pathwriters_native.empty()) {
                numoutputs = alsoContours ? schednuminputslices + schednumoutputslices * 2 : schednumoutputslices;
                for (auto &w : pathwriters_native) w->setNumRecords(numoutputs);
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
                    if (single == NULL) break;
                    if (single->has_err) {
                        fprintf(stderr, "Error in giveNextOutputSlice.2: %s\n", single->err.c_str());
                        return -1;
                    }
                    printf("received output slice %d/%d (ntool=%d, z=%f)\n", single->idx, sched.output.size()-1, single->ntool, single->z);
                    double zscaled = single->z                          * factors.internal_to_input;
                    double rad     = multispec.pp[single->ntool].radius * factors.internal_to_input;
                    for (auto &pathwriter : pathwriters_toolpath) {
                        if (!pathwriter->writePaths(single->toolpaths, PATHTYPE_TOOLPATH, rad, single->ntool, zscaled, factors.internal_to_input, false)) {
                            fprintf(stderr, "Error writing toolpaths  for ntool=%d, z=%f: %s\n", single->ntool, zscaled, pathwriter->err.c_str());
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
            double zstep = multispec.global.z_uniform_step;

            std::vector<double> zs;
            if (multispec.global.use_z_base) {
                zs = slicer->prepareSTLSimple(multispec.global.z_base, zstep);
            } else {
                zs = slicer->prepareSTLSimple(zstep);
            }

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

            if (!pathwriters_native.empty()) {
                //numoutputs: raw contours (numsteps), plus processed contours (numsteps*numtools), plus toolpaths (numsteps*numtools)
                numoutputs = alsoContours ? numsteps + numresults * 2 : numresults;
                for (auto &w : pathwriters_native) w->setNumRecords(numoutputs);
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
                    double rad     = multispec.pp[k].radius * factors.internal_to_input;
                    for (auto &pathwriter : pathwriters_toolpath) {
                        if (!pathwriter->writePaths(ress[k]->toolpaths, PATHTYPE_TOOLPATH, rad, k, zs[i], factors.internal_to_input, false)) {
                            fprintf(stderr, "Error writing toolpaths  for ntool=%d, z=%f: %s\n", k, zs[i], pathwriter->err.c_str());
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
        fprintf(stderr, "Unhandled exception: %s\n", e.what()); return -1;
    }

    results.clear();

    if (!slicer->finalize()) {
        std::string err = slicer->getErrorMessage();
        fprintf(stderr, "Error while finalizing the slicer manager: %s!!!!", err.c_str());
    }

    for (auto &pathwriter : pathwriters_arefiles) {
        if (!pathwriter->close()) {
            fprintf(stderr, "Error trying to close file <%s>: %s\n", pathwriter->filename.c_str(), pathwriter->err.c_str());
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
