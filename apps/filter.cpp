//this is a simple command line application that organizes the execution of the multislicer

//if macro STANDALONE_USEPYTHON is defined, SHOWCONTOUR support is baked in
#define STANDALONE_USEPYTHON

#include "app.hpp"
#include "config.hpp"
#include "spec.hpp"
#include "auxgeom.hpp"
#include <stdio.h>
#include <sstream>
#include <string>

typedef struct Data {
    int k;
    SliceHeader header;
    std::vector<int64> data;
    Data(int _k, SliceHeader _header, int datasize) : k(_k), header(std::move(_header)), data(datasize) {}
} Data;

std::string filterMatchesFromFile(const char * filename, const char *outputname, PathInFileSpec spec) {
    FILE * f = fopen(filename, "rb");
    if (f == NULL) { return str("Could not open file ", filename); }


    FileHeader fileheader;
    std::string err = fileheader.readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", filename, ": ", err); }
    std::vector<Data> data;
    data.reserve(fileheader.numRecords);

    SliceHeader sliceheader;
    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        err = seekNextMatchingPathsFromFile(f, fileheader, currentRecord, spec, sliceheader);
        if (!err.empty()) { fclose(f); return str("Error reading file ", filename, ": ", err); }
        if (currentRecord >= fileheader.numRecords) break;

        int numRecords = (int)((sliceheader.totalSize - sliceheader.headerSize) / sizeof(int64));
        data.push_back(Data(currentRecord, sliceheader, numRecords));
        if (fread(&(data.back().data[0]), sizeof(int64), numRecords, f) != numRecords) {
            fclose(f); return str("error trying to read ", currentRecord, "-th slice payload in ", filename);
        }
    }
    fclose(f);

    if (data.empty()) {
        return str("could not match any results to the specification for file ", filename);
    }

    FILE * o = fopen(outputname, "wb");
    if (o == NULL) { return str("Could not open output file ", outputname); }

    fileheader.numRecords = data.size();
    std::string e = fileheader.writeToFile(o, true);
    if (!e.empty()) {
        err = str("error writing file ", outputname, ": ", e);
    } else {
        err = std::string();
        for (auto d = data.begin(); d != data.end(); ++d) {
            e = d->header.writeToFile(o);
            if (!e.empty()) {
                err = str("error trying to write ", d->k, "-th slice of ", filename, " in ", outputname, ": ", e);
                break;
            }
            if (fwrite(&(d->data[0]), sizeof(int64), d->data.size(), o) != d->data.size()) {
                err = str("error trying to write ", d->k, "-th slice payload of ", filename, " in ", outputname);
                break;
            }
        }
    }


    fclose(o);
    return err;
}

const char *ERR =
"\nArguments: PATHSFILENAME_INPUT PATHSFILENAME_OUTPUT [SPECTYPE VALUE]*\n\n"
"    -PATHSFILENAME_INPUT and PATHSFILENAME_OUTPUT are required (input/output paths file names).\n\n"
"    -Multiple pairs SPECTYPE VALUE can be specified. SPECTYPE can be either 'type', 'ntool', or 'z'. For the first, VALUE can be either r[aw], c[ontour] or t[oolpath], for the second, it is an integer, for the latter, a floating-point value. If several pairs have the same SPECTYPE, the latter overwrites the former. If nothing is specified, all paths are eligible.\n\n"
"This tool writes filters the contents of PATHSFILENAME_INPUT, writing in PATHSFILENAME_OUTPUT only the contents that match the specification.\n\n";

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

    const char *pathsfilename_input, *pathsfilename_output;
    
    if (!rd.readParam(pathsfilename_input,    "PATHSFILENAME_INPUT"))           { printError(rd); return -1; }
    if (!rd.readParam(pathsfilename_output,   "PATHSFILENAME_OUTPUT"))          { printError(rd); return -1; }

    PathInFileSpec spec;
    std::string err = spec.readFromCommandLine(rd);
    if (!err.empty()) {
        fprintf(stderr, "Error while trying to read the specification: %s", err.c_str());
        return -1;
    }

    err = filterMatchesFromFile(pathsfilename_input, pathsfilename_output, spec);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to get a set of paths according to the specification: %s", err.c_str());
        return -1;
    }

    return 0;
}


