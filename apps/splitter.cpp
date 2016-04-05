#include "pathwriter.hpp"
#include "simpleparsing.hpp"

bool getPaths(const char *pathsfilename, int currentRecord, SliceHeader &sliceheader, IOPaths &iop, clp::Paths &output, std::string &err) {
    if (sliceheader.saveFormat == PATHFORMAT_INT64) {
        if (!iop.readClipperPaths(output)) {
            err = str("error reading integer paths from record ", currentRecord, " in file ", pathsfilename, ": error <", iop.errs[0].message, "> in ", iop.errs[0].function);
            return false;
        }
    } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE) {
        if (!iop.readDoublePaths(output, 1 / sliceheader.scaling)) {
            err = str("error reading double paths from record ", currentRecord, " in file ", pathsfilename, ": error <", iop.errs[0].message, "> in ", iop.errs[0].function);
            return false;
        }
    } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE_3D) {
        err = str("In file ", pathsfilename, ", for path ", currentRecord, ", save mode is 3D, but we currently cannot convert 3D paths to DXF\n");
        return false;
    } else {
        err = str("In file ", pathsfilename, ", for path ", currentRecord, ", save mode not understood: ", sliceheader.saveFormat, "\n");
        return false;
    }
    return true;
}

std::string firstPass(const char *pathsfilename, std::shared_ptr<FileHeader> &fileheader, int &numRecords, double &scaling, BBox &bb) {
    FILE * f = fopen(pathsfilename, "rb");
    if (f == NULL) { return str("Could not open file ", pathsfilename); }

    std::string err = fileheader->readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", pathsfilename, ": ", err); }

    SliceHeader sliceheader;
    IOPaths iop(f);
    clp::Paths output;
    numRecords = (int)fileheader->numRecords;

    bool firstTime = true;

    if (numRecords <= 0) { fclose(f); return str("Nothing was done: the file has ", numRecords, "records!"); }

    for (int currentRecord = 0; currentRecord < numRecords; ++currentRecord) {
        std::string err = sliceheader.readFromFile(f);
        if (!err.empty()) { return str("Error reading ", currentRecord, "-th slice header: ", err); }

        if (!getPaths(pathsfilename, currentRecord, sliceheader, iop, output, err)) {
            break;
        }

        if (firstTime) {
            scaling = sliceheader.scaling;
            firstTime = false;
            bb = getBB(output);
        } else {
            if (std::abs((scaling - sliceheader.scaling) / scaling) > 1e-3) {
                fclose(f); return str("Error: the records inside file ", pathsfilename, ", have different scales: ", scaling, ", for record ", currentRecord - 1, ", ", sliceheader.scaling, " for record ", currentRecord);
            }
            BBox second = getBB(output);
            bb.merge(second);
        }

        output.clear();
    }

    fclose(f);
    return std::string();
}


std::string processFile(const char *pathsfilename, SplittingPathWriter &writer) {
    FILE * f = fopen(pathsfilename, "rb");
    if (f == NULL) { return str("Could not open file ", pathsfilename); }

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", pathsfilename, ": ", err); }

    SliceHeader sliceheader;
    IOPaths iop(f);
    clp::Paths output;

    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string err = sliceheader.readFromFile(f);
        if (!err.empty()) { return str("Error reading ", currentRecord, "-th slice header: ", err); }

        if (!getPaths(pathsfilename, currentRecord, sliceheader, iop, output, err)) {
            break;
        }

        bool isClosed = !((sliceheader.type == PATHTYPE_TOOLPATH_PERIMETER) || (sliceheader.type == PATHTYPE_TOOLPATH_INFILLING));
        writer.writePaths(output, (int)sliceheader.type, 0, (int)sliceheader.ntool, sliceheader.z, sliceheader.scaling, isClosed);

        output.clear();
    }

    fclose(f);
    return std::string();
}


const char *ERR =
"\nThis tool divides contours and toolpaths into a grid of suqares, storing the contents of each square in a different output file.\n\n"
"Arguments: PATHFILENAME OUTPUTNAMEPATTERN DISPLACEMENT MARGIN [ORIGIN_X ORIGIN_Y]\n\n"
"    -First argument: in name of the *.paths input file\n\n"
"    -Second argument: the root of the name (minus the extension) of the output files\n\n"
"    -third and fourth arguments: the input paths will be divided in squares of size DISPLACEMENT+MARGIN. The squares will overlap by MARGIN.\n\n"
"    -fifth and sixth arguments: if specified, they represent the center of coordinates of the grid of squares. If not specified, the squares will be adapted to the bounding box of the paths, and may be smaller than DISPLACEMENT+MARGIN.\n\n"
"All metric parameters are in the units of the original mesh from which the paths were sliced (usually, millimeters).\n\n";


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

    const char *pathsfilename, *outputPattern;
    double displacement, margin, origin_x, origin_y;
    bool use_origin;

    if (!rd.readParam(pathsfilename, "input *.paths file name"))                              { printError(rd); return -1; }
    if (!rd.readParam(outputPattern, "pattern for output files (without the suffix .paths)")) { printError(rd); return -1; }
    if (!rd.readParam(displacement,  "DISPLACEMENT (in mesh file units)"))                    { printError(rd); return -1; }
    if (!rd.readParam(margin, "MARGIN (in mesh file units)"))                                 { printError(rd); return -1; }
    use_origin = rd.readParam(origin_x, "ORIGIN_X (in mesh file units)");
    if (use_origin) {
        if (!rd.readParam(origin_y, "ORIGIN_Y (in mesh file units)")) { printError(rd); printf(" (If ORIGIN_X is present, ORIGIN_Y must also be present)\n"); return -1; }
    }

    std::shared_ptr<FileHeader> header = std::make_shared<FileHeader>();
    int numtools, numrecords;
    double scaling;
    BBox bb;

    std::string err = firstPass(pathsfilename, header, numrecords, scaling, bb);
    if (!err.empty()) { fprintf(stderr, err.c_str()); return -1; }

    numtools = (int)header->numtools;

    PathSplitterConfigs conf(1);
    conf[0].wallAngle      = 90; //no need to set conf[0].zmin
    conf[0].displacement.X = conf[0].displacement.Y = (clp::cInt)(displacement / scaling);
    conf[0].margin         = (clp::cInt)(margin / scaling);
    conf[0].useOrigin      = use_origin;
    conf[0].min.X          = bb.minx;
    conf[0].min.Y          = bb.miny;
    conf[0].max.X          = bb.maxx;
    conf[0].max.Y          = bb.maxy;
    if (use_origin) {
        conf[0].origin.X   = (clp::cInt)(origin_x / scaling);
        conf[0].origin.Y   = (clp::cInt)(origin_y / scaling);
    }

    const bool generic_type  = true;
    const bool generic_ntool = true;
    const bool generic_z     = true;
    SplittingSubPathWriterCreator callback = [header](int idx, PathSplitter& splitter, std::string &fname, std::string suffix, bool generic_type, bool generic_ntool, bool generic_z) {
        return std::make_shared<PathsFileWriter>(fname+suffix, (FILE*)NULL, header, PATHFORMAT_INT64);
    };
    SplittingPathWriter writer(numtools, callback, std::move(conf), outputPattern, generic_type, generic_ntool, generic_z);

    err = processFile(pathsfilename, writer);
    if (!err.empty()) { fprintf(stderr, err.c_str()); return -1; }

    return 0;
}

