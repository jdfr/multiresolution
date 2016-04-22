#include "pathsfile.hpp"
#include "simpleparsing.hpp"
#include <iomanip>


void writePolygonSVG(FILE * f, clp::Path &path, bool isContour, bool insideIsBlack, double scalingFactor, double minx, double miny) {
    if (path.empty()) return;
    fprintf(f, R"(    <polygon slicer:type="%s" points=")", isContour ? "contour" : "hole");
    bool firstTime = true;
    auto pathend = path.end();
    if (path.front() == path.back()) --pathend;
    for (auto point = path.begin(); point != path.end(); ++point) {
        fprintf(f, "%s%.20g,%.20g", firstTime ? "" : " ", point->X*scalingFactor - minx, point->Y*scalingFactor - miny);
        firstTime = false;
    }
    fprintf(f, R"(" style="fill:%s" />)" "\n", insideIsBlack ? "black" : "white");
}

void writePolygonSVG(FILE * f, clp::Paths &paths, bool isContour, bool insideIsBlack, double scalingFactor, double minx, double miny) {
    for (auto path = paths.begin(); path != paths.end(); ++path) {
        writePolygonSVG(f, *path, isContour, insideIsBlack, scalingFactor, minx, miny);
    }
}

void writePolygonSVG(FILE * f, HoledPolygon &hp, bool insideIsBlack, double scalingFactor, double minx, double miny) {
    writePolygonSVG(f, hp.contour, true,   insideIsBlack, scalingFactor, minx, miny);
    writePolygonSVG(f, hp.holes,   false, !insideIsBlack, scalingFactor, minx, miny);
}

void writePolygonSVG(FILE * f, HoledPolygons &hps, bool insideIsBlack, double scalingFactor, double minx, double miny) {
    for (auto hp = hps.begin(); hp != hps.end(); ++hp) {
        writePolygonSVG(f, *hp, insideIsBlack, scalingFactor, minx, miny);
    }
}

void writeSVG(const char * filename, HoledPolygons &hps, bool insideIsBlack, double scalingFactor, const char * units) {
    clp::DoublePoint size, polOffset, viewBox1, viewBox2, viewBoxSize;
    double viewBoxExtra;
    viewBoxExtra    = 0.1;
    BBox bb         = getBB(hps);
    size.X          = (bb.maxx - bb.minx) * scalingFactor;
    size.Y          = (bb.maxy - bb.miny) * scalingFactor;
    viewBox1.X      = -size.X *  viewBoxExtra;
    viewBox1.Y      = -size.Y *  viewBoxExtra;
    viewBox2.X      =  size.X * (viewBoxExtra+1);
    viewBox2.Y      =  size.Y * (viewBoxExtra+1);
    viewBoxSize.X   =  viewBox2.X - viewBox1.X;
    viewBoxSize.Y   =  viewBox2.Y - viewBox1.Y;
    polOffset.X     = bb.minx * scalingFactor + viewBox1.X;
    polOffset.Y     = bb.miny * scalingFactor + viewBox1.Y;

    FILE * f = fopen(filename, "wt");
    if (f == NULL) {
        fprintf(stderr, "Could not open output file %s\n", filename);
    }
    fprintf(f,
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)" "\n"
        R"(<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.0//EN" "http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd">)" "\n"
        R"(<svg width="%.20g%s" height="%.20g%s" viewBox="%g %g %.20g %.20g" xmlns:svg="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:slicer="http://slicer.org/namespaces/slicer">)" "\n"
        R"(  <g id="only_one_slice">)" "\n",
        viewBoxSize.X, units, viewBoxSize.Y, units, 0.0, 0.0, viewBoxSize.X, viewBoxSize.Y);
    if (!insideIsBlack) {
        fprintf(f, R"(    <rect width="%.20g" height="%.20g" style="fill:black"/>)" "\n", viewBoxSize.X, viewBoxSize.Y);
    }
    writePolygonSVG(f, hps, insideIsBlack, scalingFactor, polOffset.X, polOffset.Y);
    fputs("  </g>\n</svg>", f);
    fclose(f);
}

//this is a failsafe to avoid compiler errors, but users should set a default arena chunk size accordingly to the expected usage patterns
#ifndef INITIAL_ARENA_SIZE
#  define INITIAL_ARENA_SIZE (50*1024*1024)
#endif

template<typename CM = CLIPPER_MMANAGER> typename std::enable_if< CLIPPER_MMANAGER::isArena, CM>::type getManager() { return CLIPPER_MMANAGER(INITIAL_ARENA_SIZE); }
template<typename CM = CLIPPER_MMANAGER> typename std::enable_if<!CLIPPER_MMANAGER::isArena, CM>::type getManager() { return CLIPPER_MMANAGER(); }

std::string processMatches(const char * filename, const char * svgfilename, PathInFileSpec spec, bool matchFirst, bool insideIsBlack) {
    FILE * f = fopen(filename, "rb");
    if (f == NULL) { return str("Could not open file ", filename); }

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", filename, ": ", err); }

    SliceHeader sliceheader;
    int index = 0;
    IOPaths iop(f);
    clp::Paths output;
    CLIPPER_MMANAGER manager = getManager();
    clp::Clipper clipper(manager);
    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string e = seekNextMatchingPathsFromFile(f, fileheader, currentRecord, spec, sliceheader);
        if (!e.empty()) { err = str("Error reading file ", filename, ": ", e); break; }
        if (currentRecord >= fileheader.numRecords) break;

        if (sliceheader.saveFormat == PATHFORMAT_INT64) {
            if (!iop.readClipperPaths(output)) {
                err = str("error reading integer paths from record ", currentRecord, " in file ", filename, ": error <", iop.errs[0].message, "> in ", iop.errs[0].function);
                break;
            }
        } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE) {
            //TODO: modify the code so we do not have to convert back and forth the coordinates in this case!
            if (!iop.readDoublePaths(output, 1 / sliceheader.scaling)) {
                err = str("error reading double paths from record ", currentRecord, " in file ", filename, ": error <", iop.errs[0].message, "> in ", iop.errs[0].function);
                break;
            }
        } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE_3D) {
            err = str("In file ", filename, ", for path ", currentRecord, ", save mode is 3D, but we cannot save 3D paths to SVG\n");
            break;
        } else {
            err = str("In file ", filename, ", for path ", currentRecord, ", save mode not understood: ", sliceheader.saveFormat, "\n");
            break;
        }
        for (int n = 0; n < output.size(); ++n) {
            if (output[n].front() != output[n].back()) {
                err = str("In file ", filename, ", pathset ", currentRecord, " matches specification, but path ", n, "-th inside it is not closed!!!");
                break;
            }
        }
        HoledPolygons hps;
        AddPathsToHPs(clipper, output, hps);
        output.clear();

        std::string svgname;
        if (matchFirst) {
            svgname = str(svgfilename, ".svg");
        } else {
            svgname = str(svgfilename, '.', std::setfill('0'), std::setw(3), index++, ".svg");
        }

        writeSVG(svgname.c_str(), hps, insideIsBlack, sliceheader.scaling, "mm");

        if (matchFirst) {
            break;
        }
    }
    fclose(f);

    return err;
}

const char *ERR =
"\nArguments: PATHSFILENAME SVGFILENAME (black | white) (first | all) [SPECTYPE VALUE]*\n\n"
"    -PATHSFILENAME is required (input paths file name).\n\n"
"    -SVGFILENAME is required (output svg file name).\n\n"
"    -the color inside the contours can be either 'black' or 'white'.\n\n"
"    -if 'first' is specified, only the first eligible match is converted to SVG. If 'all' is specified, all eligible paths are converted, creating one SVG file for each one.\n\n"
"    -Multiple pairs SPECTYPE VALUE can be specified. SPECTYPE can be either 'type', 'ntool', or 'z'. For the first, VALUE can be either r[aw], c[ontour], p[erimeter] (perimeter toolpath type), i[nfilling] (infilling toolpath type) or t[oolpath] (any toolpath type). For the second, it is an integer, for the latter, a floating-point value. If several pairs have the same SPECTYPE, the latter overwrites the former. If nothing is specified, all paths are eligible.\n\n"
"This tool writes as a SVG file the first record in PATHSFILENAME that matches the specification.\n\n";

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

    const char *pathsfilename, *svgfilename, *matchMode, *colorMode;
    bool matchFirst, insideIsBlack;
    
    if (!rd.readParam(pathsfilename,   "PATHSFILENAME"))             { printError(rd); return -1; }
    if (!rd.readParam(svgfilename,     "SVGFILENAME"))               { printError(rd); return -1; }
    if (!rd.readParam(colorMode,       "color (black/white)"))       { printError(rd); return -1; }
    if (!rd.readParam(matchMode,       "matching mode (first/all)")) { printError(rd); return -1; }

    if (strcmp(colorMode, "black") == 0) {
        insideIsBlack = true;
    } else if (strcmp(colorMode, "white") == 0) {
        insideIsBlack = false;
    } else {
        fprintf(stderr, "the color should be 'black' or 'white', but it is: %s\n", colorMode);
        return -1;
    }

    if (strcmp(matchMode, "first") == 0) {
        matchFirst = true;
    } else if (strcmp(matchMode, "all") == 0) {
        matchFirst = false;
    } else {
        fprintf(stderr, "matching mode should be 'first' or 'all', but it is: %s\n", matchMode);
        return -1;
    }

    if (!fileExists(pathsfilename)) { fprintf(stderr, "the input file was not found: %s!!!", pathsfilename); return -1; }

    PathInFileSpec spec;
    std::string err = spec.readFromCommandLine(rd, -1, false);
    if (!err.empty()) {
        fprintf(stderr, "Error while trying to read the specification: %s", err.c_str());
        return -1;
    }

    clp::Paths paths;
    err = processMatches(pathsfilename, svgfilename, spec, matchFirst, insideIsBlack);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to get a set of paths according to the specification: %s", err.c_str());
        return -1;
    }

    return 0;
}


