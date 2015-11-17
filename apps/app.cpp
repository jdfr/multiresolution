#include "app.hpp"
#include <stdio.h>
#include <ctype.h>

bool fileExists(const char *filename) {
    FILE *test = fopen(filename, "rb");
    if (test == NULL) {
        return false;
    }
    fclose(test);
    return true;
}

char *fullPath(const char *path) {
#if (defined(_WIN32) || defined(_WIN64))
    return _fullpath(NULL, path, 1024 * 10);
#else
    return realpath(path, NULL);
#endif
}

void FileHeader::buildFrom(MultiSpec &multispec, MetricFactors &factors) {
    numtools = multispec.numspecs;
    useSched = multispec.global.useScheduler;
    radiusX.reserve(numtools);
    if (useSched) radiusZ.reserve(numtools);
    for (int k = 0; k < numtools; ++k) {
        radiusX.push_back(multispec.radiuses[k] * factors.internal_to_input);
        if (useSched) radiusZ.push_back(multispec.profiles[k]->getVoxelSemiHeight()*factors.internal_to_input);
    }
    numRecords = 0;
}

void FileHeader::writeToFiles(FILES &files, bool alsoNumRecords) {
    applyToAllFiles(files, writeInt64, numtools);
    applyToAllFiles(files, writeInt64, useSched);
    for (int k = 0; k < numtools; ++k) {
        applyToAllFiles(files, writeDouble, radiusX[k]);
        if (useSched) {
            applyToAllFiles(files, writeDouble, radiusZ[k]);
        }
    }
    if (alsoNumRecords) {
        applyToAllFiles(files, writeInt64, numRecords);
    }
}

std::string FileHeader::readFromFile(FILE * f) {
    if (fread(&numtools, sizeof(numtools), 1, f) != 1) return std::string("could not read numtools from file!");
    if (fread(&useSched, sizeof(useSched), 1, f) != 1) return std::string("could not read useSched from file!");
    radiusX.clear(); radiusX.resize(numtools);
    radiusZ.clear(); radiusZ.resize(numtools);
    for (int k = 0; k < numtools; ++k) {
                      if (fread(&(radiusX[k]), sizeof(double), 1, f) != 1) return str("Could not read radiusX for tool ", k);
        if (useSched) if (fread(&(radiusZ[k]), sizeof(double), 1, f) != 1) return str("Could not read radiusZ for tool ", k);
    }
    if (fread(&numRecords, sizeof(numRecords), 1, f) != 1) return std::string("could not read numRecords from file!");
    return std::string();
}

std::string FileHeader::openAndReadFromFile(const char * filename, FILE *&file) {
    file = fopen(filename, "rb");
    if (file == NULL) return str("Could not open file ", filename);
    std::string err = readFromFile(file);
    if (!err.empty()) {
        fclose(file);
        file = NULL;
        err = str("Could not read header from ", filename, ". Error: ", err);
    }
    return err;
}

void SliceHeader::setTo(clp::Paths &paths, PathCloseMode mode, int64 _type, int64 _ntool, double _z, int64 _saveFormat, double _scaling) {
    headerSize = numFields*sizeof(double);
    totalSize  = getPathsSerializedSize(paths, mode) + headerSize;
    type       = _type;
    ntool      = _ntool;
    z          = _z;
    saveFormat = _saveFormat;
    scaling    = _scaling;
    setBuffer();
}

void SliceHeader::setBuffer() {
    alldata.clear();
    alldata.reserve(numFields);
    alldata.push_back(T64(totalSize));
    alldata.push_back(T64(headerSize));
    alldata.push_back(T64(type));
    alldata.push_back(T64(ntool));
    alldata.push_back(T64(z));
    alldata.push_back(T64(saveFormat));
    alldata.push_back(T64(scaling));
}


void SliceHeader::writeToFiles(FILES &files) {
    applyToAllFiles(files, writeT64, &(alldata.front()), alldata.size());
}

std::string SliceHeader::readFromFile(FILE * f) {
    if (fread(&totalSize,  sizeof(totalSize),  1, f) != 1) return std::string("could not read totalSize");
    if (fread(&headerSize, sizeof(headerSize), 1, f) != 1) return std::string("could not read headerSize");
    alldata.resize(headerSize/sizeof(T64));
    alldata[0].i = totalSize;
    alldata[1].i = headerSize;
    if (headerSize > 2) {
        int numToRead = (int)(headerSize - 2 * sizeof(T64));
        if (fread(&(alldata[2]), 1, numToRead, f) != numToRead) return std::string("could not read slice header");
        numToRead = (int)(headerSize / sizeof(T64));
        if (numToRead > 2) {type       = alldata[2].i;
        if (numToRead > 3) {ntool      = alldata[3].i;
        if (numToRead > 4) {z          = alldata[4].d;
        if (numToRead > 5) {saveFormat = alldata[5].i;
        if (numToRead > 6) {scaling    = alldata[6].d;}}}}}
    }
    return std::string();
}


std::string writeSlice(FILES &files, SliceHeader header, clp::Paths &paths, PathCloseMode mode) {
    header.writeToFiles(files);
    if (header.saveFormat == SAVEMODE_INT64) {
        applyToAllFiles(files, writeClipperPaths, paths, mode);
    } else if (header.saveFormat == SAVEMODE_DOUBLE) {
        writeDoublePaths(files, paths, header.scaling, mode);
    } else {
        return str("bad saveFormat value: ", header.saveFormat, "\n");
    }
    return std::string();
}

bool PathInFileSpec::matchesHeader(SliceHeader &h) {
    return (h.alldata.size()>=5) &&
            ((!usetype)  || (type == h.type)) &&
            ((!usentool) || (ntool == h.ntool)) &&
            ((!usez)     || (z == h.z));
}

std::string PathInFileSpec::readFromCommandLine(ParamReader &rd) {
    while (true) {
        const char * spectype;
        if (!rd.readParam(spectype, "SPECTYPE (type/ntool/z)")) break;
        if (strcmp(spectype, "type") == 0) {
            this->usetype = true;
            const char * typ=NULL;
            if (!rd.readParam(typ, "value for type specification")) { return std::string(rd.fmt.str()); };
            char t = tolower(typ[0]);
            if ((t == 'r') || (typ[0] == '0')) {
                this->type = TYPE_RAW_CONTOUR;
            } else if ((t == 'p') || (typ[0] == '1')) {
                this->type = TYPE_PROCESSED_CONTOUR;
            } else if ((t == 't') || (typ[0] == '2')) {
                this->type = TYPE_TOOLPATH;
            } else {
                return str("Could not understand this type value: ", typ, "\n");
            }
        } else if (strcmp(spectype, "ntool") == 0) {
            this->usentool = true;
            if (!rd.readParam(this->ntool, "value for ntool specification")) { return std::string(rd.fmt.str()); };
        } else if (strcmp(spectype, "z") == 0) {
            this->usez = true;
            if (!rd.readParam(this->z, "value for z specification")) { return std::string(rd.fmt.str()); };
        } else {
            return str("SPECTYPE not understood (should be type/ntool/z): ", spectype, "\n");
        }
    }
    return std::string();
}

std::string seekNextMatchingPathsFromFile(FILE * f, FileHeader &fileheader, int &currentRecord, PathInFileSpec &spec, SliceHeader &sliceheader) {
    for (; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string err = sliceheader.readFromFile(f);
        if (!err.empty()) { return str("Error reading ", currentRecord, "-th slice header: ", err); }
        if (sliceheader.alldata.size() < 7) { return str("Error reading ", currentRecord, "-th slice header: header is too short!"); }
        if (spec.matchesHeader(sliceheader)) {
            break;
        } else {
            fseek(f, (long)(sliceheader.totalSize - sliceheader.headerSize), SEEK_CUR);
        }
    }
    return std::string();
}

void read3DPaths(FILE * f, Paths3D &paths) {
    int64 numpaths, numpoints;

    READ_BINARY(&numpaths, sizeof(int64), 1, f);

    size_t oldsize = paths.size(), newsize = oldsize + numpaths;

    paths.resize(newsize);
    for (auto path = paths.begin() + oldsize; path != paths.end(); ++path) {
        READ_BINARY(&numpoints, sizeof(int64), 1, f);
        path->resize(numpoints);
        size_t num = 3 * numpoints;
        READ_BINARY(&((*path)[0]), sizeof(double), num, f);
    }
}

void write3DPaths(FILE * f, Paths3D &paths, PathCloseMode mode) {
    int64 numpaths = paths.size(), numpoints, numpointsdeclared;
    WRITE_BINARY(&numpaths, sizeof(int64), 1, f);

    bool addlast = (mode == PathLoop) && (paths.size() > 0);
    for (auto path = paths.begin(); path != paths.end(); ++path) {
        numpoints = path->size();
        numpointsdeclared = addlast ? (numpoints + 1) : numpoints;
        WRITE_BINARY(&numpointsdeclared, sizeof(int64), 1, f);
        size_t num = 3 * numpoints;
        WRITE_BINARY(&((*path)[0]), sizeof(double), num, f);
        if (addlast) {
            WRITE_BINARY(&((*path)[0]), sizeof(double), 3, f);
        }
    }
}

int getPathsSerializedSize(Paths3D &paths, PathCloseMode mode) {
    int s = 0;
    for (auto path = paths.begin(); path != paths.end(); ++path) {
        s += (int)path->size();
    }
    s *= 3;
    s += 1 + ((int)paths.size());
    if ((mode == PathLoop) && (paths.size() > 0)) s += 3 * (int)paths.size();
    s *= 8;
    return s;
}

