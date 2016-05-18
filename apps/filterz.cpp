#include "pathsfile.hpp"
#include "simpleparsing.hpp"
#include "apputil.hpp"
#include <functional>

typedef std::pair<std::function<bool(double, double)>, double> Operand;

std::string filterMatchesFromFile(const char * filename, const char *outputname, std::vector<Operand> ops) {
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
        err = sliceheader.readFromFile(i.f);
        if (!err.empty()) { return str("Error reading ", currentRecord, "-th slice header: ", err); }
        if (sliceheader.alldata.size() < 7) { return str("Error reading ", currentRecord, "-th slice header: header is too short!"); }
        bool seeked = false;
        for (auto &op : ops) {
            if (!op.first(sliceheader.z, op.second)) {
                if (fseek(i.f, (long)(sliceheader.totalSize - sliceheader.headerSize), SEEK_CUR)!=0) { return str("Error reading ", currentRecord, "-th slice header: could not skip the payload!"); }
                seeked = true;
                break;
            }
        }
        if (seeked) continue;

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
"\nArguments: PATHSFILENAME_INPUT PATHSFILENAME_OUTPUT [OPERAND VALUE]+\n\n"
"    -PATHSFILENAME_INPUT and PATHSFILENAME_OUTPUT are required (input/output paths file names).\n\n"
"    -OPERAND must be one of the following: > < >= <= == !=\n\n"
"    -VALUE is a floating point value.\n\n"
"This tool filters the contents of PATHSFILENAME_INPUT, writing in PATHSFILENAME_OUTPUT only the contents that have Z values matching the specifications Z OPERAND VALUE, for all pairs OPERAND VALUE.\n\n";

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

    const char *pathsfilename_input, *pathsfilename_output, *operand;
    double value;
    
    if (!rd.readParam(pathsfilename_input,    "PATHSFILENAME_INPUT"))           { printError(rd); return -1; }
    if (!rd.readParam(pathsfilename_output,   "PATHSFILENAME_OUTPUT"))          { printError(rd); return -1; }
    
    std::vector<Operand> ops;

    while (true) {
        if (!rd.readParam(operand, "OPERAND")) break;
        if (!rd.readParam(value,   "VALUE"))   { printError(rd); return -1; }
        
        Operand op;
        if      (strcmp(operand, "equal")==0)         op.first = std::equal_to     <double>();
        else if (strcmp(operand, "not_equal")==0)     op.first = std::not_equal_to <double>();
        else if (strcmp(operand, "greater" )==0)      op.first = std::greater      <double>();
        else if (strcmp(operand, "greater_equal")==0) op.first = std::greater_equal<double>();
        else if (strcmp(operand, "less" )==0)         op.first = std::less         <double>();
        else if (strcmp(operand, "less_equal")==0)    op.first = std::less_equal   <double>();
        else if (strcmp(operand, "==")==0) op.first = std::equal_to     <double>();
        else if (strcmp(operand, "!=")==0) op.first = std::not_equal_to <double>();
        else if (strcmp(operand, ">" )==0) op.first = std::greater      <double>();
        else if (strcmp(operand, ">=")==0) op.first = std::greater_equal<double>();
        else if (strcmp(operand, "<" )==0) op.first = std::less         <double>();
        else if (strcmp(operand, "<=")==0) op.first = std::less_equal   <double>();
        else {
            fprintf(stderr, "Error: operator '%s' not understood!!!\n", operand);
            return -1;
        }
        op.second = value;
        ops.emplace_back(std::move(op));
    }
    
    if (ops.empty()) {
        fprintf(stderr, "Error: no OPERATOR VALUE pairs were read!!!\n");
        return -1;
    }

    std::string err = filterMatchesFromFile(pathsfilename_input, pathsfilename_output, std::move(ops));

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to get a set of paths according to the specification: %s", err.c_str());
        return -1;
    }

    tm.measureTime();
    tm.printLastMeasurement(stdout, "TOTAL TIME: CPU %f, WALL TIME %f\n");
    return 0;
}


