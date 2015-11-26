//this is a simple command line application that organizes the execution of the multislicer

//if macro STANDALONE_USEPYTHON is defined, SHOWCONTOUR support is baked in
#define STANDALONE_USEPYTHON

#include "app.hpp"
#include "config.hpp"
#include "spec.hpp"
#include "auxgeom.hpp"
#include <stdio.h>
#include <ctype.h>
#include <sstream>
#include <string>


std::string printPathInfo(const char * filename, bool verbose) {
    FILE * f = fopen(filename, "rb");
    if (f == NULL) { return str("Could not open input file ", filename); }
    IOPaths iop_f(f);

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", filename, ": ", err); }
    bool useSched = fileheader.useSched != 0;


    if (verbose) {
        fprintf(stdout, "File name: %s\n", filename);
        fprintf(stdout, "number of tools (processes): %d\n", fileheader.numtools);
        fprintf(stdout, "use Scheduling: %s\n", useSched ? "true" : "false");
        for (int k = 0; k < fileheader.numtools; ++k) {
            const char * padding = "\n   ";
            fprintf(stdout, "for tool %d:%sradius in X: %f", k, useSched ? padding : " ", fileheader.radiusX[k]);
            if (useSched) fprintf(stdout, "%sradius in Z: %f\n", padding, fileheader.radiusZ[k]);
        }
        fprintf(stdout, "Number of Records: %d\n", fileheader.numRecords);
        fprintf(stdout, "\n\n");
    }

    SliceHeader sliceheader;
    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string err = sliceheader.readFromFile(f);
        if (!err.empty())                   { fclose(f); return str("Error reading ", currentRecord, "-th slice header: ", err); }
        if (sliceheader.alldata.size() < 7) { fclose(f); return str("Error reading ", currentRecord, "-th slice header: header is too short!"); }


        if (verbose) {
            fprintf(stdout, "Record %d\n", currentRecord);
            switch (sliceheader.type) {
            case PATHTYPE_RAW_CONTOUR:       fprintf(stdout, "                  type: raw slice (sliced from mesh file)\n"); break;
            case PATHTYPE_PROCESSED_CONTOUR: fprintf(stdout, "                  type: contours for tool %d\n", sliceheader.ntool); break;
            case PATHTYPE_TOOLPATH:          fprintf(stdout, "                  type: toolpaths for tool %d\n", sliceheader.ntool); break;
            default:                         fprintf(stdout, "                  type: unknown (%d) for tool %d\n", sliceheader.type, sliceheader.ntool);
            }
            fprintf(stdout, "                  z: %.20g\n", sliceheader.z);
            switch (sliceheader.saveFormat) {
            case PATHFORMAT_INT64:     fprintf(stdout, "     coordinate format: 64-bit integers\n"); break;
            case PATHFORMAT_DOUBLE:    fprintf(stdout, "     coordinate format: double floating point\n"); break;
            case PATHFORMAT_DOUBLE_3D: fprintf(stdout, "     coordinate format: double floating point (3D paths)\n"); break;
            default:                   fprintf(stdout, "     coordinate format: unknown (%d)\n", sliceheader.saveFormat);
            }
            fprintf(stdout, "      %s scaling: %.20g\n", sliceheader.saveFormat == PATHFORMAT_INT64 ? "original" : "        ", sliceheader.scaling);
        } else {
            fprintf(stdout, "Record %d: ", currentRecord);
            switch (sliceheader.type) {
            case PATHTYPE_RAW_CONTOUR:       fprintf(stdout, "type=raw (from mesh file),  z=%.20g\n", sliceheader.z); break;
            case PATHTYPE_PROCESSED_CONTOUR: fprintf(stdout, "type=contour,  ntool=%d,     z=%.20g\n", sliceheader.ntool, sliceheader.z); break;
            case PATHTYPE_TOOLPATH:          fprintf(stdout, "type=toolpath, ntool=%d,     z=%.20g\n", sliceheader.ntool, sliceheader.z); break;
            default:                         fprintf(stdout, "type=%d (unknown), ntool=%d,  z=%.20g\n", sliceheader.type, sliceheader.ntool, sliceheader.z);
            }
        }

        if (verbose) {
            int numpaths;
            if (sliceheader.saveFormat == PATHFORMAT_INT64) {
                clp::Paths paths;
                if (!iop_f.readClipperPaths(paths)) {
                    fclose(f);
                    return str("Error reading ", currentRecord, "-th integer clipperpaths: could not read record ", currentRecord, " data!");
                }
                numpaths = (int)paths.size();
                BBox bb = getBB(paths);
                fprintf(stdout, "          bounding box:\n");
                fprintf(stdout, "       X: min=%.20g, max=%.20g\n", bb.minx*sliceheader.scaling, bb.maxx*sliceheader.scaling);
                fprintf(stdout, "       Y: min=%.20g, max=%.20g\n", bb.miny*sliceheader.scaling, bb.maxy*sliceheader.scaling);
            } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE) {
                DPaths paths;
                if (!iop_f.readDoublePaths(paths)) {
                    fclose(f);
                    return str("Error reading ", currentRecord, "-th double clipperpaths: could not read record ", currentRecord, " data!");
                }
                numpaths = (int)paths.size();
            } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE_3D) {
                Paths3D paths;
                if (!read3DPaths(iop_f, paths)) {
                    fclose(f);
                    return str("Error reading ", currentRecord, "-th 3d clipperpaths: could not read record ", currentRecord, " data!");
                }
                numpaths = (int)paths.size();
            }
            int payload   = (sliceheader.totalSize - sliceheader.headerSize) / sizeof(int64);
            int numpoints = (payload - numpaths - 1) / (sliceheader.saveFormat == PATHFORMAT_DOUBLE_3D ? 3 : 2);
            fprintf(stdout, "    number of elements: %d\n", numpaths);
            fprintf(stdout, "      number of points: %d\n", numpoints);
            /*
            fprintf(stdout, "   total size in bytes: %d\n", sliceheader.totalSize);
            fprintf(stdout, "  header size in bytes: %d\n", sliceheader.headerSize);
            fprintf(stdout, " payload size in bytes: %d\n", sliceheader.totalSize - sliceheader.headerSize);
            fprintf(stdout, "    \"  in 8-byte words: %d\n", (sliceheader.totalSize - sliceheader.headerSize) / sizeof(int64));
            */
            fprintf(stdout, "\n\n");
        } else {
            fseek(f, (long)(sliceheader.totalSize - sliceheader.headerSize), SEEK_CUR);
        }
    }

    fclose(f);

    return std::string();
}

const char *ERR =
"\nArguments: PATHSFILENAME_INPUT [verbose]\n\n"
"    -PATHSFILENAME_INPUT is the paths file name.\n\n"
"    -'verbose' if more information is to be printed.\n\n";

void printError(ParamReader &rd) {
    rd.fmt << ERR;
    std::string err = rd.fmt.str();
    fprintf(stderr, err.c_str());
}

int main(int argc, const char** argv) {
    ParamReader rd(0, --argc, ++argv); //we only have one parameter, so no param file loading here!

    if (!rd.err.empty()) {
        fprintf(stderr, "ParamReader error: %s\n", rd.err.c_str());
        return -1;
    }

    const char *pathsfilename_input, *verbose_input=NULL;

    if (!rd.readParam(pathsfilename_input, "PATHSFILENAME_INPUT"))           { printError(rd); return -1; }
    
    bool verbose = false;
    if (rd.readParam(verbose_input, "VERBOSE_INPUT")) {
        verbose = (tolower(verbose_input[0]) == 'v');
        if (!verbose) {
            fprintf(stderr, "if present, las argument must be 'verbose' (or at least start with 'v'), but it was <%s>\n", verbose_input);
            return -1;
        }
    }

    std::string err = printPathInfo(pathsfilename_input, verbose);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to show info: %s", err.c_str());
        return -1;
    }

    return 0;
}


