#include "pathsfile.hpp"
#include "simpleparsing.hpp"

typedef struct Data {
    int k;
    SliceHeader header;
    std::vector<int64> data;
    Data(int _k, SliceHeader _header, int datasize) : k(_k), header(std::move(_header)), data(datasize) {}
} Data;

typedef std::pair<PathInFileSpec, PathInFileSpec> specpair;

std::string touchFile(const char * filename, const char *outputname, std::vector<specpair> &pairs) {
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

        for (auto &pair : pairs) {
            auto &filterspec = pair.first;
            auto &setspec    = pair.second;
            if (filterspec.matchesHeader(sliceheader)) {
                if (setspec.usentool) sliceheader.ntool = setspec.ntool;
                if (setspec.usetype)  sliceheader.type = setspec.type;
                if (setspec.usez)     sliceheader.z = setspec.z;
                sliceheader.setBuffer();
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
"\nArguments: PATHSFILENAME_INPUT PATHSFILENAME_OUTPUT [set [SPECTYPE VALUE]+ to [SPECTYPE VALUE]+]+\n\n"
"    -PATHSFILENAME_INPUT and PATHSFILENAME_OUTPUT are required (input/output paths file names).\n\n"
"    -a sequence of specifications 'set SPEC_MATCH to SPEC_SET', where SPEC_MATCH and SPEC_SET are lists of pairs 'SPECTYPE VALUE'.\n\n"
"        -SPECTYPE can be either 'type', 'ntool', or 'z'.\n\n"
"        -If SPECTYPE is 'type', VALUE can be either r[aw], c[ontour] or t[oolpath].\n\n"
"        -If SPECTYPE is 'ntool', VALUE is an integer.\n\n"
"        -If SPECTYPE is 'z', VALUE is a floating point number.\n\n"
"This tool modifies the headers of records: if they match a SPEC_MATCH set of specifications, their values are set according to the cooresponding SPEC_SET.\n\n";

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

    const char *pathsfilename_input, *pathsfilename_output;
    
    if (!rd.readParam(pathsfilename_input,    "PATHSFILENAME_INPUT"))           { printError(rd); return -1; }
    if (!rd.readParam(pathsfilename_output,   "PATHSFILENAME_OUTPUT"))          { printError(rd); return -1; }

    std::vector<specpair> pairs;
    std::string err;

    while (rd.argidx<rd.argc) {
        if (!rd.readKeyword("set", false, "'set' keyword")) { printError(rd); return -1; }

        pairs.push_back(specpair());
        err = pairs.back().first.readFromCommandLine(rd, -1, true);
        if (!err.empty()) {
            fprintf(stderr, "Error while trying to read the %d-th filter specification: %s", pairs.size(), err.c_str());
            return -1;
        }

        if (!rd.readKeyword("to", false, "'to' keyword")) { printError(rd); return -1; }

        err = pairs.back().second.readFromCommandLine(rd, -1, true);
        if (!err.empty()) {
            fprintf(stderr, "Error while trying to read the %d-th set specification: %s", pairs.size(), err.c_str());
            return -1;
        }
    }

    err = touchFile(pathsfilename_input, pathsfilename_output, pairs);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to get a set of paths according to the specification: %s", err.c_str());
        return -1;
    }

    return 0;
}


