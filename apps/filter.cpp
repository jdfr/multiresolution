#include "pathsfile.hpp"
#include "simpleparsing.hpp"
#include "apputil.hpp"

std::string filterMatchesFromFile(const char * filename, const char *outputname, PathInFileSpec spec) {
    FILEOwner i(filename, "rb");
    if (!i.isopen()) { return str("Could not open file ", filename); }

    FILEOwner o(outputname, "wb");
    if (!o.isopen()) { return str("Could not open output file ", outputname); }

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(i.f);    if (!err.empty()) { return str("Error reading file header for ", filename, ": ", err); }

    int64 inputNumRecords  = fileheader.numRecords;
    int64 outputNumRecords = 0;
    
    std::string e = fileheader.writeToFile(o.f, true); if (!e.empty())   { return str("error writing file ", outputname, ": ", e); }
    
    SliceHeader sliceheader;
    std::vector<T64> data;
    
    for (int currentRecord = 0; currentRecord < inputNumRecords; ++currentRecord) {
        err = seekNextMatchingPathsFromFile(i.f, fileheader, currentRecord, spec, sliceheader);
        if (!err.empty()) { return str("Error reading file ", filename, ": ", err); }
        if (currentRecord >= inputNumRecords) break;

        e = sliceheader.writeToFile(o.f);
        if (!e.empty())                                                    { return str("error trying to write ", currentRecord, "-th slice header of ", filename, " in ", outputname, ": ", e); }
        
        int64 sizeT64 = (sliceheader.totalSize - sliceheader.headerSize) / sizeof(T64);
        data.resize(sizeT64);
        
        if (fread (&(data.front()), sizeof(int64), sizeT64, i.f) != sizeT64) { return str("error trying to read ",  currentRecord, "-th slice payload in ", filename); }
        if (fwrite(&(data.front()), sizeof(int64), sizeT64, o.f) != sizeT64) { return str("error trying to write ", currentRecord, "-th slice payload of ", filename, " in ", outputname); }
        ++outputNumRecords;
    }
    i.close();
    
    int numToSkip = fileheader.numRecordsOffset();
    if (fseek(o.f, numToSkip, SEEK_SET) != 0)                                { return str( "fseek failed: could not write numRecords to file ", filename); }
    if (fwrite(&outputNumRecords, sizeof(outputNumRecords), 1, o.f) != 1)    { return str("fwrite failed: could not write numRecords to file ", filename); }
    o.close();

    if (outputNumRecords == 0) {
        return str("could not match any results to the specification for file ", filename);
    }

    return err;
}

const char *ERR =
"\nArguments: PATHSFILENAME_INPUT PATHSFILENAME_OUTPUT [SPECTYPE VALUE]*\n\n"
"    -PATHSFILENAME_INPUT and PATHSFILENAME_OUTPUT are required (input/output paths file names).\n\n"
"    -Multiple pairs SPECTYPE VALUE can be specified. SPECTYPE can be either 'type', 'ntool', or 'z'. For the first, VALUE can be either r[aw], c[ontour], p[erimeter] (perimeter toolpath type), i[nfilling] (infilling toolpath type), or t[oolpath] (any toolpath type). For the second, it is an integer, for the latter, a floating-point value. If several pairs have the same SPECTYPE, the latter overwrites the former. If nothing is specified, all paths are eligible.\n\n"
"This tool writes filters the contents of PATHSFILENAME_INPUT, writing in PATHSFILENAME_OUTPUT only the contents that match the specification.\n\n";

void printError(ParamReader &rd) {
    rd.fmt << ERR;
    std::string err = rd.fmt.str();
    fprintf(stderr, err.c_str());
}

int main(int argc, const char** argv) {
    TimeMeasurements tm;
    tm.measureTime();
    ParamReader rd = ParamReader::getParamReaderWithOptionalResponseFile(argc, argv);

    if (!rd.err.empty()) {
        fprintf(stderr, "ParamReader error: %s\n", rd.err.c_str());
        return -1;
    }

    const char *pathsfilename_input, *pathsfilename_output;
    
    if (!rd.readParam(pathsfilename_input,    "PATHSFILENAME_INPUT"))           { printError(rd); return -1; }
    if (!rd.readParam(pathsfilename_output,   "PATHSFILENAME_OUTPUT"))          { printError(rd); return -1; }

    PathInFileSpec spec;
    std::string err = spec.readFromCommandLine(rd, -1, false);
    if (!err.empty()) {
        fprintf(stderr, "Error while trying to read the specification: %s", err.c_str());
        return -1;
    }

    err = filterMatchesFromFile(pathsfilename_input, pathsfilename_output, spec);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to get a set of paths according to the specification: %s", err.c_str());
        return -1;
    }

    tm.measureTime();
    tm.printLastMeasurement(stdout, "TOTAL TIME: CPU %f, WALL TIME %f\n");
    return 0;
}


