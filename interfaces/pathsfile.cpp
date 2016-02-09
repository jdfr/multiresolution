#include "pathsfile.hpp"
#include <stdio.h>
#include <ctype.h>
#include <exception>

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
        radiusX.push_back(multispec.pp[k].radius * factors.internal_to_input);
        if (useSched) radiusZ.push_back(multispec.pp[k].profile->getVoxelSemiHeight()*factors.internal_to_input);
    }
    numRecords = 0;
}

std::string FileHeader::writeToFile(FILE *f, bool alsoNumRecords) {
    if (fwrite(&numtools, sizeof(numtools), 1, f) != 1) return std::string("could not write numtools to file!");
    if (fwrite(&useSched, sizeof(useSched), 1, f) != 1) return std::string("could not write useSched to file!");
    for (int k = 0; k < numtools; ++k) {
        if (fwrite(&(radiusX[k]), sizeof(double), 1, f) != 1) return std::string("could not write radiusX to file!");
        if (useSched) {
            if (fwrite(&(radiusZ[k]), sizeof(double), 1, f) != 1) return std::string("could not write radiusZ to file!");
        }
    }
    if (alsoNumRecords) {
        if (fwrite(&numRecords, sizeof(numRecords), 1, f) != 1) return std::string("could not write numRecords to file!");
    }
    return std::string();
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
            ((!usetype)  || (type == h.type)) &&
            ((!usentool) || (ntool == h.ntool)) &&
            ((!usez)     || (std::fabs(z - h.z)<1e-6));
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
            else if ((t == 't') || (typ[0] == '2')) { type = PATHTYPE_TOOLPATH; }
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



bool PathWriter::start() {
    throw std::runtime_error("Base method PathWriter::close() should never be called!");
}
bool PathWriter::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    throw std::runtime_error("Base method PathWriter::writePaths() should never be called!");
}
bool PathWriter::close() {
    throw std::runtime_error("Base method PathWriter::close() should never be called!");
}

#define ISASCII  (mode==DXFAscii)
#define ISBINARY (mode==DXFBinary)

template class PathWriterMultiFile<DXFAsciiPathWriter>;
template class PathWriterMultiFile<DXFBinaryPathWriter>;

template class DXFPathWriter<DXFAscii>;
template class DXFPathWriter<DXFBinary>;

template<typename T> bool PathWriterMultiFile<T>::matchZNtool(int _type, int _ntool, double _z) {
    return generic_all || ((generic_for_ntool || (ntool == _ntool)) &&
                           (generic_for_z     || (std::fabs(z - _z) < epsilon)) &&
                           (generic_for_type  || (_type==type)));
}

template<typename T> int PathWriterMultiFile<T>::findOrCreateSubwriter(int _type, double _radius, int _ntool, double _z) {
    if (subwriters.size()>0) {
        if (subwriters[currentSubwriter]->matchZNtool(_type, _ntool, _z)) {
            return currentSubwriter;
        }
        for (auto w = subwriters.begin(); w != subwriters.end(); ++w) {
            if ((*w)->matchZNtool(_type, _ntool, _z)) {
                currentSubwriter = (int)(w - subwriters.begin());
                return currentSubwriter;
            }
        }
    }

    std::string N, Z;
    const char * Type = "";
    if (!generic_for_type)  {
        switch (_type) {
        case PATHTYPE_TOOLPATH:          Type = ".toolpaths"; break;
        case PATHTYPE_PROCESSED_CONTOUR: Type = ".contour";   break;
        case PATHTYPE_RAW_CONTOUR:       Type = ".raw";       break;
        default:                         Type = ".unknown";
        }
    }
    if (!generic_for_ntool) N       = str(".N", _ntool);
    if (!generic_for_z)     Z       = str(".Z", _z);
    std::string newfilename         = str(filename, Type, N, Z, ".dxf");
    subwriters.push_back(new T(newfilename, epsilon, generic_for_type, generic_for_ntool, generic_for_z));
    subwriters.back()->type         = _type;
    subwriters.back()->radius       = _radius;
    subwriters.back()->ntool        = _ntool;
    subwriters.back()->z            = _z;
    subwriters.back()->delegateWork = false;
    return currentSubwriter = ((int)subwriters.size() - 1);
}

template<typename T> bool PathWriterMultiFile<T>::start() {
    if (!this->isopen) {
        if (!static_cast<T*>(this)->startWriter()) return false;
    }
}

template<typename T> bool PathWriterMultiFile<T>::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    if (delegateWork) {
        int idx = findOrCreateSubwriter(type, radius, ntool, z);
        bool ret = subwriters[idx]->writePaths(paths, type, radius, ntool, z, scaling, isClosed);
        if (!ret) err = subwriters[idx]->err;
        return ret;
    }
    if (!isopen) {
        if (!static_cast<T*>(this)->startWriter()) return false;
    }
    return static_cast<T*>(this)->writePathsSpecific(paths, type, radius, ntool, z, scaling, isClosed);
}

template<typename T> bool PathWriterMultiFile<T>::close() {
    bool ok = true;
    if (!subwriters.empty()) {
        for (auto &w : subwriters) {
            if (!w->close()) {
                err = w->err;
                ok = false;
            }
            delete w;
        }
        subwriters.clear();
    }
    if (isopen) {
        bool newok;
        newok = static_cast<T*>(this)->endWriter();
        ok = ok && newok;
        newok = static_cast<T*>(this)->specificClose();
        ok = ok && newok;
        isopen = false;
    }
    return ok;
}

template<DXFWMode mode> DXFPathWriter<mode>::DXFPathWriter(std::string file, double _epsilon, bool _generic_type, bool _generic_ntool, bool _generic_z) {
    this->epsilon           = _epsilon;
    this->generic_for_ntool = _generic_ntool;
    this->generic_for_z     = _generic_z;
    this->generic_for_type  = _generic_type;
    this->generic_all       = this->generic_for_ntool && this->generic_for_z && this->generic_for_type;
    this->delegateWork      = !this->generic_all;
    this->filename          = std::move(file);
    if (this->generic_all) {
        bool file_ends_in_dxf = (this->filename.length() >= 4) &&
            (tolower(*(this->filename.end() - 1)) == 'f') &&
            (tolower(*(this->filename.end() - 2)) == 'x') &&
            (tolower(*(this->filename.end() - 3)) == 'd') &&
            ((*(this->filename.end() - 4)) == '.');
        if (!file_ends_in_dxf) {
            this->filename += ".dxf";
        }
    }
    this->isopen = false;
}

template<DXFWMode mode> bool DXFPathWriter<mode>::startWriter() {
    if (ISBINARY) {
        if (sizeof(char) != 1) {
            this->err = str("To write binary DXF files we expect char to be 1 byte long!");
            return false;
        }
    }
    this->f = fopen(this->filename.c_str(), ISASCII ? "wt" : "wb");
    if (this->f == NULL) {
        this->err = str("DXF output file <", this->filename, ">: file could not be open");
        return false;
    }
    this->isopen = true;
    if (ISASCII) {
        const char * HEADERA =
            //DXF header
            "0\nSECTION\n"
            "2\nHEADER\n"
            "9\n$ACADVER\n"
            "1\nAC1009\n"
            //INSBASE COMMAND
            "9\n$INSBASE\n"
            "10\n0.0\n"
            "20\n0.0\n"
            "30\n0.0\n"
            "0\nENDSEC\n"
            //START PAYLOAD
            "0\nSECTION\n"
            "2\nENTITIES\n";
        if (fprintf(this->f, HEADERA) <0) {
            this->err = str("DXF output file <", this->filename, ">: header could not be written!!!");
            return false;
        }
    } else {
        const char HEADERB[] =
            //DXF header
            "AutoCAD Binary DXF\x0d\x0a\x1a\x00" //"AutoCAD Binary DXF<CR><LF><SUB><NUL>"
            "\x00" "SECTION\x00"
            "\x02" "HEADER\x00"
            "\x09" "$ACADVER\x00"
            "\x01" "AC1009\x00"
            //INSBASE COMMAND
            "\x09" "$INSBASE\x00"
            "\x0a" "\x00\x00\x00\x00\x00\x00\x00\x00"
            "\x14" "\x00\x00\x00\x00\x00\x00\x00\x00"
            "\x1e" "\x00\x00\x00\x00\x00\x00\x00\x00"
            "\x00" "ENDSEC\x00"
            //START PAYLOAD
            "\x00" "SECTION\x00"
            "\x02" "ENTITIES\x00";
        if (fwrite(HEADERB, sizeof(char), sizeof(HEADERB) - 1, this->f) != (sizeof(HEADERB) - 1)) {
            this->err = str("DXF output file <", this->filename, ">: header could not be written!!!");
            return false;
        }
    }
    return true;
}

template<DXFWMode mode> bool DXFPathWriter<mode>::endWriter() {
    if (this->isopen && this->err.empty()) {
        if (ISASCII) {
            const char * ENDA =
                "0\nENDSEC\n"
                "0\nEOF";
            if (fprintf(this->f, ENDA) < 0) {
                this->err = str("DXF output file <", this->filename, ">: end could not be written!!!");
                return false;
            }
        } else {
            const char ENDB[] =
                "\x00" "ENDSEC\x00"
                "\x00" "EOF\x00";
            if (fwrite(ENDB, sizeof(char), sizeof(ENDB) - 1, this->f) != (sizeof(ENDB) - 1)) {
                this->err = str("DXF output file <", this->filename, ">: end could not be written!!!");
                return false;
            }
        }
    }
    return true;
}

template<DXFWMode mode> bool DXFPathWriter<mode>::specificClose() {
    bool ok = fclose(this->f) == 0;
    if (ok) {
        this->f = NULL;
    } else {
        this->err = str("DXF output file <", this->filename, ">: could not be closed!!!");
    }
    return ok;
}

template<DXFWMode mode> bool DXFPathWriter<mode>::writePathsSpecific(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    if (this->isopen && (!paths.empty())) {
        double width = this->generic_for_ntool ? (2 * radius) : 0.0;
        double elevation = this->generic_for_z ? z : 0.0;
        for (auto & path : paths) {
            if (path.empty()) continue;
            if (ISASCII) {
                if (fprintf(this->f,
                    "0\nPOLYLINE\n"
                    "8\n0\n" //the layer
                    "39\n%.20g\n" //width
                    "100\nAcDb2dPolyline\n"
                    "66\n1\n" //"entities follow flag"
                    "10\n0.0\n"
                    "20\n0.0\n"
                    "30\n%.20g\n" //elevation
                    "70\n%d\n", //isClosed
                    width, elevation, isClosed != 0) < 0) {
                    this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                    return false;
                };
            } else {
                const char POLYHEADER[] =
                    "\x00" "POLYLINE\x00"
                    "\x08" "0\x00" //the layer
                    "\x27"; //width
                if (fwrite(POLYHEADER, sizeof(char), sizeof(POLYHEADER) - 1, this->f) != (sizeof(POLYHEADER) - 1)) {
                    this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                    return false;
                }
                if (fwrite(&width, sizeof(double), 1, this->f) != 1) {
                    this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                    return false;
                }
                const char AcDb2dPolyline[] =
                    "\x64" "AcDb2dPolyline\x00"
                    "\x42" "\x01\x00" //"entities follow flag"
                    "\x0a" "\x00\x00\x00\x00\x00\x00\x00\x00"
                    "\x14" "\x00\x00\x00\x00\x00\x00\x00\x00"
                    "\x1e"; //elevation
                if (fwrite(AcDb2dPolyline, sizeof(char), sizeof(AcDb2dPolyline) - 1, this->f) != (sizeof(AcDb2dPolyline) - 1)) {
                    this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                    return false;
                }
                if (fwrite(&elevation, sizeof(double), 1, this->f) != 1) {
                    this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                    return false;
                };
                const char closedVal0[] = "\x46\x00\x00";
                const char closedVal1[] = "\x46\x01\x00";
                const char *closedVal = isClosed ? closedVal1 : closedVal0;
                const size_t closedValL = isClosed ? (sizeof(closedVal1) - 1) : (sizeof(closedVal0) - 1);
                //isClosed
                if (fwrite(closedVal, sizeof(char), closedValL, this->f) != closedValL) {
                    this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                    return false;
                }
            }
            for (auto &point : path) {
                if (ISASCII) {
                    if (fprintf(this->f,
                        "0\nVERTEX\n"
                        "8\n0\n" //the layer
                        //"62\n[COLORNUMBER]\n" //color number
                        //"420\n[RGB]\n" //number representing color value in 24-bit format
                        "100\nAcDb2dVertex\n" //subclass marker
                        "10\n%.20g\n" //X
                        "20\n%.20g\n" //Y
                        "30\n0\n", //Elevation (absolute or relative? if absolute, we have to repeat it here)
                        point.X*scaling,
                        point.Y*scaling
                        ) < 0) {
                        this->err = str("Error writing polyline points to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                        return false;
                    }
                } else {
                    double val;
                    const char VERTEX[] =
                        "\x00" "VERTEX\x00"
                        "\x08" "0\x00" //the layer
                        "\x64" "AcDb2dVertex\x00" //subclass marker
                        "\x0a"; //X
                    if (fwrite(VERTEX, sizeof(char), sizeof(VERTEX) - 1, this->f) != (sizeof(VERTEX) - 1)) {
                        this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                        return false;
                    }
                    val = point.X*scaling;
                    if (fwrite(&val, sizeof(double), 1, this->f) != 1) {
                        this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                        return false;
                    }
                    const char Y[] = "\x14";
                    if (fwrite(Y, sizeof(char), sizeof(Y) - 1, this->f) != (sizeof(Y) - 1)) { //Y
                        this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                        return false;
                    }
                    val = point.Y*scaling;
                    if (fwrite(&val, sizeof(double), 1, this->f) != 1) {
                        this->err = str("Error writing polyline header to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                        return false;
                    }
                    const char Z[] = "\x1e\x00\x00\x00\x00\x00\x00\x00\x00";
                    if (fwrite(Z, sizeof(char), sizeof(Z) - 1, this->f) != (sizeof(Z) - 1)) { //Elevation (absolute or relative? if absolute, we have to repeat it here)
                        this->err = str("Error writing polyline points to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                        return false;
                    }
                }
            }
            if (ISASCII) {
                if (fprintf(this->f, "0\nSEQEND\n") < 0) {
                    this->err = str("Error writing polyline end to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                    return false;
                }
            } else {
                const char SEQEND[] = "\x00" "SEQEND\x00";
                if (fwrite(SEQEND, sizeof(char), sizeof(SEQEND) - 1, this->f) != (sizeof(SEQEND) - 1)) {
                    this->err = str("Error writing polyline end to file <", this->filename, "> in DXFPathWriter::writePathsSpecific()");
                    return false;
                }
            }
        }
    }
    return true;
}
