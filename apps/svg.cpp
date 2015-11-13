//this is a simple command line application that organizes the execution of the multislicer

#include "app.hpp"
#include "config.hpp"
#include "spec.hpp"
#include "auxgeom.hpp"
#include <stdio.h>
#include <ctype.h>
#include <sstream>
#include <string>


void writePolygonSVG(FILE * f, clp::Path &path, bool isContour, double scalingFactor, double minx, double miny) {
    if (path.empty()) return;
    fprintf(f, R"(    <polygon slicer:type="%s" points=")", isContour ? "contour" : "hole");
    bool firstTime = true;
    auto pathend = path.end();
    if (path.front() == path.back()) --pathend;
    for (auto point = path.begin(); point != path.end(); ++point) {
        fprintf(f, "%s%f,%f", firstTime ? "" : " ", point->X*scalingFactor - minx, point->Y*scalingFactor - miny);
        firstTime = false;
    }
    fprintf(f, R"(" style="fill:%s" />)" "\n", isContour ? "black" : "white");
}

void writePolygonSVG(FILE * f, clp::Paths &paths, bool isContour, double scalingFactor, double minx, double miny) {
    for (auto path = paths.begin(); path != paths.end(); ++path) {
        writePolygonSVG(f, *path, isContour, scalingFactor, minx, miny);
    }
}

void writePolygonSVG(FILE * f, HoledPolygon &hp, double scalingFactor, double minx, double miny) {
    writePolygonSVG(f, hp.contour, true, scalingFactor, minx, miny);
    writePolygonSVG(f, hp.holes,  false, scalingFactor, minx, miny);
}

void writePolygonSVG(FILE * f, HoledPolygons &hps, double scalingFactor, double minx, double miny) {
    for (auto hp = hps.begin(); hp != hps.end(); ++hp) {
        writePolygonSVG(f, *hp, scalingFactor, minx, miny);
    }
}

void writeSVG(const char * filename, HoledPolygons &hps, double scalingFactor, const char * units) {
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
        R"(<svg width="%f%s" height="%f%s" viewBox="%f %f %f %f" xmlns:svg="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:slicer="http://slicer.org/namespaces/slicer">)" "\n"
        R"(  <g id="only_one_slice">)" "\n",
        viewBoxSize.X, units, viewBoxSize.Y, units, 0.0, 0.0, viewBoxSize.X, viewBoxSize.Y);
    writePolygonSVG(f, hps, scalingFactor, polOffset.X, polOffset.Y);
    fputs("  </g>\n</svg>", f);
    fclose(f);
}

std::string getFirstMatchFromFile(const char * filename, PathInFileSpec spec, clp::Paths &output, double &scaling) {
    FILE * f = fopen(filename, "rb");
    if (f == NULL) { return str("Could not open file ", filename); }

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", filename, ": ", err); }

    SliceHeader sliceheader;
    int currentRecord = 0;
    err = seekNextMatchingPathsFromFile(f, fileheader, currentRecord, spec, sliceheader);
    if (!err.empty()) { fclose(f); return str("Error reading file ", filename, ": ", err); }
    if (currentRecord >= fileheader.numRecords) { fclose(f); return std::string("Could not match the specification to file contents"); }

    scaling = sliceheader.scaling;
    if (sliceheader.saveFormat == SAVEMODE_INT64) {
        readClipperPaths(f, output);
        err = std::string();
        for (int n = 0; n < output.size(); ++n) {
            if (output[n].front() != output[n].back()) {
                err = str("In file ", filename, ", pathset ", currentRecord, " matches specification, but path ", n, "-th inside it is not closed!!!");
                break;
            }
        }
    } else if (sliceheader.saveFormat == SAVEMODE_DOUBLE) {
        err = str("In file ", filename, ", pathset ", currentRecord, " matches specification, but it was saved in DOUBLE format (if you know what you are doing, you can convert it back to INT64 format)");
        //fprintf(stderr, "WARNING: in file %s, path %d matches specification, but it was saved in DOUBLE format. This may cause (hard to debug) errors in some cases because of the required conversion to INT64\n", filename, currentRecord);
        //readDoublePaths(f, output, 1/sliceheader.scaling);
        //err = std::string();
    } else {
        err = str("In file ", filename, ", for path ", currentRecord, ", save mode not understood: ", sliceheader.saveFormat, "\n");
    }
    fclose(f);
    return err;
}

const char *ERR =
"\nArguments: PATHSFILENAME SVGFILENAME [SPECTYPE VALUE]*\n\n"
"    -PATHSFILENAME is required (input paths file name).\n\n"
"    -SVGFILENAME is required (output svg file name).\n\n"
"    -Multiple pairs SPECTYPE VALUE can be specified. SPECTYPE can be either 'type', 'ntool', or 'z'. For the first, VALUE can be either r[aw], p[rocessed] or t[oolpath], for the second, it is an integer, for the latter, a floating-point value. If several pairs have the same SPECTYPE, the latter overwrites the former.\n\n"
"This tool writes as a SVG file the first record in PATHSFILENAME that matches the specification.\n\n";

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

    const char *pathsfilename, *svgfilename;
    
    if (!rd.readParam(pathsfilename,   "PATHSFILENAME"))          { printError(rd); return -1; }
    if (!rd.readParam(svgfilename,     "SVGFILENAME"))            { printError(rd); return -1; }

    if (!fileExists( pathsfilename)) { fprintf(stderr,  "the input file was not found: %s!!!", pathsfilename ); return -1; }

    PathInFileSpec spec;
    std::string err = spec.readFromCommandLine(rd);
    if (!err.empty()) {
        fprintf(stderr, "Error while trying to read the specification: %s", err.c_str());
        return -1;
    }

    clp::Paths paths;
    double scaling;
    err = getFirstMatchFromFile(pathsfilename, spec, paths, scaling);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to get a set of paths according to the specification: %s", err.c_str());
        return -1;
    }

    HoledPolygons hps;
    AddPathsToHPs(paths, hps);

    writeSVG(svgfilename, hps, scaling, "mm");

    return 0;
}


