#include "pathwriter.hpp"
#include <iomanip>

void addExtension(std::string &filename, std::string ext) {
    bool file_ends_in_ext = filename.length() > ext.length();
    if (file_ends_in_ext) {
        auto charf = filename.end() - ext.length();
        for (auto chare = ext.begin(); chare != ext.end(); ++chare) {
            file_ends_in_ext = (tolower(*charf) == tolower(*chare));
            if (!file_ends_in_ext) {
                break;
            }
            ++charf;
        }
    }
    if (!file_ends_in_ext) {
        filename += ext;
    }
}

bool PathWriter::start() {
    throw std::runtime_error("Base method PathWriter::close() should never be called!");
}
bool PathWriter::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    throw std::runtime_error("Base method PathWriter::writePaths() should never be called!");
}
bool PathWriter::writeEnclosedPaths(PathSplitter::EnclosedPaths &encl, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    //this is the most sensible default definition for this method
    return writePaths(encl.paths, type, radius, ntool, z, scaling, isClosed);
}
bool PathWriter::close() {
    throw std::runtime_error("Base method PathWriter::close() should never be called!");
}

bool PathsFileWriter::start() {
    if (!isOpen) {
        if (numRecordsSet && (numRecords < 0)) {
            err = str("output pathsfile <", filename, ">: the number of records was incorrectly set");
            return false;
        }
        if (!f_already_open) {
            addExtension(this->filename, ".paths");
            f = fopen(filename.c_str(), "wb");
            if (f == NULL) {
                err = str("output pathsfile <", filename, ">: file could not be open");
                return false;
            }
        }
        isOpen = true;
        err = fileheader->writeToFile(f, false);
        if (fwrite(&numRecords, sizeof(numRecords), 1, f) != 1) {
            err = str("output pathsfile <", filename, ">: could not write number of records");
            return false;
        }
        if (!err.empty()) return false;
    }
    return true;
}

bool PathsFileWriter::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    if (!isOpen) {
        if (!start()) return false;
    }
    PathCloseMode mode = isClosed ? PathLoop : PathOpen;
    err = writeSlice(f, SliceHeader(paths, mode, type, ntool, z, saveFormat, scaling), paths, mode);
    if (err.empty() && !numRecordsSet) ++numRecords;
    return err.empty();
}

bool PathsFileWriter::close() {
    bool ok = true;
    if (isOpen) {
        if (!numRecordsSet) {
            int numToSkip = (int) (sizeof(double) * (2 + (fileheader->numtools * (fileheader->useSched ? 2 : 1))));
            if (fseek(f, numToSkip, SEEK_SET) == 0) {
                if (fwrite(&numRecords, sizeof(numRecords), 1, f) != 1) {
                    ok = false;
                    err = "fseek failed: could not write numRecords to file " + filename;
                }
            } else {
                ok = false;
                err = "fwrite failed: could not write numRecords to file " + filename;
            }
        }
        if (!f_already_open) {
            bool newok = fclose(f) == 0;
            if (newok) {
                f = NULL;
            } else {
                if (ok) {
                    ok = false;
                    err = str("output pathsfile <", filename, ">: could not be closed!!!");
                } else {
                    err += ". Also, the file could not be closed!!!!";
                }
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

template<typename T> void PathWriterMultiFile<T>::init(std::string file, const char * _extension, double _epsilon, bool _generic_for_type, bool _generic_for_ntool, bool _generic_for_z) {
    epsilon           = _epsilon;
    generic_for_ntool = _generic_for_ntool;
    generic_for_z     = _generic_for_z;
    generic_for_type  = _generic_for_type;
    generic_all       = this->generic_for_ntool && this->generic_for_z && this->generic_for_type;
    delegateWork      = !this->generic_all;
    filename          = std::move(file);
    extension         = _extension;
    isopen            = false;
}

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
    subwriters.push_back(static_cast<T*>(this)->createSubWriter(newfilename, epsilon, generic_for_type, generic_for_ntool, generic_for_z));
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
    return true;
}

template<typename T> bool PathWriterMultiFile<T>::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    if (delegateWork) {
        int idx = findOrCreateSubwriter(type, radius, ntool, z);
        bool ret = subwriters[idx]->writePaths(paths, type, radius, ntool, z, scaling, isClosed);
        if (!ret) err = subwriters[idx]->err;
        return ret;
    }
    if (!isopen) {
        addExtension(filename, extension);
        if (!static_cast<T*>(this)->startWriter()) return false;
    }
    return static_cast<T*>(this)->writePathsSpecific(paths, type, radius, ntool, z, scaling, isClosed);
}

template<typename T> bool PathWriterMultiFile<T>::writeToAll(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    bool ret = true;
    if (delegateWork) {
        bool ret = true;
        for (auto &w : subwriters) {
            bool newret = w->writePaths(paths, type, radius, ntool, z, scaling, isClosed);
            if (!newret) err = w->err;
            ret = ret && newret;
        }
    }
    if (isopen) {
        bool newret = static_cast<T*>(this)->writePathsSpecific(paths, type, radius, ntool, z, scaling, isClosed);
        ret = ret && newret;
    }
    return ret;
}


template<typename T> bool PathWriterMultiFile<T>::close() {
    bool ok = true;
    if (!subwriters.empty()) {
        for (auto &w : subwriters) {
            if (!w->close()) {
                err = w->err;
                ok = false;
            }
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
    this->init(std::move(file), ".dxf", _epsilon, _generic_type, _generic_ntool, _generic_z);
}

template<DXFWMode mode> std::shared_ptr<DXFPathWriter<mode>> DXFPathWriter<mode>::createSubWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z) {
    return std::make_shared<DXFPathWriter<mode>>(std::move(file), epsilon, generic_type, _generic_ntool, _generic_z);
}

static_assert(sizeof(char) == 1, "To write binary DXF files we expect char to be 1 byte long!");

template<DXFWMode mode> bool DXFPathWriter<mode>::startWriter() {
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


bool SplittingPathWriter::setup(MultiSpec &_spec, SplittingSubPathWriterCreator &callback, PathSplitterConfigs splitterconfs, std::string file, bool generic_type, bool generic_ntool, bool generic_z) {
    numtools = (int)_spec.numspecs;
    isopen = false;
    //make sanity checks
    if (splitterconfs.empty()) {
        err = "Error: There are no splitting configurations";
        return false;
    }
    auto numsconfs = splitterconfs.size();
    if ((numsconfs > 1) && (numsconfs != numtools)) {
        err = "Error: if more than one splitting configuration is provided, the number of configurations must match the number of tools";
        return false;
    }
    justone = numsconfs == 1;
    if (numtools == 1) {
        generic_ntool = true;
    }
    //flag to decide if we must include the tool number in the file path (to avoid possible filename conflicts)
    bool non_generic_ntool_name = !justone && (numtools > 1)  && generic_ntool;
    printf("!justone=%d, numtools>1=%d, generic_ntool=%d, non_generic_ntool_name=%d\n", !justone, (numtools > 1), generic_ntool, non_generic_ntool_name);
    //initialize stuff
    splitters.reserve(splitterconfs.size());
    subwriters.reserve(splitterconfs.size());
    for (auto conf = splitterconfs.begin(); conf != splitterconfs.end(); ++conf) {
        std::string ntoolname;
        if (non_generic_ntool_name) {
            auto n = conf - splitterconfs.begin();
            ntoolname = str(".N", n);
        }

        //initialize splitters
        splitters.emplace_back(std::move(*conf));
        if (!splitters.back().setup()) {
            auto n = conf - splitterconfs.begin();
            err = str("Error while setting up the ", n, "-th path splitter: ", splitters.back().err);
            return false;
        }

        //initialize subwriters
        auto numx = splitters.back().numx;
        auto numy = splitters.back().numy;
        subwriters.emplace_back(Matrix<std::shared_ptr<PathWriter>>(numx, numy));
        int num0x = (int)std::ceil(std::log10(numx-1));
        int num0y = (int)std::ceil(std::log10(numy-1));
        for (int x = 0; x < numx; ++x) {
            for (int y = 0; y < numy; ++y) {
                int coded_y = ((x % 2) == 0) ? y : numy - y - 1;
                std::string newfile = str(filename, ntoolname, '.', std::setw(num0x), std::setfill('0'), x, '.', std::setw(num0y), std::setfill('0'), coded_y);
                subwriters.back().at(x, y) = callback(splitters.back(), std::move(newfile), generic_type, generic_ntool, generic_z);
            }
        }
    }

    return true;
}

bool SplittingPathWriter::start() {
    if (!isopen) {
        for (auto &sub : subwriters) {
            for (auto &subsub : sub.data) {
                if (!subsub->start()) {
                    err = str("Error starting subwriter ", subsub->filename, ": ", subsub->err);
                    return false;
                }
            }
        }
        isopen = true;
    }
    return true;
}

bool SplittingPathWriter::close() {
    bool ok = true;
    for (auto &sub : subwriters) {
        for (auto &subsub : sub.data) {
            if (!subsub->close()) {
                err = str("Error closing subwriter ", subsub->filename, ": ", subsub->err);
                ok = false;
            }
        }
    }
    return ok;
}

bool SplittingPathWriter::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    int idx        = (subwriters.size() == 1) ? 0 : ntool;
    auto &splitter = splitters[idx];
    auto &subw     = subwriters[idx];
    if (!splitter.processPaths(paths, isClosed, z, scaling)) {
        err = str("For paths of tool ", ntool, ", z ", z, "type ", type, ": ", splitter.err);
        return false;
    }
    auto numx = splitter.numx;
    auto numy = splitter.numy;
    for (int x = 0; x < numx; ++x) {
        for (int y = 0; y < numy; ++y) {
            if (!subw.at(x,y)->writeEnclosedPaths(splitter.buffer.at(x,y), type, radius, ntool, z, scaling, isClosed)) {
                err = str("For paths of tool ", ntool, ", z ", z, "type ", type, ", error while writing to split(", x, ',', y, "): ", splitter.err);
                return false;
            }
        }
    }
    return true;
}

