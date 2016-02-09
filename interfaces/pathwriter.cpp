#include "pathwriter.hpp"

bool PathWriter::start() {
    throw std::runtime_error("Base method PathWriter::close() should never be called!");
}
bool PathWriter::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    throw std::runtime_error("Base method PathWriter::writePaths() should never be called!");
}
bool PathWriter::close() {
    throw std::runtime_error("Base method PathWriter::close() should never be called!");
}

bool PathsFileWriter::start() {
    if (!isOpen) {
        if (!f_already_open) {
            f = fopen(filename.c_str(), "wb");
            if (f == NULL) {
                err = str("output pathsfile <", filename, ">: file could not be open");
                return false;
            }
        }
        isOpen = true;
        err = fileheader->writeToFile(f, false);
        if (!err.empty()) return false;
    }
    return true;
}

bool PathsFileWriter::writeNumRecords(int64 numRecords) {
    if (fwrite(&numRecords, sizeof(numRecords), 1, f) != 1) {
        err = str("output pathsfile <", filename, ">: could not write number of records");
        return false;
    }
}

bool PathsFileWriter::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    if (!isOpen) {
        if (!start()) return false;
    }
    PathCloseMode mode = isClosed ? PathLoop : PathOpen;
    err = writeSlice(f, SliceHeader(paths, mode, type, ntool, z, saveFormat, scaling), paths, mode);
    return err.empty();
}

bool PathsFileWriter::close() {
    bool ok = true;
    if (isOpen) {
        if (!f_already_open) {
            bool ok = fclose(f) == 0;
            if (ok) {
                f = NULL;
            } else {
                err = str("output pathsfile <", filename, ">: could not be closed!!!");
            }
        }
        isOpen = false;
    }
    return ok;
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
