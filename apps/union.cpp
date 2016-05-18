#include "pathsfile.hpp"
#include "apputil.hpp"

std::string pathUnion(const char ** inputs, int numinputs, const char * output) {
    std::vector<FILEOwner> is(numinputs);
    std::vector<FileHeader> fileheaders_i(numinputs);
    FileHeader fileheader_o;
    fileheader_o.version  = 1; //this must be kept in sync with FileHeader read/write code in pathsfile.cpp
    fileheader_o.numtools = 0;
    fileheader_o.useSched = false;
    fileheader_o.numRecords = 0;
    for (int i = 0; i < numinputs; ++i) {
        if (!fileExists(inputs[i])) {
            return str("Error: input file <", inputs[i], "> does not exist!");
        }
        if (!is[i].open(inputs[i], "rb")) { return str("Could not open input file ", inputs[i]); }
        std::string err = fileheaders_i[i].readFromFile(is[i].f);
        if (!err.empty()) { return str("Error reading file header for ", inputs[i], ": ", err); }
        fileheader_o.numRecords += fileheaders_i[i].numRecords;
        if (fileheader_o.numtools < fileheaders_i[i].numtools) {
            fileheader_o.numtools = fileheaders_i[i].numtools;
            fileheader_o.useSched = fileheaders_i[i].useSched;
            fileheader_o.voxels   = fileheaders_i[i].voxels;
            if (fileheaders_i[i].version > 0) { //there is no clear way to do this. For the moment, just overwrite data...
                if (!fileheaders_i[i].additional.empty()) {
                    fileheader_o.additional = std::move(fileheaders_i[i].additional);
                }
            }
        }
    }

    FILEOwner o(output, "wb");
    if (!o.isopen()) { return str("Could not open output file ", output); }
    std::string err = fileheader_o.writeToFile(o.f, true);
    if (!err.empty()) { return str("Error writing file header for ", output, ": ", err); }

    SliceHeader sliceheader;
    std::vector<int64> data;
    for (int i = 0; i < numinputs; ++i) {
        for (int currentRecord = 0; currentRecord < fileheaders_i[i].numRecords; ++currentRecord) {
            std::string err = sliceheader.readFromFile(is[i].f);
            if (!err.empty()) { return str("Error reading ", currentRecord, "-th slice header in file ", inputs[i], ": ", err); }
            err = sliceheader.writeToFile(o.f);
            if (!err.empty()) { return str("Error writing ", currentRecord, "-th slice header in file ", inputs[i], " to file ", output, ": ", err); }

            int numPackets = (int)((sliceheader.totalSize - sliceheader.headerSize) / sizeof(int64));
            data.resize(numPackets);
            size_t numread = fread(&(data[0]), sizeof(int64), numPackets, is[i].f);
            if (numread != numPackets) {
                return str("error trying to read ", currentRecord, "-th slice payload (size: ", numPackets, " but read ", numread, ") in ", inputs[i]);
            }
            size_t numwrite = fwrite(&(data[0]), sizeof(int64), numPackets, o.f);
            if (numwrite != numPackets) {
                return str("error trying to write ", currentRecord, "-th slice payload (size: ", numPackets, " but write ", numwrite, ") to ", output);
            }
        }
    }

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
    TimeMeasurements tm;
    tm.measureTime();
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

    tm.measureTime();
    tm.printLastMeasurement(stdout, "TOTAL TIME: CPU %f, WALL TIME %f\n");
    return 0;
}


