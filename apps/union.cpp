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

void closeAll(std::vector<FILE *> &is, FILE * o) {
    if (o != NULL) fclose(o);
    for (auto &i : is) {
        if (i != NULL) fclose(i);
    }
}

std::string pathUnion(const char ** inputs, int numinputs, const char * output) {
    for (int i = 0; i < numinputs; ++i) {
        if (!fileExists(inputs[i])) {
            return str("Error: input file <", inputs[i], "> does not exist!");
        }
    }

    std::vector<FILE *> is(numinputs, NULL);
    std::vector<FileHeader> fileheaders_i(numinputs);
    FileHeader fileheader_o;
    fileheader_o.numtools = 0;
    fileheader_o.useSched = false;
    fileheader_o.numRecords = 0;
    for (int i = 0; i < numinputs; ++i) {
        is[i] = fopen(inputs[i], "rb");
        if (is[i] == NULL) { closeAll(is, NULL); return str("Could not open input file ", inputs[i]); }
        std::string err = fileheaders_i[i].readFromFile(is[i]);
        if (!err.empty()) { closeAll(is, NULL); return str("Error reading file header for ", inputs[i], ": ", err); }
        fileheader_o.numRecords += fileheaders_i[i].numRecords;
        if (fileheader_o.numtools < fileheaders_i[i].numtools) {
            fileheader_o.numtools = fileheaders_i[i].numtools;
            fileheader_o.useSched = fileheaders_i[i].useSched;
            fileheader_o.radiusX  = fileheaders_i[i].radiusX;
            fileheader_o.radiusZ  = fileheaders_i[i].radiusZ;
        }
    }

    FILE * o = fopen(output, "wb");
    if (o == NULL) { closeAll(is, NULL); return str("Could not open output file ", output); }
    std::string err = fileheader_o.writeToFile(o, true);
    if (!err.empty()) { closeAll(is, o); return str("Error writing file header for ", output, ": ", err); }

    SliceHeader sliceheader;
    std::vector<int64> data;
    for (int i = 0; i < numinputs; ++i) {
        for (int currentRecord = 0; currentRecord < fileheaders_i[i].numRecords; ++currentRecord) {
            std::string err = sliceheader.readFromFile(is[i]);
            if (!err.empty()) { closeAll(is, o); return str("Error reading ", currentRecord, "-th slice header in file ", inputs[i], ": ", err); }
            err = sliceheader.writeToFile(o);
            if (!err.empty()) { closeAll(is, o); return str("Error writing ", currentRecord, "-th slice header in file ", inputs[i], " to file ", output, ": ", err); }

            int numPackets = (int)((sliceheader.totalSize - sliceheader.headerSize) / sizeof(int64));
            data.resize(numPackets);
            size_t numread = fread(&(data[0]), sizeof(int64), numPackets, is[i]);
            if (numread != numPackets) {
                closeAll(is, o);
                return str("error trying to read ", currentRecord, "-th slice payload (size: ", numPackets, " but read ", numread, ") in ", inputs[i]);
            }
            size_t numwrite = fwrite(&(data[0]), sizeof(int64), numPackets, o);
            if (numwrite != numPackets) {
                closeAll(is, o);
                return str("error trying to write ", currentRecord, "-th slice payload (size: ", numPackets, " but write ", numwrite, ") to ", output);
            }
        }
    }

    closeAll(is, o);
    return std::string();
}

const char *ERR =
"\nArguments: [PATHSFILENAME_INPUT]+ PATHSFILENAME_OUTPUT\n\n"
"    -PATHSFILENAME_INPUT: two or more file names of input pathsfiles.\n\n"
"    -PATHSFILENAME_OUTPUT: file name of output pathsfiles.\n\n";

void printError(ParamReader &rd) {
    rd.fmt << ERR;
    std::string err = rd.fmt.str();
    fprintf(stderr, err.c_str());
}

int main(int argc, const char** argv) {
    --argc; ++argv;
    if (argc < 3) {
        fprintf(stderr, "At least two input files and one output file must be provided.\n%s", ERR);
        return -1;
    }

    int numinputs = argc - 1;

    std::string err = pathUnion(argv, numinputs, argv[numinputs]);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to do union: %s", err.c_str());
        return -1;
    }

    return 0;
}


