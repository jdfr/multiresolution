#include "multislicer.hpp"
#include "pathwriter.hpp"
#include "simpleparsing.hpp"
#include <iomanip>

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

void createCubesFromGrid(const char *cubeNameTemplate, PathSplitterConfigs &saveInGridConf, std::shared_ptr<ClippingResources> res, double scaling, double zmin, double zmax) {
    auto numconfs = saveInGridConf.size();
    bool justone  = numconfs==1;
    std::string prefixA;
    if (justone) prefixA = cubeNameTemplate;
    int ntool = 0;
    std::vector<std::string> prefixes;
    for (auto &conf : saveInGridConf) {
        PathSplitter splitter(std::move(conf), res);
        splitter.setup();
        if (!justone) prefixA = str(cubeNameTemplate, ".N", ntool);
        auto cubes = splitter.generateGridCubes(scaling, zmin, zmax);
        int num0x = cubes.numx < 10 ? 1 : (int)std::ceil(std::log10(cubes.numx-1));
        int num0y = cubes.numy < 10 ? 1 : (int)std::ceil(std::log10(cubes.numy-1));
        for (int i=0; i<cubes.numx; ++i) {
            for (int j=0; j<cubes.numy; ++j) {
                std::string prefixB = str(prefixA, '.', std::setw(num0x), std::setfill('0'), i, '.', std::setw(num0y), std::setfill('0'), j);
                std::string name = str(prefixB, ".off");
                FILE *f = fopen(name.c_str(), "wt");
                writeTriangleMeshToOFF(f, "%03.017f", cubes.at(i, j));
                fclose(f);
                prefixes.push_back(std::move(prefixB));
            }
        }
        ++ntool;
    }
    fprintf(stdout, "All %ld cubic meshes have been generated. Now, to partition a STL model with them, you can use the following commands (you will need meshlab and an appropriate version of cork):\n\n", prefixes.size());
    fprintf(stdout, "meshlabserver -i mesh.stl -o mesh.off\n");
    for (auto &prefix : prefixes) {
      fprintf(stdout, "cork -isct mesh.off \"%s.off\" \"%s.output.off\"\n", prefix.c_str(), prefix.c_str());
    }
    fprintf(stdout, "\n");
    for (auto &prefix : prefixes) {
      fprintf(stdout, "meshlabserver -i \"%s.output.off\" \"%s.output.stl\"\n", prefix.c_str(), prefix.c_str());
    }
    fprintf(stdout, "\n\nAnd that's it.\n");
}



const char *ERR =
"\nThis tool has two operation modes: in the operation mode SPLIT, it divides contours and toolpaths into a grid of squares, storing the contents of each square in a different output file. In the operation mode CUBES, it takes a bounding box and generates cubes (written as OFF files) that represent the grid as specified in the SPLIT mode.\n\n"
"Arguments: (cubes MINX MAXX MINY MAXY MINZ MAXZ| split PATHFILENAME) OUTPUTNAMEPATTERN DISPLACEMENT MARGIN [ORIGIN_X ORIGIN_Y]\n\n"
"    -First argument: mode, either 'cubes' or 'split'.\n\n"
"    -MINX MAXX MINY MAXY MINZ MAXZ: if the mode is 'cubes', these arguments define the bounding box\n\n"
"    -PATHFILENAME: if the mode is 'split', this is the name of the *.paths input file\n\n"
"    -OUTPUTNAMEPATTERN: the root of the name (minus the extension) of the output files\n\n"
"    -DISPLACEMENT and MARGIN: the input paths will be divided in squares of size DISPLACEMENT+MARGIN. The squares will overlap by MARGIN.\n\n"
"    -ORIGIN_X and ORIGIN_Y: if specified, they represent the center of coordinates of the grid of squares. If not specified, the squares will be adapted to the bounding box of the paths, and may be smaller than DISPLACEMENT+MARGIN.\n\n"
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

    const char *mode, *pathsfilename, *outputPattern;
    double displacement, margin, origin_x, origin_y, minx, maxx, miny, maxy, minz, maxz;
    bool use_origin;

    if (!rd.readParam(mode,          "mode"))                                                 { printError(rd); return -1; }
    bool modeSplit = strcmp(mode, "split")==0;
    bool modeCubes = strcmp(mode, "cubes")==0;
    if (!modeSplit && !modeCubes) {
        printf("Error: argument mode must be either 'split' or 'cubes', but it was '%s'", mode);
        return -1;
    }
    if (modeSplit) {
        if (!rd.readParam(pathsfilename, "input *.paths file name"))                          { printError(rd); return -1; }
    } else if (modeCubes) {
        if (!rd.readParam(minx,          "MINX (in mesh file units)"))                        { printError(rd); return -1; }
        if (!rd.readParam(maxx,          "MAXX (in mesh file units)"))                        { printError(rd); return -1; }
        if (!rd.readParam(miny,          "MINY (in mesh file units)"))                        { printError(rd); return -1; }
        if (!rd.readParam(maxy,          "MAXY (in mesh file units)"))                        { printError(rd); return -1; }
        if (!rd.readParam(minz,          "MINZ (in mesh file units)"))                        { printError(rd); return -1; }
        if (!rd.readParam(maxz,          "MAXZ (in mesh file units)"))                        { printError(rd); return -1; }
    }
    if (!rd.readParam(outputPattern, "pattern for output files (without the suffix .paths)")) { printError(rd); return -1; }
    if (!rd.readParam(displacement,  "DISPLACEMENT (in mesh file units)"))                    { printError(rd); return -1; }
    if (!rd.readParam(margin, "MARGIN (in mesh file units)"))                                 { printError(rd); return -1; }
    use_origin = rd.readParam(origin_x, "ORIGIN_X (in mesh file units)");
    if (use_origin) {
        if (!rd.readParam(origin_y, "ORIGIN_Y (in mesh file units)")) { printError(rd); printf(" (If ORIGIN_X is present, ORIGIN_Y must also be present)\n"); return -1; }
    }

    PathSplitterConfigs conf(1);
    conf[0].wallAngle      = 90; //no need to set conf[0].zmin
    conf[0].useOrigin      = use_origin;

    std::shared_ptr<ClippingResources> clipres = std::make_shared<ClippingResources>(std::shared_ptr<MultiSpec>());
    
    if (modeSplit) {
        std::shared_ptr<FileHeader> header = std::make_shared<FileHeader>();
        int numtools, numrecords;
        double scaling;
        BBox bb;

        std::string err = firstPass(pathsfilename, header, numrecords, scaling, bb);
        if (!err.empty()) { fprintf(stderr, err.c_str()); return -1; }

        conf[0].displacement.X = conf[0].displacement.Y = (clp::cInt)(displacement / scaling);
        conf[0].margin         = (clp::cInt)(margin / scaling);
        if (use_origin) {
            conf[0].origin.X   = (clp::cInt)(origin_x / scaling);
            conf[0].origin.Y   = (clp::cInt)(origin_y / scaling);
        }
        conf[0].min.X   = bb.minx;
        conf[0].min.Y   = bb.miny;
        conf[0].max.X   = bb.maxx;
        conf[0].max.Y   = bb.maxy;
        
        numtools = (int)header->numtools;
        const bool generic_type  = true;
        const bool generic_ntool = true;
        const bool generic_z     = true;
        SplittingSubPathWriterCreator callback = [header](int idx, PathSplitter& splitter, std::string &fname, std::string suffix, bool generic_type, bool generic_ntool, bool generic_z) {
            return std::make_shared<PathsFileWriter>(fname+suffix, (FILE*)NULL, header, PATHFORMAT_INT64);
        };
        SplittingPathWriter writer(clipres, numtools, callback, std::move(conf), outputPattern, generic_type, generic_ntool, generic_z);

        err = processFile(pathsfilename, writer);
        if (!err.empty()) { fprintf(stderr, err.c_str()); return -1; }
    } else if (modeCubes) {
        double scaling = 1e-6;
        conf[0].displacement.X = conf[0].displacement.Y = (clp::cInt)(displacement / scaling);
        conf[0].margin         = (clp::cInt)(margin / scaling);
        if (use_origin) {
            conf[0].origin.X   = (clp::cInt)(origin_x / scaling);
            conf[0].origin.Y   = (clp::cInt)(origin_y / scaling);
        }
        conf[0].min.X   = minx / scaling;
        conf[0].min.Y   = miny / scaling;
        conf[0].max.X   = maxx / scaling;
        conf[0].max.Y   = maxy / scaling;
        createCubesFromGrid(outputPattern, conf, clipres, scaling, minz, maxz);
    }

    return 0;
}

