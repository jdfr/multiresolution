#include "pathsfile.hpp"
#include <stdio.h>
#include <ctype.h>
#include <exception>

static_assert((sizeof(double) == sizeof(int64)) && (sizeof(int64) == sizeof(T64)) && (sizeof(double) == 8), "this code requires that <double>, <long long int> and their union all have a size of 8 bytes.");
static_assert(sizeof(int) == 4, "this code requires <int> to have a size of 4 bytes.");


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
    version  = 0;
    numtools = multispec.numspecs;
    useSched = multispec.global.useScheduler;
    voxels.reserve(numtools);
    if (useSched) {
        for (int k = 0; k < numtools; ++k) {
            voxels.emplace_back(multispec.pp[k].radius                        * factors.internal_to_input,
                                multispec.pp[k].profile->getVoxelSemiHeight() * factors.internal_to_input,
                                multispec.pp[k].profile->sliceHeight          * factors.internal_to_input,
                                multispec.pp[k].profile->applicationPoint     * factors.internal_to_input);
        }
    } else {
        for (int k = 0; k < numtools; ++k) {
            voxels.emplace_back(multispec.pp[k].radius                        * factors.internal_to_input);
        }
    }
    numRecords = 0;
}

std::string FileHeader::writeToFile(FILE *f, bool alsoNumRecords) {
    if (fwrite("PATH",   4, 1, f) != 1) return std::string("Could not write magic number!");
    if (fwrite(&version, 4, 1, f) != 1) return std::string("Could not write version!");
    if (fwrite(&numtools, sizeof(numtools), 1, f) != 1) return std::string("could not write numtools to file!");
    if (fwrite(&useSched, sizeof(useSched), 1, f) != 1) return std::string("could not write useSched to file!");
    for (int k = 0; k < numtools; ++k) {
        if (fwrite(&(voxels[k].xrad), sizeof(double), 1, f) != 1) return std::string("could not write radiusX to file!");
        if (useSched) {
            if (fwrite(&(voxels[k].zrad),               sizeof(double), 1, f) != 1) return std::string("could not write radiusZ to file!");
            if (fwrite(&(voxels[k].zheight),            sizeof(double), 1, f) != 1) return std::string("could not write Z height to file!");
            if (fwrite(&(voxels[k].z_applicationPoint), sizeof(double), 1, f) != 1) return std::string("could not write Z application point to file!");
        }
    }
    if (alsoNumRecords) {
        if (fwrite(&numRecords, sizeof(numRecords), 1, f) != 1) return std::string("could not write numRecords to file!");
    }
    return std::string();
}

int FileHeader::numRecordsOffset() {
    return (int)(sizeof(double) * (3 + (numtools * (useSched ? 4 : 1))));
}

std::string FileHeader::readFromFile(FILE * f) {
    if (fread(&version, 4, 1, f) != 1) return std::string("could not read magic number from file!");
    if (fread(&version, 4, 1, f) != 1) return std::string("could not read version from file!");
    if (version != 0) return std::string("File has non-supported version!!!");
    if (fread(&numtools, sizeof(numtools), 1, f) != 1) return std::string("could not read numtools from file!");
    if (fread(&useSched, sizeof(useSched), 1, f) != 1) return std::string("could not read useSched from file!");
    voxels.clear(); voxels.resize(numtools);
    for (int k = 0; k < numtools; ++k) {
        if (fread(&(voxels[k].xrad), sizeof(double), 1, f) != 1) return str("Could not read radiusX for tool ", k);
        if (useSched) {
            if (fread(&(voxels[k].zrad),               sizeof(double), 1, f) != 1) return str("Could not read radiusZ for tool ", k);
            if (fread(&(voxels[k].zheight),            sizeof(double), 1, f) != 1) return str("Could not read Z height for tool ", k);
            if (fread(&(voxels[k].z_applicationPoint), sizeof(double), 1, f) != 1) return str("Could not read Z app point for tool ", k);
        }
    }
    if (fread(&numRecords, sizeof(numRecords), 1, f) != 1) return std::string("could not read numRecords from file!");
    return std::string();
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


std::string SliceHeader::writeToFile(FILE * f) {
    if (fwrite(&(alldata.front()), sizeof(T64), alldata.size(), f) != alldata.size()) return std::string("could not write the slice header");
    return std::string();
}

std::string SliceHeader::readFromFile(FILE * f) {
    if (fread(&totalSize,  sizeof(totalSize),  1, f) != 1) return std::string("could not read totalSize");
    if (fread(&headerSize, sizeof(headerSize), 1, f) != 1) return std::string("could not read headerSize");
    alldata.resize(headerSize/sizeof(T64));
    if (alldata.size() < 2) return str("bad headerSize field (", headerSize, ")");
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


std::string writeSlice(FILE *f, SliceHeader header, clp::Paths &paths, PathCloseMode mode) {
    std::string res = header.writeToFile(f);
    if (!res.empty()) return res;
    IOPaths iop(f);
    if (header.saveFormat == PATHFORMAT_INT64) {
        if (!iop.writeClipperPaths(paths, mode)) return str("Could not write ClipperPaths in Int64 mode: error <", iop.errs[0].message, "> in ", iop.errs[0].function);
    } else if (header.saveFormat == PATHFORMAT_DOUBLE) {
        if (!iop.writeDoublePaths(paths, header.scaling, mode)) return str("Could not write ClipperPaths in double mode: error <", iop.errs[0].message, "> in ", iop.errs[0].function);
    } else {
        return str("bad saveFormat value: ", header.saveFormat, "\n");
    }
    return std::string();
}

bool PathInFileSpec::matchesHeader(SliceHeader &h) {
    return (h.alldata.size()>=5) &&
            ((!usetype)     || (h.type == type)) &&
            ((!usetoolpath) || (h.type == PATHTYPE_TOOLPATH_PERIMETER) || (h.type == PATHTYPE_TOOLPATH_INFILLING)) &&
            ((!usentool)    || (h.ntool == ntool)) &&
            ((!usez)        || (std::fabs(h.z - z)<1e-6));
}

std::string PathInFileSpec::readFromCommandLine(ParamReader &rd, int maxtimes, bool furtherArgs) {
    while ((maxtimes--) != 0) {
        const char * spectype;
        if (!rd.readParam(spectype, "SPECTYPE (type/ntool/z)")) break;
        if (strcmp(spectype, "type") == 0) {
            usetype          = true;
            const char * typ = NULL;
            if (!rd.readParam(typ, "value for type specification")) { return std::string(rd.fmt.str()); };
            char t = tolower(typ[0]);
            if      ((t == 'r') || (typ[0] == '0')) { type = PATHTYPE_RAW_CONTOUR; }
            else if ((t == 'c') || (typ[0] == '1')) { type = PATHTYPE_PROCESSED_CONTOUR; }
            else if ((t == 'p') || (typ[0] == '2')) { type = PATHTYPE_TOOLPATH_PERIMETER; }
            else if ((t == 'i') || (typ[0] == '3')) { type = PATHTYPE_TOOLPATH_INFILLING; }
            else if ((t == 't'))                    { usetype = false; usetoolpath = true; }
            else                                    { return str("Could not understand this type value: ", typ, "\n"); }
        } else if (strcmp(spectype, "ntool") == 0) {
            usentool = true;
            if (!rd.readParam(ntool, "value for ntool specification")) { return std::string(rd.fmt.str()); };
        } else if (strcmp(spectype, "z") == 0) {
            usez = true;
            if (!rd.readParam(z,     "value for z specification"))     { return std::string(rd.fmt.str()); };
        } else if (furtherArgs) {
            --rd.argidx;
            break;
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

bool read3DPaths(IOPaths &iop, Paths3D &paths) {
    int64 numpaths, numpoints;

    if (!iop.readInt64(numpaths)) return false;

    size_t oldsize = paths.size(), newsize = oldsize + numpaths;

    paths.resize(newsize);
    for (auto path = paths.begin() + oldsize; path != paths.end(); ++path) {
        if (!iop.readInt64(numpoints)) return false;
        path->resize(numpoints);
        size_t num = 3 * numpoints;
        if (!iop.readDoubleP((double*)&((*path)[0]), num)) return false;
    }
    return true;
}

bool write3DPaths(IOPaths &iop, Paths3D &paths, PathCloseMode mode) {
    int64 numpaths = paths.size(), numpoints, numpointsdeclared;
    if (!iop.writeInt64(numpaths)) return false;

    bool addlast = (mode == PathLoop) && (paths.size() > 0);
    for (auto path = paths.begin(); path != paths.end(); ++path) {
        numpoints = path->size();
        numpointsdeclared = addlast ? (numpoints + 1) : numpoints;
        if (!iop.writeInt64(numpointsdeclared)) return false;
        size_t num = 3 * numpoints;
        if (!iop.writeDoubleP((double*)&((*path)[0]), num)) return false;
        if (addlast) {
            if (!iop.writeDoubleP((double*)&((*path)[0]), 3)) return false;
        }
    }
    return true;
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
