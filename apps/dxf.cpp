#include "pathwriter.hpp"
#include "simpleparsing.hpp"
#include <iomanip>

std::string processFile(const char *pathsfilename, const char *dxffilename, bool toolpaths, bool ascii, bool byz, bool byn) {
    FILE * f = fopen(pathsfilename, "rb");
    if (f == NULL) { return str("Could not open file ", pathsfilename); }

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", pathsfilename, ": ", err); }

    SliceHeader sliceheader;
    int index = 0;
    IOPaths iop(f);
    clp::Paths output;
    PathWriter *writer;
    if (ascii) {
        writer = new DXFAsciiPathWriter (dxffilename, 1e-9, true, !byn, !byz);
    } else {
        writer = new DXFBinaryPathWriter(dxffilename, 1e-9, true, !byn, !byz);
    }
    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string err = sliceheader.readFromFile(f);
        if (!err.empty()) { return str("Error reading ", currentRecord, "-th slice header: ", err); }

        bool doProcess = ( toolpaths && (sliceheader.type == PATHTYPE_TOOLPATH)) ||
                         (!toolpaths && (sliceheader.type == PATHTYPE_PROCESSED_CONTOUR));
        if (!doProcess) {
            fseek(f, (long)(sliceheader.totalSize - sliceheader.headerSize), SEEK_CUR);
            continue;
        }

        if (sliceheader.saveFormat == PATHFORMAT_INT64) {
            if (!iop.readClipperPaths(output)) {
                err = str("error reading integer paths from record ", currentRecord, " in file ", pathsfilename, ": error <", iop.errs[0].message, "> in ", iop.errs[0].function);
                break;
            }
        } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE) {
            //TODO: modify the code so we do not have to convert back and forth the coordinates in this case!
            if (!iop.readDoublePaths(output, 1 / sliceheader.scaling)) {
                err = str("error reading double paths from record ", currentRecord, " in file ", pathsfilename, ": error <", iop.errs[0].message, "> in ", iop.errs[0].function);
                break;
            }
        } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE_3D) {
            err = str("In file ", pathsfilename, ", for path ", currentRecord, ", save mode is 3D, but we currently cannot convert 3D paths to DXF\n");
            break;
        } else {
            err = str("In file ", pathsfilename, ", for path ", currentRecord, ", save mode not understood: ", sliceheader.saveFormat, "\n");
            break;
        }
        bool isclosed = !toolpaths;
        double radius = fileheader.radiusX[sliceheader.ntool];
        if (!writer->writePaths(output, (int)sliceheader.type, radius, (int)sliceheader.ntool, sliceheader.z, sliceheader.scaling, isclosed)) {
            err = writer->err;
            break;
        }
        output.clear();
    }
    fclose(f);
    writer->close();
    delete writer;

    return err;
}

const char *ERR =
"\nArguments: (toolpaths|contours) (ascii|binary) (all|byz|byn|byzn) PATHSFILENAME DXFFILENAME\n\n"
"    -First argument: the output DXF will contain either toolpaths or contours\n\n"
"    -Second argument: the output DXF will be either in ASCII or binary format\n\n"
"    -if 'all' is specified, a single DXF file will be generated, containing all toolpaths or contours for all tools and Z values. Each polyline will have an elevation corresponding to its Z value, and a width corresponding to its tool.\n\n"
"    -if 'byz' is specified, a DXf file will be generated for each Z value, containing all toolpaths or contours for all tools. Each polyline will have a width corresponding to its tool.\n\n"
"    -if 'byn' is specified, a DXf file will be generated for each tool, containing all toolpaths or contours for all Z values. Each polyline will have an elevation corresponding to its Z value.\n\n"
"    -if 'byzn' is specified, a DXf file will be generated for each combination of tool and Z value.\n\n"
"    -PATHSFILENAME is the input file name (the file must be in pathsfile format). Extension should be included.\n\n"
"    -DXFFILENAME is the output file name. Extension DXF should not be included.\n\n";

void printError(ParamReader &rd) {
    rd.fmt << ERR;
    std::string err = rd.fmt.str();
    fprintf(stderr, err.c_str());
}

int main(int argc, const char** argv) {
    ParamReader rd = ParamReader::getParamReaderWithOptionalResponseFile(argc, argv);

    if (!rd.err.empty()) {
        fprintf(stderr, "ParamReader error: %s\n", rd.err.c_str());
        return -1;
    }

    const char *whatToOutput, *outputMode, *outputFormat, *pathsfilename, *dxffilename;
    bool toolpaths, ascii, byz, byn;
    
    if (!rd.readParam(whatToOutput,    "otuput (toolpaths/contours"))  { printError(rd); return -1; }
    if (!rd.readParam(outputFormat,    "format (ascii/binary"))        { printError(rd); return -1; }
    if (!rd.readParam(outputMode,      "mode (all/byz/byn/byzn"))      { printError(rd); return -1; }
    if (!rd.readParam(pathsfilename,   "PATHSFILENAME"))               { printError(rd); return -1; }
    if (!rd.readParam(dxffilename,     "SVGFILENAME"))                 { printError(rd); return -1; }

    if (strcmp(whatToOutput, "toolpaths") == 0) {
        toolpaths = true;
    } else if (strcmp(whatToOutput, "contours") == 0) {
        toolpaths = false;
    } else {
        fprintf(stderr, "the output should be 'toolpaths' or 'contours', but it is: %s\n", whatToOutput);
        return -1;
    }

    if (strcmp(outputFormat, "ascii") == 0) {
        ascii = true;
    } else if (strcmp(outputFormat, "binary") == 0) {
        ascii = false;
    } else {
        fprintf(stderr, "the format should be 'ascii' or 'binary', but it is: %s\n", outputFormat);
        return -1;
    }

    if (strcmp(outputMode, "all") == 0) {
        byz = byn = false;
    } else if (strcmp(outputMode, "byzn") == 0) {
        byz = byn = true;
    } else if (strcmp(outputMode, "byz") == 0) {
        byz = true;
        byn = false;
    } else if (strcmp(outputMode, "byn") == 0) {
        byn = true;
        byz = false;
    } else {
        fprintf(stderr, "the mode should be either 'all', 'byz', 'byn', or 'byzn': %s\n", outputMode);
        return -1;
    }

    if (!fileExists(pathsfilename)) { fprintf(stderr, "the input file was not found: %s!!!", pathsfilename); return -1; }

    clp::Paths paths;
    std::string err = processFile(pathsfilename, dxffilename, toolpaths, ascii, byz, byn);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to compute DXF output: %s", err.c_str());
        return -1;
    }

    return 0;
}


