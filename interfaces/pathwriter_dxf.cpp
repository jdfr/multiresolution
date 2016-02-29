#include "pathwriter_dxf.hpp"
#include "pathwriter_multifile.tpp"

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
