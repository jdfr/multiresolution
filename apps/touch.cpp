#include "pathsfile.hpp"
#include "simpleparsing.hpp"
#include "measureTime.hpp"

typedef struct Data {
    int k;
    SliceHeader header;
    std::vector<int64> data;
    Data(int _k, SliceHeader _header, int datasize) : k(_k), header(std::move(_header)), data(datasize) {}
} Data;

typedef std::pair<bool, T64> Add;

typedef struct Spec {
    PathInFileSpec filterspec;
    PathInFileSpec setspec;
    std::vector<Add> adds;
} Spec;

std::string touchFile(const char * filename, const char *outputname, std::vector<Spec> &specs) {
    FILE * f = fopen(filename, "rb");
    if (f == NULL) { return str("Could not open file ", filename); }

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", filename, ": ", err); }
    std::vector<Data> data;
    data.reserve(fileheader.numRecords);

    SliceHeader sliceheader;
    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string e = sliceheader.readFromFile(f);
        if (!e.empty())                     { err = str("Error reading ", currentRecord, "-th slice header: ", e); break; }
        if (sliceheader.alldata.size() < 7) { err = str("Error reading ", currentRecord, "-th slice header: header is too short!"); break;  }

        for (auto &spec : specs) {
            if (spec.filterspec.matchesHeader(sliceheader)) {
                if (spec.setspec.usentool)    sliceheader.ntool = spec.setspec.ntool;
                if (spec.setspec.usetoolpath) sliceheader.type  = PATHTYPE_TOOLPATH_PERIMETER;
                if (spec.setspec.usetype)     sliceheader.type  = spec.setspec.type;
                if (spec.setspec.usez)        sliceheader.z     = spec.setspec.z;
                sliceheader.setBuffer();
                if (!spec.adds.empty()) {
                    auto num = spec.adds.size()*sizeof(double);
                    sliceheader.alldata[0].i += num;
                    sliceheader.alldata[1].i += num;
                    sliceheader.alldata.reserve(sliceheader.alldata.size() + num);
                    for (auto &add : spec.adds) {
                        sliceheader.alldata.push_back(add.second);
                    }
                }
                break;
            }
        }

        int numRecords = (int)((sliceheader.totalSize - sliceheader.headerSize) / sizeof(int64));
        data.push_back(Data(currentRecord, sliceheader, numRecords));
        if (fread(&(data.back().data[0]), sizeof(int64), numRecords, f) != numRecords) {
            err = str("error trying to read ", currentRecord, "-th slice payload in ", filename);
            break;
        }

    }

    fclose(f);

    if (!err.empty()) { return err; }

    FILE * o = fopen(outputname, "wb");
    if (o == NULL) { return str("Could not open output file ", outputname); }

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
    return std::string();
}

const char *ERR =
"\nArguments: PATHSFILENAME_INPUT PATHSFILENAME_OUTPUT [set [SPECTYPE VALUE]+ to [SPECTYPE VALUE]+ [add [ADDTYPE VALUE]+]]+\n\n"
"    -PATHSFILENAME_INPUT and PATHSFILENAME_OUTPUT are required (input/output paths file names).\n\n"
"    -a sequence of specifications 'set SPEC_MATCH to SPEC_SET [add SPEC_ADD]', where SPEC_MATCH and SPEC_SET are lists of pairs 'SPECTYPE VALUE'.\n\n"
"        -SPECTYPE can be either 'type', 'ntool', or 'z'.\n\n"
"            -If SPECTYPE is 'type', VALUE can be either r[aw], c[ontour], p[erimeter] (perimeter toolpath type), i[nfilling] (infilling toolpath type), or t[oolpath] (any toolpath type).\n\n"
"            -If SPECTYPE is 'ntool', VALUE is an integer.\n\n"
"            -If SPECTYPE is 'z', VALUE is a floating point number.\n\n"
"        -If 'add SPEC_ADD' is specified (it is optional), a list of values are appended to the header of the matching headers. these values are specified by a list of pairs 'ADDTYPE VALUE'.\n\n"
"            -If ADDTYPE is 'double', VALUE is a floating point number.\n\n"
"            -If ADDTYPE is 'integer', VALUE is a 64-bit signed integer.\n\n"
"This tool modifies the headers of records: if they match a SPEC_MATCH set of specifications, their values are set according to the corresponding SPEC_SET and SPEC_ADD. If the corresponding SPEC_SET specifies 'type t[oolpath]', it will be implicitly translated to 'type perimeter'.\n\n";

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

    std::vector<Spec> specs;
    std::string err;

    while (rd.argidx<rd.argc) {
        if (!rd.readKeyword("set", false, "'set' keyword")) { printError(rd); return -1; }

        specs.push_back(Spec());
        err = specs.back().filterspec.readFromCommandLine(rd, -1, true);
        if (!err.empty()) {
            fprintf(stderr, "Error while trying to read the %d-th filter specification: %s", specs.size(), err.c_str());
            return -1;
        }

        if (!rd.readKeyword("to", false, "'to' keyword")) { printError(rd); return -1; }

        err = specs.back().setspec.readFromCommandLine(rd, -1, true);
        if (!err.empty()) {
            fprintf(stderr, "Error while trying to read the %d-th set specification: %s", specs.size(), err.c_str());
            return -1;
        }

        if (rd.argidx < rd.argc) {
            if (rd.readKeyword("add", false, "'add' keyword")) {
                const char *type;
                bool isdouble;
                if (!rd.readParam(type, "add_type (double/integer)"))           { printError(rd); return -1; }
                if (tolower(type[0] == 'd')) {
                    isdouble = true;
                } else if (tolower(type[0] == 'i')) {
                    isdouble = false;
                } else {
                    fprintf(stderr, "Error: add type must be either (d)ouble or (i)nteger, but it was <%s>\n", type);
                    return -1;
                }
                T64 value;
                if (isdouble) {
                    if (!rd.readParam(value.d, "add_value (double/integer)"))           { printError(rd); return -1; }
                } else {
                    if (!rd.readParam(value.i, "add_value (double/integer)"))           { printError(rd); return -1; }
                }
                specs.back().adds.emplace_back(isdouble, value);
            } else {
                --rd.argidx;
            }
        }
    }

    err = touchFile(pathsfilename_input, pathsfilename_output, specs);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to get a set of paths according to the specification: %s", err.c_str());
        return -1;
    }

    tm.measureTime();
    tm.printLastMeasurement(stdout, "TOTAL TIME: CPU %f, WALL TIME %f\n");
    return 0;
}


