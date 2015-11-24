//this is a simple command line application that organizes the execution of the multislicer

#include "spec.hpp"
#include "slicermanager.hpp"
#include "multislicer.hpp"
#include "3d.hpp"
#include "app.hpp"
#include <stdio.h>
#include <ctype.h>
#include <sstream>
#include <string>
#include <stdlib.h>

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

const char *ERR =
"\nArguments: CONFIGFILENAME MESHFILENAME (show (3d (spec SPEC_3D | nspec) | 2d (spec SPEC_2D | nspec | debug)) | nshow) (save (float | integer) OUTPUTFILENAME | nsave) MULTISLICING_PARAMETERS\n\n"
"This list of arguments can be read from the command line, or a single argument can specify a text file from which tha arguments are read.\n\n"
"    -CONFIGFILENAME is required (config file name).\n\n"
"    -MESHFILENAME is required (input mesh file name).\n\n"
"    -If 'show 3d nspec' is specified, a 3D view of the contours will be generated with mayavi after all is computed (viewing parameters are set in the config file)\n\n"
"    -If 'show 3d spec SPEC_3D' is the same as the previous one, but SPEC_3D is a python expression for specifying the viewing parameters\n\n"
"    -If 'show 2d nspec' is specified, Z-navigable 2D views of contours will be generated with matplotlib after they are computed (viewing parameters are set in the config file)\n\n"
"    -If 'show 2d spec SPEC_2D' is the same as the previous one, but SPEC_2D is a python expression for specifying the viewing parameters\n\n"
"    -If 'show 2d debug' is specified, 2D views of contours will be generated as they are computed (computation will be interrumpted until the viewing window is closed)\n\n"
"    -If 'save MODE OUTPUTFILENAME' is specified, MODE is either 'float' (or just 'f') or 'integer' (or just 'i'), meaning the format of the points: contours in 'float' are ready to be consumed by other applications but should not be converted back and forth to raw data to avoid data degradation, 'integer' saves the raw data to avoid , but the config file is needed to scale the contours accurately. OUTPUTFILENAME is the name of the file to save all toolpaths\n\n"
"    -MULTISLICING_PARAMETERS represents the parameters specifying the multislicing process all further arguments, which are evaluated verbatim by the multislicing engine (in particular, metric arguments execept z_uniform_step must be supplied in the engine's native unit)\n\n";

void printError(ParamReader &rd) {
    rd.fmt << ERR;
    std::string err = rd.fmt.str();
    fprintf(stderr, err.c_str());
}


int main(int argc, const char** argv) {
    ParamReader rd = getParamReader(argc, argv);

    if (!rd.err.empty()) {
        fprintf(stderr, "ParamReader error: %s\n", rd.err.c_str());
        return -1;
    }

    const char *configfilename, *meshfilename;
    char *meshfullpath;
    
    if (!rd.readParam(configfilename,  "CONFIGFILENAME"))         { printError(rd); return -1; }
    if (!rd.readParam(meshfilename,    "MESHFILENAME"))           { printError(rd); return -1; }

    if (!fileExists(configfilename)) { fprintf(stderr, "Could not open config file %s!!!!",   configfilename); return -1; }
    if (!fileExists(  meshfilename)) { fprintf(stderr, "Could not open input mesh file %s!!!!", meshfilename); return -1; }

    //this is necessary because the slicer has a different working directory
    meshfullpath = fullPath(meshfilename);
    if (meshfullpath == NULL) {
        fprintf(stderr, "Error trying to resolve canonical path to the input mesh file");
        return -1;
    }

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

    bool save;
    int64 saveFormat;
    const char *savemode, *saveformat, *singleoutputfilename=NULL;
    if (!rd.readParam(savemode, "save mode (save/multisave/nsave)")) { printError(rd); return -1; }
    save = strcmp(savemode, "save")==0;
    if (save) {
        if (!rd.readParam(saveformat, "save format (f[loat]/i[nteger])")) { printError(rd); return -1; }
        char t = tolower(saveformat[0]);
        if        (t == 'i') {
            saveFormat = PATHFORMAT_INT64;
        } else if (t == 'f') {
            saveFormat = PATHFORMAT_DOUBLE;
        } else {
            fprintf(stderr, "save format parameter must start by either 'i' (integer) or 'f' (float)\n");
            return -1;
        }
        if (!rd.readParam(singleoutputfilename, "OUTPUTFILENAME")) { printError(rd); return -1; }
    } else {
        saveFormat = PATHFORMAT_DOUBLE;
    }

    if (!save && !show) {
        fprintf(stderr, "ERROR: computed contours would be neither saved nor shown!!!!!\n");
        return -1;
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

    MetricFactors factors(*args.config);
    if (!factors.err.empty()) {
        fprintf(stderr, factors.err.c_str());
        return -1;
    }

    if (rd.argidx != rd.argc) {
        fprintf(stderr, "ERROR: SOME REMAINING PARAMETERS HAVE NOT BEEN USED: \n");
        for (int k = rd.argidx; k < rd.argc; ++k) {
            fprintf(stderr, "   %d/%d: %s\n", k, rd.argc - 1, rd.argv[k]);
        }
        return -1;
    }

    bool saveContours = show && ((!use2d) || showAtEnd);
    bool showInline = show && use2d && (!showAtEnd);
    bool shownotinline = show && showAtEnd;
    bool removeUnused = true; //!saveContours;
    bool write = save || shownotinline;

    FILES all_files;
    FILE *singleoutput = NULL;
    IOPaths iop;
    if (save) {
        singleoutput = fopen(singleoutputfilename, "wb");
        if (singleoutput == NULL) {
            fprintf(stderr, "Error trying to open this file for output: %s\n", singleoutputfilename);
            return -1;
        }
        all_files.push_back(singleoutput);
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
        all_files.push_back(slicesViewer->pipeIN);
    }
#endif

    if (!slicer->start(meshfullpath)) {
        std::string err = slicer->getErrorMessage();
        fprintf(stderr, "Error while trying to start the slicer manager: %s!!!\n", err.c_str());
        return -1;
    }

    bool alsoContours = args.multispec->global.alsoContours;
    clp::Paths rawslice, dummy;
    int64 numoutputs, numsteps;
    int numtools = (int)args.multispec->numspecs;
    if (write) {
        FileHeader header(*args.multispec, factors);
        applyToAllFiles(all_files, [&header](FILE *f) { return header.writeToFile(f, false); });
    }

    std::vector<std::shared_ptr<ResultSingleTool>> results;

    if (args.multispec->global.useScheduler) {

        SimpleSlicingScheduler sched(removeUnused, *args.multispec);

        double minz = 0, maxz = 0;

        slicer->getZLimits(&minz, &maxz);

        if (args.multispec->global.schedMode == ManualScheduling) {
            for (auto pair = args.multispec->global.schedSpec.begin(); pair != args.multispec->global.schedSpec.end(); ++pair) {
                pair->z *= factors.input_to_internal;
            }
        }

        sched.createSlicingSchedule(minz*factors.input_to_internal, maxz*factors.input_to_internal, args.multispec->global.z_epsilon, ScheduleTwoPhotonSimple);

        if (sched.has_err) {
            fprintf(stderr, "Error while trying to create the slicing schedule: %s\n", sched.err.c_str());
            return -1;
        }

        int schednuminputslices = (int)sched.rm.raw.size();
        int schednumoutputslices = (int)sched.output.size();

        if (write) {
            numoutputs = alsoContours ? schednuminputslices + schednumoutputslices * 2 : schednumoutputslices;
            std::string err = applyToAllFilesWithIOP(all_files, iop, &IOPaths::writeInt64, numoutputs);
            if (!err.empty()) {
                fprintf(stderr, err.c_str());
                return -1;
            }
        }

        std::vector<double> rawZs = sched.rm.rawZs;
        for (auto z = rawZs.begin(); z != rawZs.end(); ++z) {
            *z *= factors.internal_to_input;
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
                std::string err = writeSlices(all_files, rawslice, PathLoop, PATHTYPE_RAW_CONTOUR, -1, rawZs[i], saveFormat, factors.internal_to_input);
                if (!err.empty()) { fprintf(stderr, "Error writing raw slice for z=%f: %s\n", rawZs[i], err.c_str()); return -1; }
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
                    double z = single->z * factors.internal_to_input;
                    std::string err     = writeSlices(all_files, single->toolpaths,      PathOpen, PATHTYPE_TOOLPATH,          single->ntool, z, saveFormat, factors.internal_to_input);
                    if (!err.empty()) {     fprintf(stderr, "Error writing toolpaths for ntool=%d, z=%f: %s\n", single->ntool, single->z, err.c_str()); return -1; }
                    if (alsoContours) {
                        std::string err = writeSlices(all_files, single->contoursToShow, PathLoop, PATHTYPE_PROCESSED_CONTOUR, single->ntool, z, saveFormat, factors.internal_to_input);
                        if (!err.empty()) { fprintf(stderr, "Error writing contours  for ntool=%d, z=%f: %s\n", single->ntool, single->z, err.c_str()); return -1; }
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

            slicer->readNextSlice(rawslice);
#           ifdef STANDALONE_USEPYTHON
                //SHOWCONTOURS(args.multispec->global.config, "raw", &rawslice);
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
        fclose(singleoutput);
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

    free(meshfullpath);

    return 0;
}


