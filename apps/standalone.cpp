//this is a simple command line application that organizes the execution of the multislicer

//if macro STANDALONE_USEPYTHON is defined, SHOWCONTOUR support is baked in
#define STANDALONE_USEPYTHON

#include "spec.hpp"
#include "slicermanager.hpp"
#include "multislicer.hpp"
#include "3d.hpp"
#include "iopaths.hpp"
#include <stdio.h>
#include <sstream>
#include <string>

#ifdef STANDALONE_USEPYTHON
#    include "showcontours.hpp"
#    include "sliceviewer.hpp"
#endif

#define TYPE_RAW_CONTOUR       0
#define TYPE_PROCESSED_CONTOUR 1
#define TYPE_TOOLPATH          2

//utility template for iopaths functionality
template<typename Function, typename... Args> void applyToAllFiles(FILES &files, Function function, Args... args) {
    for (auto file = files.begin(); file != files.end(); ++file) {
        function(*file, args...);
    }
}

void writeSlice(FILES files, int64 type, int64 ntool, double z, clp::Paths &paths, double scalingFactor, PathCloseMode mode) {
    applyToAllFiles(files, writeInt64, type);
    applyToAllFiles(files, writeInt64, ntool);
    applyToAllFiles(files, writeDouble, z);
    //applyToAllFiles(files, writeClipperPaths, paths, mode);
    writeDoublePaths(files, paths, scalingFactor, mode);
}

const char *ERR =
"\nArguments: CONFIGFILENAME MESHFILENAME (show (3d (spec SPEC_3D | nspec) | 2d (spec SPEC_2D | nspec | debug)) | nshow) MULTISLICING_PARAMETERS (save OUTPUTFILENAME | multisave OUTPUT1 OUTPUT2 ... | nsave)\n\n"
"    -CONFIGFILENAME is required (config file name).\n\n"
"    -MESHFILENAME is required (input mesh file name).\n\n"
"    -If 'show 3d nspec' is specified, a 3D view of the contours will be generated with mayavi after all is computed (viewing parameters are set in the config file)\n\n"
"    -If 'show 3d spec SPEC_3D' is the same as the previous one, but SPEC_3D is a python expression for specifying the viewing parameters\n\n"
"    -If 'show 2d nspec' is specified, Z-navigable 2D views of contours will be generated with matplotlib after they are computed (viewing parameters are set in the config file)\n\n"
"    -If 'show 2d spec SPEC_2D' is the same as the previous one, but SPEC_2D is a python expression for specifying the viewing parameters\n\n"
"    -If 'show 2d debug' is specified, 2D views of contours will be generated as they are computed (computation will be interrumpted until the viewing window is closed)\n\n"
"    -MULTISLICING_PARAMETERS represents the parameters specifying the multislicing process all further arguments, which are evaluated verbatim by the multislicing engine (in particular, metric arguments execept z_uniform_step must be supplied in the engine's native unit)\n\n"
"    -If 'save OUTPUTFILENAME' is specified, OUTPUTFILENAME is the name of the file to save all toolpaths\n\n"
"    -If 'multisave OUTPUT1 OUTPUT2 ...' is specified, must be as many output files as processes.\n\n";

void printError(ParamReader &rd) {
    rd.fmt << ERR;
    std::string err = rd.fmt.str();
    fprintf(stderr, err.c_str());
}

ParamReader getParamReader(int argc, const char **argv) {
    /*const char * PARAMETERS = R"("config.txt" "a3.stl" save "output.paths" show 2d nall contours ncorrect motion_opt - 1 - 1 nsched 0.1 simple 2 75 0.1 0.75 0.01 0.01 0 snap safestep clearance 1 1.0 ninfill 10 0.1 0.1 0.001 0.1 0 snap safestep clearance 1 1.0 ninfill)";
    return ParamReader(PARAMETERS, ParamString);*/

    //first argument is exec's filename
    if (argc == 2) {
        return ParamReader(argv[1], ParamFile);
    } else {
        return ParamReader(0, --argc, ++argv);
    }
}

int main(int argc, const char** argv) {
    ParamReader rd = getParamReader(argc, argv);

    if (!rd.err.empty()) {
        fprintf(stderr, "ParamReader error: %s\n", rd.err.c_str());
        return -1;
    }

    const char *configfilename, *meshfilename = NULL;
    
    if (!rd.readParam(configfilename,  "CONFIGFILENAME"))         { printError(rd); return -1; }
    if (!rd.readParam(meshfilename,    "MESHFILENAME"))           { printError(rd); return -1; }

    bool show, use2d, showAtEnd, useviewparams;
    const char *viewmode, *viewparams=NULL;
    if (!rd.readParam(show, "show",    "show flag (show/nshow)")) { printError(rd); return -1; }
    if (show) {
        if (!rd.readParam(use2d, "2d", "view mode flag (2d/3d)")) { printError(rd); return -1; }
        if (!rd.readParam(viewmode,    "viewing parameters flag (spec/nspec/debug)")) { printError(rd); return -1; }
        useviewparams = strcmp(viewmode, "spec") == 0;
        if (useviewparams) {
          if (!rd.readParam(viewparams,"python viewing parameters")) { printError(rd); return -1; }
        }
        showAtEnd = strcmp(viewmode, "debug") != 0;
        if ((!use2d) && (!showAtEnd)) {
            fprintf(stderr, "'3d debug' cannot be specified!!!");
            return -1;
        }
        if (use2d) {
            if (showAtEnd) {
#               ifndef STANDALONE_USEPYTHON
                    fprintf(stderr, "ERROR: 'show 2d all' VIEWING MODE UNAVAILABLE. PLEASE RECONFIGURE AND REBUILD TO ADD SUPPORT!!!!\n\n");
                    return -1;
#               endif
            } else {
#               ifndef STANDALONE_USEPYTHON
                    fprintf(stderr, "ERROR: 'show 2d nall' VIEWING MODE UNAVAILABLE. PLEASE RECONFIGURE AND REBUILD TO ADD SUPPORT!!!!\n\n");
                    return -1;
#               endif
            }
        } else {
#           ifndef STANDALONE_USEPYTHON
                fprintf(stderr, "ERROR: 'show 3d' VIEWING MODE UNAVAILABLE. PLEASE RECONFIGURE AND REBUILD TO ADD SUPPORT!!!!\n\n");
                return -1;
#           endif
        }
    }

    Arguments args(configfilename);

    if (args.config->has_err) {
        fprintf(stderr, "Error reading configuration file: %s", args.config->err.c_str());
        return -1;
    }

    //skip app name and mesh filename
    if (!args.readArguments(true, rd)) {
        fprintf(stderr, "Error while parsing and populating arguments: %s", args.err.c_str());
        return -1;
    }

    double input_to_internal_factor, internal_to_input_factor;
    double input_to_slicer_factor, slicer_to_internal_factor;
    std::string val_input_to_slicer_factor, val_slicer_to_internal_factor;
    if (args.config->hasKey("INPUT_TO_SLICER_FACTOR")) {
        val_input_to_slicer_factor = args.config->getValue("INPUT_TO_SLICER_FACTOR");
    } else {
        fprintf(stderr, "the configuration value INPUT_TO_SLICER_FACTOR was not found in the configuration file!");
        return -1;
    }
    if (args.config->hasKey("SLICER_TO_INTERNAL_FACTOR")) {
        val_slicer_to_internal_factor = args.config->getValue("SLICER_TO_INTERNAL_FACTOR");
    } else {
        fprintf(stderr, "the configuration value SLICER_TO_INTERNAL_FACTOR was not found in the configuration file!");
        return -1;
    }
    input_to_slicer_factor = strtod(val_input_to_slicer_factor.c_str(), NULL);
    slicer_to_internal_factor = strtod(val_slicer_to_internal_factor.c_str(), NULL);
    input_to_internal_factor = input_to_slicer_factor*slicer_to_internal_factor;
    internal_to_input_factor = 1 / input_to_internal_factor;

    bool save, savemultiple;
    const char * savemode, *singleoutputfilename=NULL;
    std::vector<const char *> multioutputfilenames;
    if (!rd.readParam(savemode, "save mode (save/multisave/nsave)")) { printError(rd); return -1; }
    savemultiple = strcmp(savemode, "multisave")==0;
    save = savemultiple || (strcmp(savemode, "save")==0);
    if (save) {
        if (savemultiple) {
            multioutputfilenames.resize(args.multispec->numspecs);
            for (int k = 0; k < multioutputfilenames.size(); ++k) {
                if (!rd.readParam(multioutputfilenames[k], "output file name for process ", k)) { printError(rd); return -1; }
            }
        } else {
            if (!rd.readParam(singleoutputfilename, "OUTPUTFILENAME")) { printError(rd); return -1; }
        }
    }

    if (rd.argidx != rd.argc) {
        fprintf(stderr, "ERROR: SOME REMAINING PARAMETERS HAVE NOT BEEN USED: \n");
        for (int k = rd.argidx; k < rd.argc; ++k) {
            fprintf(stderr, "   %d/%d: %s\n", k, rd.argc - 1, rd.argv[k]);
        }
        return -1;
    }

    if (!save && !show) {
        fprintf(stderr, "ERROR: computed contours would be neither saved nor shown!!!!!\n");
        return -1;
    }

    bool saveContours = show && ((!use2d) || showAtEnd);
    bool showInline = show && use2d && (!showAtEnd);
    bool shownotinline = show && showAtEnd;
    bool removeUnused = true; //!saveContours;
    bool write = save || shownotinline;

    FILES all_files;
    FILES current_files;
    FILES multitool_files;
    FILES singletool_files(multioutputfilenames.size(), NULL);
    FILE *singleoutput = NULL;
    int currentoutputfile;
    if (save) {
        if (savemultiple) {
            for (int k = 0; k < singletool_files.size(); ++k) {
                singletool_files[k] = fopen(multioutputfilenames[k], "wb");
                if (singletool_files[k] == NULL) {
                    fprintf(stderr, "Error trying to open this file for output: %s (for process %d)\n", singletool_files[k], k);
                    return -1;
                }
            }
            current_files.push_back(singletool_files[0]);
            currentoutputfile = (int)(current_files.size()-1);
        } else {
            singleoutput = fopen(singleoutputfilename, "wb");
            if (singleoutput == NULL) {
                fprintf(stderr, "Error trying to open this file for output: %s\n", singleoutputfilename);
                return -1;
            }
              current_files.push_back(singleoutput);
            multitool_files.push_back(singleoutput);
        }
    }

    SlicerManager *slicer = getSlicerManager(*args.config, SlicerManagerExternal);
    //SlicerManager *slicer = getSlicerManager(SlicerManagerNative);
#ifdef STANDALONE_USEPYTHON
    SlicesViewer *slicesViewer=NULL;
    if (shownotinline) {
        slicesViewer = new SlicesViewer(*args.config, "view slices", use2d, viewparams);
        std::string err = slicesViewer->start();
        if (!err.empty()) {
            fprintf(stderr, "Error while trying to launch SlicerViewer script: %s\n", err.c_str());
            return -1;
        }
          current_files.push_back(slicesViewer->pipeIN);
        multitool_files.push_back(slicesViewer->pipeIN);
    }
#endif

    COPYTO( multitool_files, all_files);
    COPYTO(singletool_files, all_files);

    if (!slicer->start(meshfilename)) {
        std::string err = slicer->getErrorMessage();
        fprintf(stderr, "Error while trying to start the slicer manager: %s!!!\n", err.c_str());
        return -1;
    }

    bool alsoContours = args.multispec->global.alsoContours;
    clp::Paths rawslice, dummy;
    int64 numoutputs, numsteps, numtools = args.multispec->numspecs;
    if (write) {
        int64 useSched = args.multispec->global.useScheduler;
        int64 onetool = 1;
        applyToAllFiles(all_files, writeInt64, numtools);
        applyToAllFiles(all_files, writeInt64, useSched);
        for (int k = 0; k < numtools; ++k) {
            double v;
            v = args.multispec->radiuses[k]*internal_to_input_factor;
            applyToAllFiles(all_files, writeDouble, v);
            if (useSched) {
                v = args.multispec->profiles[k]->getVoxelSemiHeight()*internal_to_input_factor;
                applyToAllFiles(all_files, writeDouble, v);
            }
        }
    }

    std::vector<std::shared_ptr<ResultSingleTool>> results;

    if (args.multispec->global.useScheduler) {

        SimpleSlicingScheduler sched(removeUnused, *args.multispec);

        double minz = 0, maxz = 0;

        slicer->getZLimits(&minz, &maxz);

        if (args.multispec->global.manualScheduler) {
            for (auto pair = args.multispec->global.schedSpec.begin(); pair != args.multispec->global.schedSpec.end(); ++pair) {
                pair->z *= input_to_internal_factor;
            }
        }

        sched.createSlicingSchedule(minz*input_to_internal_factor, maxz*input_to_internal_factor, args.multispec->global.z_epsilon, ScheduleTwoPhotonSimple);

        if (sched.has_err) {
            fprintf(stderr, "Error while trying to create the slicing schedule: %s\n", sched.err.c_str());
            return -1;
        }

        int schednuminputslices = (int)sched.rm.raw.size();
        int schednumoutputslices = (int)sched.output.size();

        if (write) {
            numoutputs = alsoContours ? schednuminputslices + schednumoutputslices * 2 : schednumoutputslices;
            applyToAllFiles(multitool_files, writeInt64, numoutputs);
            if (!singletool_files.empty()) {
                for (int k = 0; k < singletool_files.size(); ++k) {
                    int64 numouts = alsoContours ? sched.num_output_by_tool[k] * 2 + schednuminputslices : sched.num_output_by_tool[k];
                    writeInt64(singletool_files[k], numouts);
                }
            }
        }

        std::vector<double> rawZs = sched.rm.rawZs;
        for (auto z = rawZs.begin(); z != rawZs.end(); ++z) {
            *z *= internal_to_input_factor;
        }
        slicer->sendZs(&(rawZs[0]), schednuminputslices);

        if (saveContours) {
            results.reserve(schednumoutputslices);
        }

        for (int i = 0; i < schednuminputslices; ++i) {
            printf("reading raw slice %d/%d\n", i, schednuminputslices - 1);
            rawslice.clear();

            slicer->readNextSlice(rawslice);

            if (write && alsoContours) {
                writeSlice(all_files, TYPE_RAW_CONTOUR, -1, rawZs[i], rawslice, internal_to_input_factor, PathLoop);
            }

#           ifdef STANDALONE_USEPYTHON
                if (showInline) {
                    //SHOWCONTOURS(args.multispec->global.config, str("raw contour at Z ", sched.rm.raw[sched.rm.raw_idx].z), &rawslice);
                }
#           endif

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
                    if (savemultiple) {
                        current_files[currentoutputfile] = singletool_files[single->ntool];
                    }
                    double z = single->z * internal_to_input_factor;
                    writeSlice(    current_files, TYPE_TOOLPATH,          single->ntool, z, single->toolpaths,      internal_to_input_factor, PathOpen);
                    if (alsoContours) {
                        writeSlice(current_files, TYPE_PROCESSED_CONTOUR, single->ntool, z, single->contoursToShow, internal_to_input_factor, PathLoop);
                    }
                }
#               ifdef STANDALONE_USEPYTHON
                    if (showInline) {
                        SHOWCONTOURS(args.multispec->global.config,
                            str("processed contours at Z ", single->z, ", tool ", single->ntool),
                            &(single->contoursToShow), &(single->toolpaths));
                    }
#               endif

            }
        }

    } else {

        Multislicer multi(*args.multispec);
        std::vector<ResultSingleTool> res;
        std::vector<SingleProcessOutput*> ress(numtools);
        double zstep = args.multispec->global.z_uniform_step;

        //std::vector<double> zs = slicer->prepareSTLSimple(zstep, zstep);
        std::vector<double> zs = slicer->prepareSTLSimple(zstep);

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
            applyToAllFiles(multitool_files, writeInt64, numoutputs);
            if (!singletool_files.empty()) {
                for (int k = 0; k < singletool_files.size(); ++k) {
                    int64 numouts = alsoContours ? numsteps * 3 : numsteps;
                    writeInt64(singletool_files[k], numouts);
                }
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

            slicer->readNextSlice(rawslice);
#           ifdef STANDALONE_USEPYTHON
                //SHOWCONTOURS(args.multispec->global.config, "raw", &rawslice);
#           endif

            if (write && alsoContours) writeSlice(all_files, TYPE_RAW_CONTOUR, -1, zs[i], rawslice, internal_to_input_factor, PathLoop);

            int lastk = multi.applyProcesses(ress, rawslice, dummy);
            if (lastk != numtools) {
                fprintf(stderr, "Error in applyProcesses (raw slice %d, last tool %d): %s\n", i, lastk, ress[lastk]->err.c_str());
                return -1;
            }

            if (write) {
                for (int k = 0; k < numtools; ++k) {
                    if (savemultiple) {
                        current_files[currentoutputfile] = singletool_files[k];
                    }
                    writeSlice(    current_files, TYPE_TOOLPATH,          k, zs[i], ress[k]->toolpaths,      internal_to_input_factor, PathOpen);
                    if (alsoContours) {
                        writeSlice(current_files, TYPE_PROCESSED_CONTOUR, k, zs[i], ress[k]->contoursToShow, internal_to_input_factor, PathLoop);
                    }
                }
            }
#           ifdef STANDALONE_USEPYTHON
                if (showInline) {
                    std::vector<clp::Paths*> toshowv;
                    toshowv.push_back(&rawslice);
                    for (int k = 0; k < numtools; ++k) {
                        toshowv.push_back(&(ress[k]->toolpaths));
                    }
                    ShowContoursInfo info(args.multispec->global.config, str("slices at Z ", zs[i]));
                    showContours(toshowv, info);
                }
#           endif
        }

    }

    for (auto file = all_files.begin(); file != all_files.end(); ++file) {
        fflush(*file);
    }
    if (save) {
        if (savemultiple) {
            for (auto file = singletool_files.begin(); file != singletool_files.end(); ++file) {
                fclose(*file);
            }
        } else {
            fclose(singleoutput);
        }
    }

    results.clear();

    if (shownotinline) {
#ifdef STANDALONE_USEPYTHON
        slicesViewer->wait();
        delete slicesViewer;
#endif
    }

    if (!slicer->finalize()) {
        std::string err = slicer->getErrorMessage();
        fprintf(stderr, "Error while finalizing the slicer manager: %s!!!!", err.c_str());
    }
    delete slicer;

    return 0;
}


