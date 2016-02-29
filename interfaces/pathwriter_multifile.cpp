#include "pathwriter_multifile.hpp"

/************************************************
****************GENERIC CODE*********************
*************************************************/

template<typename T> void PathWriterMultiFile<T>::init(std::string file, const char * _extension, double _epsilon, bool _generic_for_type, bool _generic_for_ntool, bool _generic_for_z) {
    epsilon = _epsilon;
    generic_for_ntool = _generic_for_ntool;
    generic_for_z = _generic_for_z;
    generic_for_type = _generic_for_type;
    generic_all = this->generic_for_ntool && this->generic_for_z && this->generic_for_type;
    delegateWork = !this->generic_all;
    filename = std::move(file);
    extension = _extension;
    isopen = false;
}

template<typename T> bool PathWriterMultiFile<T>::matchZNtool(int _type, int _ntool, double _z) {
    return generic_all || ((generic_for_ntool || (ntool == _ntool)) &&
        (generic_for_z || (std::fabs(z - _z) < epsilon)) &&
        (generic_for_type || (_type == type)));
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
    if (!generic_for_ntool) N = str(".N", _ntool);
    if (!generic_for_z)     Z = str(".Z", _z);
    std::string newfilename = str(filename, Type, N, Z, ".dxf");
    subwriters.push_back(static_cast<T*>(this)->createSubWriter(newfilename, epsilon, generic_for_type, generic_for_ntool, generic_for_z));
    subwriters.back()->type = _type;
    subwriters.back()->radius = _radius;
    subwriters.back()->ntool = _ntool;
    subwriters.back()->z = _z;
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

/************************************************
********CODE SPECIFIC TO DXF WRITING*************
*************************************************/

template class PathWriterMultiFile<DXFAsciiPathWriter>;
template class PathWriterMultiFile<DXFBinaryPathWriter>;

template class DXFPathWriter<DXFAscii>;
template class DXFPathWriter<DXFBinary>;

template<DXFWMode mode> DXFPathWriter<mode>::DXFPathWriter(std::string file, double _epsilon, bool _generic_type, bool _generic_ntool, bool _generic_z) {
    this->init(std::move(file), ".dxf", _epsilon, _generic_type, _generic_ntool, _generic_z);
}

template<DXFWMode mode> std::shared_ptr<DXFPathWriter<mode>> DXFPathWriter<mode>::createSubWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z) {
    return std::make_shared<DXFPathWriter<mode>>(std::move(file), epsilon, generic_type, _generic_ntool, _generic_z);
}

static_assert(sizeof(char) == 1, "To write binary DXF files we expect char to be 1 byte long!");

template<DXFWMode mode> bool DXFPathWriter<mode>::startWriter() {
    this->f = fopen(this->filename.c_str(), (mode == DXFAscii) ? "wt" : "wb");
    if (this->f == NULL) {
        this->err = str("DXF output file <", this->filename, ">: file could not be open");
        return false;
    }
    this->isopen = true;
    if (mode == DXFAscii) {
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
        if (mode == DXFAscii) {
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
            if (mode == DXFAscii) {
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
                if (mode == DXFAscii) {
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
            if (mode == DXFAscii) {
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

/************************************************
****CODE SPECIFIC TO NANOSCRIBE WRITING**********
*************************************************/

//the piezo can address a cubic volume whose length in all directions is this value
const double NanoscribePiezoRange = 300.0; //expressed in nanoscribe units

const char * GWLEXTENSION = ".gwl";

void SimpleNanoscribeConfig::init(MetricFactors &factors) {
    factor_internal_to_nanoscribe     = factors.internal_to_nanoscribe;
    NanoscribePiezoRangeInternalUnits = NanoscribePiezoRange / factor_internal_to_nanoscribe;
    factor_input_to_nanoscribe        = factors.input_to_internal * factor_internal_to_nanoscribe;
    if (snapToGrid) {
        snapspec.gridstepX            = snapspec.gridstepY = (double)gridStep;
        snapspec.shiftX               = snapspec.shiftY = 0;
    }
    pointLineFormatting               = str(nanoscribeNumberFormatting, ' ', nanoscribeNumberFormatting, " 0\n");
    zoffsetFormatting                 = str("ZOffset ", nanoscribeNumberFormatting, "\n");
    addzdriveFormatting               = str("AddZDrivePosition ", nanoscribeNumberFormatting, " %%change z block from %d to %d\nZOffset 0\n");
}

SimpleNanoscribePathWriter::SimpleNanoscribePathWriter(PathSplitter &_splitter, std::shared_ptr<SimpleNanoscribeConfig> _config, std::string file, double epsilon, bool generic_ntool, bool generic_z)
    :
    splitter(_splitter),
    config(std::move(_config)),
    firstTime(true),
    current_z_block(0),
    overrideGalvoMode(splitter.config.wallAngle == 90.0) {
    bool generic_type = true;
    this->init(std::move(file), GWLEXTENSION, epsilon, generic_type, generic_ntool, generic_z);
}

std::shared_ptr<SimpleNanoscribePathWriter> SimpleNanoscribePathWriter::createSubWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z) {
    return std::make_shared<SimpleNanoscribePathWriter>(splitter, config, std::move(file), epsilon, _generic_ntool, _generic_z);
}

bool SimpleNanoscribePathWriter::startWriter() {
    this->f = fopen(this->filename.c_str(), "wt");
    if (this->f == NULL) {
        this->err = str("GWL output file <", this->filename, ">: file could not be open");
        return false;
    }
    this->isopen = true;
    return true;
}

bool SimpleNanoscribePathWriter::endWriter() {
    if (this->isopen && this->err.empty()) {
        if (!firstTime) {
            std::string &end = (*config->toolChanges)[lastNTool].second;
            if (!end.empty()) {
                if (fputs(end.c_str(), this->f) < 0) {
                    err = str("error: cannot write tool starting for tool ", ntool, " in file ", this->filename);
                    return false;
                }
            }
            if (!config->endScript.empty()) {
                if (fputs(config->endScript.c_str(), this->f) < 0) {
                    err = str("error: cannot write script ending for file ", this->filename);
                    return false;
                }
            }
        }
    }
    return true;
}

bool SimpleNanoscribePathWriter::specificClose() {
    bool ok = fclose(this->f) == 0;
    if (ok) {
        this->f = NULL;
    } else {
        this->err = str("GWL output file <", this->filename, ">: could not be closed!!!");
    }
    return ok;
}

bool SimpleNanoscribePathWriter::writeEnclosedPaths(PathSplitter::EnclosedPaths &encl, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    square = std::move(encl.actualSquare);
    return this->writePaths(encl.paths, type, radius, ntool, z, scaling, isClosed);
}

bool SimpleNanoscribePathWriter::canBePrintedInCurrentWindow(clp::Paths &paths) {
    bool insideForX = (square[0].X >= currentWindowMin.X) && (square[2].X <= currentWindowMax.X);
    bool insideForY = (square[0].Y >= currentWindowMin.Y) && (square[2].Y <= currentWindowMax.Y);
    if (insideForX && insideForY) return true;
    //there is a chance that the square is just too big, let's test the contents
    BBox bb = getBB(paths);
    insideForX = (bb.minx >= currentWindowMin.X) && (bb.maxx <= currentWindowMax.X);
    insideForY = (bb.miny >= currentWindowMin.Y) && (bb.maxy <= currentWindowMax.Y);
    return insideForX && insideForY;
}

bool SimpleNanoscribePathWriter::writePathsSpecific(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    if (this->isopen && (!paths.empty())) {
        if (!square.empty() && !paths.empty()) {
            clp::cInt lengthX = square[2].X - square[0].X;
            clp::cInt lengthY = square[2].Y - square[0].Y;
            bool okX = lengthX < config->maxSquareLen;
            bool okY = lengthY < config->maxSquareLen;
            if (okX && okY) {
                clp::IntPoint     squareCenter;
                bool galvoCenter = (config->scanmode == GalvoScanMode) && ((config->galvomode == GalvoAlwaysCenter) || overrideGalvoMode);
                if (galvoCenter) {
                    squareCenter.X = square[0].X + (square[2].X - square[0].X) / 2;
                    squareCenter.Y = square[0].Y + (square[2].Y - square[0].Y) / 2;
                    if (!firstTime) {
                        clp::IntPoint lastSquareCenter;
                        lastSquareCenter.X = lastSquare[0].X + (lastSquare[2].X - lastSquare[0].X) / 2;
                        lastSquareCenter.Y = lastSquare[0].Y + (lastSquare[2].Y - lastSquare[0].Y) / 2;
                        galvoCenter = (squareCenter.X != lastSquareCenter.X) || (squareCenter.Y != lastSquareCenter.Y);
                    }
                }
                if (firstTime || galvoCenter || !canBePrintedInCurrentWindow(paths)) {
                    //determine Piezo positioning
                    double StagePosNanoscribeX;
                    double StagePosNanoscribeY;
                    double semi_maxSquareLen;
                    switch (config->scanmode) {
                    case GalvoScanMode:
                        semi_maxSquareLen = config->maxSquareLen / 2.0;
                        if (galvoCenter) {
                            StagePosition = squareCenter;
                            StagePosNanoscribeX = StagePosition.X * config->factor_internal_to_nanoscribe;
                            StagePosNanoscribeY = StagePosition.Y * config->factor_internal_to_nanoscribe;
                        } else {
                            //double spareLenX = (config->maxSquareLen - (square[2].X - square[0].X));
                            //double spareLenY = (config->maxSquareLen - (square[2].Y - square[0].Y));
                            StagePosition.X = square[2].X - (clp::cInt)semi_maxSquareLen;
                            StagePosition.Y = square[2].Y - (clp::cInt)semi_maxSquareLen;
                            StagePosNanoscribeX = (square[2].X - semi_maxSquareLen) * config->factor_internal_to_nanoscribe;
                            StagePosNanoscribeY = (square[2].Y - semi_maxSquareLen) * config->factor_internal_to_nanoscribe;
                        }
                        currentWindowMin = currentWindowMax = StagePosition;
                        currentWindowMin.X -= (clp::cInt)semi_maxSquareLen;
                        currentWindowMin.Y -= (clp::cInt)semi_maxSquareLen;
                        currentWindowMax.X += (clp::cInt)semi_maxSquareLen;
                        currentWindowMax.Y += (clp::cInt)semi_maxSquareLen;
                        break;
                    case PiezoScanMode:
                    default:
                        double spareLen = (config->NanoscribePiezoRangeInternalUnits - config->maxSquareLen) / 2;
                        if (spareLen < 0) {
                            err = str("error: maxSquareLen was ", config->maxSquareLen, "but it should not be greater than ", config->NanoscribePiezoRangeInternalUnits, " in file ", this->filename);
                            return false;
                        }
                        //align the square to the upper right corner of the piezo range, so the Piezo range remains valid for as long as possible if slanted walls are used (wallAngle!=90)
                        StagePosition.X = square[2].X + (clp::cInt)(-config->NanoscribePiezoRangeInternalUnits + spareLen);
                        StagePosition.Y = square[2].Y + (clp::cInt)(-config->NanoscribePiezoRangeInternalUnits + spareLen);
                        StagePosNanoscribeX = StagePosition.X * config->factor_internal_to_nanoscribe;
                        StagePosNanoscribeY = StagePosition.Y * config->factor_internal_to_nanoscribe;
                        currentWindowMin = currentWindowMax = StagePosition;
                        currentWindowMin.X += (clp::cInt)spareLen;
                        currentWindowMin.Y += (clp::cInt)spareLen;
                        currentWindowMax.X += (clp::cInt)(config->NanoscribePiezoRangeInternalUnits - spareLen);
                        currentWindowMax.Y += (clp::cInt)(config->NanoscribePiezoRangeInternalUnits - spareLen);
                    }
                    if (fprintf(this->f, "StageGotoX %.20g\n", StagePosNanoscribeX) < 0) {
                        err = str("error writing stage displacement to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                        return false;
                    }
                    if (fprintf(this->f, "StageGotoY %.20g\n", StagePosNanoscribeY) < 0) {
                        err = str("error writing stage displacement to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                        return false;
                    }
                    if (firstTime) {
                        if (!config->beginScript.empty()) {
                            if (fputs(config->beginScript.c_str(), this->f) < 0) {
                                err = str("error: cannot write script beggining for file ", this->filename);
                                return false;
                            }
                        }
                        const char *scanmode = config->scanmode == GalvoScanMode ? "GalvoScanMode\n" : "PiezoScanMode\n";
                        if (fprintf(this->f, scanmode) < 0) {
                            err = str("error: cannot write scan mode for file ", this->filename);
                            return false;
                        }
                    }
                }
                if (!setupNToolAndZ(firstTime, ntool, z)) return false;
                //offset paths by stage position
                for (auto &path : paths) {
                    for (auto &point : path) {
                        point.X -= StagePosition.X;
                        point.Y -= StagePosition.Y;
                    }
                }
                if (config->snapToGrid) {
                    simpleSnapPathsToGrid(paths, config->snapspec);
                }
                //write paths
                int n = (int)paths.size();
                int k = 0;
                for (auto &path : paths) {
                    if (fprintf(this->f, "%%Write path %d/%d\n", k, n) < 0) {
                        err = str("error writing starting comment for path ", k, '/', n, " to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                        return false;
                    }
                    for (auto &point : path) {
                        if (fprintf(this->f, config->pointLineFormatting.c_str(), point.X*config->factor_internal_to_nanoscribe, point.Y*config->factor_internal_to_nanoscribe) < 0) {
                            err = str("error writing point command for path ", k, '/', n, " to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                            return false;
                        }
                    }
                    if (fprintf(this->f, "write\n") < 0) {
                        err = str("error writing end command for path ", k, '/', n, " to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                        return false;
                    }
                    ++k;
                }
                lastSquare = std::move(square);
            } else {
                /*this is rather inefficient, we will optimize it (both in terms of C++ code and of generated GWL code) if necessary.
                In particular:
                -going back and forth between several subareas is not technically the best solution (we
                should store the code in separated files for greater flexibility), but let's do it that
                way for now
                -it would be better to take into account the whole stack at all Z values to decide how
                to partition the squares, instead of doing it anew for each Z plane
                */
                auto subconf = splitter.config;
                subconf.wallAngle = 90.0;
                subconf.useOrigin = false;
                subconf.min = square[0];
                subconf.max = square[2];
                PathSplitter subsplitter(subconf);
                bool old_overrideGalvoMode = overrideGalvoMode;
                overrideGalvoMode = true;
                if (!subsplitter.setup()) {
                    err = subsplitter.err;
                    return false;
                }
                if (!subsplitter.processPaths(paths, isClosed, z, scaling)) {
                    err = subsplitter.err;
                    return false;
                }
                clp::Path original(square);
                for (auto &subenclosed : subsplitter.buffer.data) {
                    square = std::move(subenclosed.actualSquare);
                    writePathsSpecific(subenclosed.paths, type, radius, ntool, z, scaling, isClosed);
                }
                square = std::move(original);
                overrideGalvoMode = old_overrideGalvoMode;
                //in this code path we do not update lastSquare, since the actual squares are set in the nested calls to this method
            }
        }
        firstTime = false;
        lastZ = z;
        lastNTool = ntool;
        square.clear();
    }
    return true;
}

bool SimpleNanoscribePathWriter::setupNToolAndZ(bool firstTime, int ntool, double z) {
    double nanoscribe_z = 0.0;
    int z_block = 0;
    bool differentZ = z != lastZ;
    if (firstTime || differentZ) {
        nanoscribe_z = z * config->factor_input_to_nanoscribe;
        if (nanoscribe_z < 0.0) {
            err = str("error: z cannot be negative, but it was ", nanoscribe_z, " in um for file ", this->filename);
            return false;
        }
        z_block = (int)std::floor(nanoscribe_z / NanoscribePiezoRange);
        if (firstTime) {
            current_z_block = z_block;
        }
    }
    if (firstTime) {
        std::string &begin = (*config->toolChanges)[ntool].first;
        if (!begin.empty()) {
            if (fputs(begin.c_str(), this->f) < 0) {
                err = str("error: cannot write tool starting for tool ", ntool, " in file ", this->filename);
                return false;
            }
        }
        //we just suppose that we are starting with ZOffset==0, at the correct Z position
    } else {
        if (ntool != lastNTool) {
            std::string &end = (*config->toolChanges)[lastNTool].second;
            std::string &begin = (*config->toolChanges)[ntool].first;
            if (!end.empty()) {
                if (fputs(end.c_str(), this->f) < 0) {
                    err = str("error: cannot write tool ending for tool ", lastNTool, " in file ", this->filename);
                    return false;
                }
            }
            if (!begin.empty()) {
                if (fputs(begin.c_str(), this->f) < 0) {
                    err = str("error: cannot write tool starting for tool ", ntool, " in file ", this->filename);
                    return false;
                }
            }
        }
        if (differentZ) {
            if (z_block != current_z_block) {
                if (fprintf(this->f, config->addzdriveFormatting.c_str(), (z_block - current_z_block)*NanoscribePiezoRange, current_z_block, z_block) < 0) {
                    err = str("error writing Z block change to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                    return false;
                }
                current_z_block = z_block;
            } else {
                double nanoscribe_last_z = lastZ * config->factor_input_to_nanoscribe;
                if (fprintf(this->f, config->zoffsetFormatting.c_str(), nanoscribe_z - current_z_block*NanoscribePiezoRange) < 0) {
                    err = str("error writing Z block change to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                    return false;
                }
            }
        }
    }
    return true;
}


NanoscribeSplittingPathWriter::NanoscribeSplittingPathWriter(MultiSpec &spec, SimpleNanoscribeConfigs _nanoconfigs, PathSplitterConfigs _splitterconfs, std::string file, bool generic_ntool, bool generic_z) {
    nanoconfigs = std::move(_nanoconfigs);
    double epsilon = spec.global.z_epsilon;
    SplittingSubPathWriterCreator callback = [this, epsilon](int idx, PathSplitter& splitter, std::string filename, bool generic_type, bool generic_ntool, bool generic_z) {
        return std::make_shared<SimpleNanoscribePathWriter>(splitter, nanoconfigs[nanoconfigs.size() == 1 ? 0 : idx], std::move(filename), epsilon, generic_ntool, generic_z);
    };
    setup(spec, callback, std::move(_splitterconfs), std::move(file), true, generic_ntool, generic_z);
}

bool writeSquare(FILE *f, clp::Path square, double factor) {
    return  fprintf(f, "%f %f 0\n%f %f 0\n%f %f 0\n%f %f 0\n%f %f 0\n",
        square[0].X*factor, square[0].Y*factor,
        square[1].X*factor, square[1].Y*factor,
        square[2].X*factor, square[2].Y*factor,
        square[3].X*factor, square[3].Y*factor,
        square[0].X*factor, square[0].Y*factor
        ) >= 0;
}

bool NanoscribeSplittingPathWriter::finishAfterClose() {
    bool ok = true;
    std::vector<std::string> debugnames(states.size());
    for (auto state = states.begin(); state != states.end(); ++state) {
        int n = (int)(state - states.begin());
        std::string fname        = state->filename_prefix;
        debugnames[n]            = fname+".debug";
        std::string &fname_debug = debugnames[n];
        addExtension(fname,       GWLEXTENSION);
        addExtension(fname_debug, GWLEXTENSION);
        FILE *main = fopen(fname.c_str(), "wt");
        if (main == NULL) {
            err = str("main GWL output file <", fname, ">: file could not be open");
            return false;
        }
        FILE *debug = fopen(fname_debug.c_str(), "wt");
        if (debug == NULL) {
            err = str("main debug GWL output file <", fname_debug, ">: file could not be open");
            fclose(main);
            return false;
        }
        std::string &globalBegin = nanoconfigs[n]->beginGlobalScript;
        if (!globalBegin.empty()) {
            if (fputs(globalBegin.c_str(), main) < 0) {
                err = str("error: cannot write global script beggining for file ", fname);
                ok = false;
            }
            if (fputs(globalBegin.c_str(), debug) < 0) {
                err = str("error: cannot write global script beggining for file ", fname_debug);
                ok = false;
            }
        }
        if (ok) {
            auto &subwriters = state->subwriters;
            auto &splitter   = state->splitter;
            double factor    = nanoconfigs[n]->factor_internal_to_nanoscribe;
            auto includeNanoscribeSubscript = [main, debug, factor](SimpleNanoscribePathWriter* writer, clp::Path &square) {
                bool ok = true;
                if (!writer->firstTime) {
                    if (writer->current_z_block != 0) {
                        ok =        fprintf(main,  writer->config->addzdriveFormatting.c_str(), -writer->current_z_block*NanoscribePiezoRange, writer->current_z_block, 0) >= 0;
                        ok = ok && (fprintf(debug, writer->config->addzdriveFormatting.c_str(), -writer->current_z_block*NanoscribePiezoRange, writer->current_z_block, 0) >= 0);
                    } else if (writer->lastZ != 0) {
                        ok =       fprintf(main,  "ZOffset 0\n") >= 0;
                        ok = ok && fprintf(debug, "ZOffset 0\n") >= 0;
                    }
                    clp::DoublePoint center((square[0].X + square[2].X) / 2 * factor, (square[0].Y + square[2].Y) / 2 * factor);
                    ok = ok && fprintf(debug, "StageScanMode\n") >= 0;
                    ok = ok && writeSquare(debug, square, factor);
                    ok = ok && fprintf(debug, "Write\nTextPositionX %f\nTextPositionY %f\nWriteText \"%s\"\n", center.X, center.Y, writer->filename.c_str()) >= 0;
                    ok = ok && fprintf(debug, "include %s\n", writer->filename.c_str()) >= 0;
                    ok = ok && fprintf(main,  "include %s\n", writer->filename.c_str()) >= 0;
                }
                return ok;
            };
            auto includeSubWriter = [this, &includeNanoscribeSubscript, &fname, &subwriters, &splitter, &ok](int x, int y) {
                //we are sure that the object is a SimpleNanoscribePathWriter, as we created it with the callback defined in the constructor
                auto writer = reinterpret_cast<SimpleNanoscribePathWriter*>(subwriters.at(x, y).get());
                if (writer->delegateWork) {
                    for (auto &subw : writer->subwriters) {
                        if (subw->delegateWork) {
                            err = str("IMPLEMENTATION error: Work delegation in MultiFilePathsWriter should not be nested at most than one level!");
                            ok = false;
                            return;
                        }
                        if (!includeNanoscribeSubscript(subw.get(), splitter.buffer.at(x,y).originalSquare)) {
                            err = str("error: cannot write commands for including sub-file ", subw->filename, " into file ", fname);
                            ok = false;
                            return;
                        }
                    }
                } else {
                    if (!includeNanoscribeSubscript(writer, splitter.buffer.at(x, y).originalSquare)) {
                        err = str("error: cannot write commands for including file ", writer->filename, " into file ", fname);
                        ok = false;
                        return;
                    }
                }
            };
            //order of execution depends on the use of angled walls:
            //    -for vertical walls: do in inverse order, and put as the outer loop variable the one with smalles effective domain size, on the hope that this will minimize the number of movements (unless in GalvoScanMode combined with GalvoAlwaysCenter, then the reordering will be ineffectual)
            //    -for angled walls: conventional order, as the correct stitching relies in the blocks being printed in their conventional order
            if (splitter.angle90) {
                auto spareLenX = nanoconfigs[n]->maxSquareLen - splitter.originalSize.X;
                auto spareLenY = nanoconfigs[n]->maxSquareLen - splitter.originalSize.Y;
                bool outerLoopOverX = splitter.originalSize.X < splitter.originalSize.Y;
                if (outerLoopOverX) {
                    for (int x = subwriters.numx-1; x >= 0; --x) {
                        for (int y = subwriters.numy-1; y >= 0; --y) {
                            includeSubWriter(x, y);
                        }
                    }
                } else {
                    for (int y = subwriters.numy - 1; y >= 0; --y) {
                        for (int x = subwriters.numx - 1; x >= 0; --x) {
                            includeSubWriter(x, y);
                        }
                    }
                }
            } else {
                for (int x = 0; x < subwriters.numx; ++x) {
                    for (int y = 0; y < subwriters.numy; ++y) {
                        includeSubWriter(x, y);
                    }
                }
            }
        }
        if (ok) {
            std::string &globalEnd = nanoconfigs[n]->endGlobalScript;
            if (!globalEnd.empty()) {
                if (fputs(globalEnd.c_str(), main) < 0) {
                    err = str("error: cannot write global script ending for file ", fname);
                    ok = false;
                }
                if (fputs(globalEnd.c_str(), debug) < 0) {
                    err = str("error: cannot write global script ending for file ", fname_debug);
                    ok = false;
                }
            }
        }
        if (fclose(main) != 0) {
            err = str("GWL output file <", fname, ">: could not be closed!!!");
            ok  = false;
        }
        if (fclose(debug) != 0) {
            err = str("GWL output file <", fname_debug, ">: could not be closed!!!");
            ok  = false;
        }
        if (!ok) break;
    }
    if (ok && (states.size() > 1)) {
        std::string fname_debug = filename+".debug";
        addExtension(fname_debug, GWLEXTENSION);
        FILE *debug = fopen(fname_debug.c_str(), "wt");
        if (debug == NULL) {
            err = str("main overall debug GWL output file <", fname_debug, ">: file could not be open");
            return false;
        }
        for (auto &debugname : debugnames) {
            if (fprintf(debug, "FindInterfaceAt 0\ninclude %s\n", debugname.c_str()) < 0) {
                err = str("error including file ", debugname.c_str(), " in file ", fname_debug);
                ok  = false;
                break;
            }
        }
        if (fclose(debug) != 0) {
            err = str("GWL output file <", fname_debug, ">: could not be closed!!!");
            return false;
        }
    }
    return ok;
}

