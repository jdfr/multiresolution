#include "pathsfile.hpp"
#include "simpleparsing.hpp"
#include "apputil.hpp"
#include <limits>

std::string printPathInfo(const char * filename, int verbose) {
    FILEOwner i(filename, "rb");
    if (!i.isopen()) { return str("Could not open input file ", filename); }
    IOPaths iop_f(i.f);

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(i.f);
    if (!err.empty()) { return str("Error reading file header for ", filename, ": ", err); }
    bool useSched = fileheader.useSched != 0;


    if (verbose>0) {
        fprintf(stdout, "File name: %s\n", filename);
        fprintf(stdout, "number of tools (processes): %lld\n", fileheader.numtools);
        fprintf(stdout, "use Scheduling: %s\n", useSched ? "true" : "false");
        for (int k = 0; k < fileheader.numtools; ++k) {
            const char * padding = "\n   ";
            fprintf(stdout, "for tool %d:%sradius in X: %f", k, useSched ? padding : " ", fileheader.voxels[k].xrad);
            if (useSched) {
                fprintf(stdout, "%s           radius in Z: %f", padding, fileheader.voxels[k].zrad);
                fprintf(stdout, "%s  complete height in Z: %f", padding, fileheader.voxels[k].zheight);
                fprintf(stdout, "%sapplication point in Z: %f", padding, fileheader.voxels[k].z_applicationPoint);
            }
            fprintf(stdout, "\n");
        }
    }
    
    fprintf(stdout, "File version: %d\n", fileheader.version);
    if ((fileheader.version > 0) && (!fileheader.additional.empty())) {
        fprintf(stdout, "Additional metadata: " FMTSIZET " words\n", fileheader.additional.size());
        for (int i = 0; i < fileheader.additional.size(); ++i) {
            fprintf(stdout, "  %d int64: %21lld, double: %.12f\n", i, fileheader.additional[i].i, fileheader.additional[i].d);
        }
    }
    
    if (verbose>0) {
        fprintf(stdout, "Number of Records: %lld\n", fileheader.numRecords);
        fprintf(stdout, "\n\n");
    }

    SliceHeader sliceheader;
    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string err = sliceheader.readFromFile(i.f);
        if (!err.empty())                       { return str("Error reading ", currentRecord, "-th slice header: ", err); }
        const int usual = 7;
        if (sliceheader.alldata.size() < usual) { return str("Error reading ", currentRecord, "-th slice header: header is too short!"); }

        if (verbose>0) {
            fprintf(stdout, "Record %d\n", currentRecord);
            switch (sliceheader.type) {
            case PATHTYPE_RAW_CONTOUR:        fprintf(stdout, "                  type: raw slice (sliced from mesh file)\n"); break;
            case PATHTYPE_PROCESSED_CONTOUR:  fprintf(stdout, "                  type: contours for tool %lld\n", sliceheader.ntool); break;
            case PATHTYPE_TOOLPATH_PERIMETER: fprintf(stdout, "                  type: perimeter toolpaths for tool %lld\n", sliceheader.ntool); break;
            case PATHTYPE_TOOLPATH_SURFACE:   fprintf(stdout, "                  type: surface   toolpaths for tool %lld\n", sliceheader.ntool); break;
            case PATHTYPE_TOOLPATH_INFILLING: fprintf(stdout, "                  type: infilling toolpaths for tool %lld\n", sliceheader.ntool); break;
            default:                          fprintf(stdout, "                  type: unknown (%lld) for tool %lld\n", sliceheader.type, sliceheader.ntool);
            }
            fprintf(stdout, "                  z: %.20g\n", sliceheader.z);
            switch (sliceheader.saveFormat) {
            case PATHFORMAT_INT64:     fprintf(stdout, "     coordinate format: 64-bit integers\n"); break;
            case PATHFORMAT_DOUBLE:    fprintf(stdout, "     coordinate format: double floating point\n"); break;
            case PATHFORMAT_DOUBLE_3D: fprintf(stdout, "     coordinate format: double floating point (3D paths)\n"); break;
            default:                   fprintf(stdout, "     coordinate format: unknown (%lld)\n", sliceheader.saveFormat);
            }
            fprintf(stdout, "      %s scaling: %.20g\n", sliceheader.saveFormat == PATHFORMAT_INT64 ? "original" : "        ", sliceheader.scaling);
            
            if (sliceheader.alldata.size() > usual) {
                for (int k = usual; k < sliceheader.alldata.size(); ++k) {
                    fprintf(stdout, "      additional %d-th value:\n", k-usual);
                    fprintf(stdout, "            as  int64: %lld\n", sliceheader.alldata[k].i);
                    fprintf(stdout, "            as double: %g\n", sliceheader.alldata[k].d);
                }
            }
        } else {
            fprintf(stdout, "Record %d: ", currentRecord);
            switch (sliceheader.type) {
            case PATHTYPE_RAW_CONTOUR:        fprintf(stdout, "type=raw (from mesh file),        z=%.20g\n", sliceheader.z); break;
            case PATHTYPE_PROCESSED_CONTOUR:  fprintf(stdout, "type=contour,            ntool=%lld, z=%.20g\n", sliceheader.ntool, sliceheader.z); break;
            case PATHTYPE_TOOLPATH_PERIMETER: fprintf(stdout, "type=perimeter toolpath, ntool=%lld, z=%.20g\n", sliceheader.ntool, sliceheader.z); break;
            case PATHTYPE_TOOLPATH_SURFACE:   fprintf(stdout, "type=surface   toolpath, ntool=%lld, z=%.20g\n", sliceheader.ntool, sliceheader.z); break;
            case PATHTYPE_TOOLPATH_INFILLING: fprintf(stdout, "type=infilling toolpath, ntool=%lld, z=%.20g\n", sliceheader.ntool, sliceheader.z); break;
            default:                          fprintf(stdout, "type=%lld (unknown),        ntool=%lld, z=%.20g\n", sliceheader.type, sliceheader.ntool, sliceheader.z);
            }
        }

        if (verbose>0) {
            int numpaths;
            if (sliceheader.saveFormat == PATHFORMAT_INT64) {
                clp::Paths paths;
                if (!iop_f.readClipperPaths(paths)) {
                    return str("Error reading ", currentRecord, "-th integer clipperpaths: could not read record ", currentRecord, " data!");
                }
                numpaths = (int)paths.size();
                BBox bb = getBB(paths);
                fprintf(stdout, "          bounding box:\n");
                fprintf(stdout, "       X: min=%.20g, max=%.20g\n", bb.minx*sliceheader.scaling, bb.maxx*sliceheader.scaling);
                fprintf(stdout, "       Y: min=%.20g, max=%.20g\n", bb.miny*sliceheader.scaling, bb.maxy*sliceheader.scaling);
                if (verbose > 1) {
                    int ipath = 0;
                    for (auto &path : paths) {
                        fprintf(stdout, "          path %d/%lld:\n", ipath, paths.size());
                        int ipoint = 0;
                        for (auto &point : path) {
                            fprintf(stdout, "            point %d/%lld:\n", ipoint, path.size());
                            fprintf(stdout, "              X: %lld\n", point.X);
                            fprintf(stdout, "              Y: %lld\n", point.Y);
                            ++ipoint;
                        }
                        ++ipath;
                    }
                }
            } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE) {
                DPaths paths;
                if (!iop_f.readDoublePaths(paths)) {
                    return str("Error reading ", currentRecord, "-th double clipperpaths: could not read record ", currentRecord, " data!");
                }
                numpaths = (int)paths.size();
                double minx =  std::numeric_limits<double>::infinity();
                double maxx = -std::numeric_limits<double>::infinity();
                double miny =  std::numeric_limits<double>::infinity();
                double maxy = -std::numeric_limits<double>::infinity();
                for (auto &path : paths) {
                    for (auto &point : path) {
                        minx = fmin(minx, point.X);
                        maxx = fmax(maxx, point.X);
                        miny = fmin(miny, point.Y);
                        maxy = fmax(maxy, point.Y);
                    }
                }
                fprintf(stdout, "          bounding box:\n");
                fprintf(stdout, "       X: min=%.20g, max=%.20g\n", minx, maxx);
                fprintf(stdout, "       Y: min=%.20g, max=%.20g\n", miny, maxy);
                if (verbose > 1) {
                    int ipath = 0;
                    for (auto &path : paths) {
                        fprintf(stdout, "          path %d/%lld:\n", ipath, paths.size());
                        int ipoint = 0;
                        for (auto &point : path) {
                            fprintf(stdout, "            point %d/%lld:\n", ipoint, path.size());
                            fprintf(stdout, "              X: %.20g\n", point.X);
                            fprintf(stdout, "              Y: %.20g\n", point.Y);
                            ++ipoint;
                        }
                        ++ipath;
                    }
                }
            } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE_3D) {
                Paths3D paths;
                if (!read3DPaths(iop_f, paths)) {
                    return str("Error reading ", currentRecord, "-th 3d clipperpaths: could not read record ", currentRecord, " data!");
                }
                numpaths = (int)paths.size();
            }
            int payload   = (int)((sliceheader.totalSize - sliceheader.headerSize) / sizeof(int64));
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
            fseek(i.f, (long)(sliceheader.totalSize - sliceheader.headerSize), SEEK_CUR);
        }
    }

    return std::string();
}

const char *ERR =
"\nArguments: PATHSFILENAME_INPUT [verbose]\n\n"
"    -PATHSFILENAME_INPUT is the paths file name.\n\n"
"    -'v' or 'vv' if more information is to be printed ('v' for summary, 'vv' for full output).\n\n";

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
    
    int verbose = 0;
    if (rd.readParam(verbose_input, "VERBOSE_INPUT")) {
        if (strcmp(verbose_input, "v") == 0) {
            verbose = 1;
        } else if (strcmp(verbose_input, "vv") == 0) {
            verbose = 2;
        } else {
            fprintf(stderr, "if present, last argument must be either 'v' or 'vv', but it was <%s>\n", verbose_input);
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


