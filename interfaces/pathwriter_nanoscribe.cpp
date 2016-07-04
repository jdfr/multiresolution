#include "pathwriter_nanoscribe.hpp"
#include "pathwriter_multifile.tpp"

template class PathWriterMultiFile<SimpleNanoscribePathWriter>;

//the piezo can address a cubic volume whose length in all directions is this value
const double NanoscribePiezoRange = 300.0; //expressed in nanoscribe units

const char * GWLEXTENSION = ".gwl";

void SimpleNanoscribeConfig::init(MetricFactors &factors) {
    factor_internal_to_nanoscribe     = factors.internal_to_nanoscribe;
    factor_internal_to_input          = factors.internal_to_input;
    NanoscribePiezoRangeInternalUnits = NanoscribePiezoRange / factor_internal_to_nanoscribe;
    factor_input_to_nanoscribe        = factors.input_to_internal * factor_internal_to_nanoscribe;
    if (snapToGrid) {
        snapspec.gridstepX = snapspec.gridstepY = (double)gridStep;
        snapspec.shiftX    = snapspec.shiftY    = 0;
    }
    stagegotoFormatting = str("StageGoto%c ", nanoscribeNumberFormatting, "\n");
    pointLineFormatting = str(nanoscribeNumberFormatting, ' ', nanoscribeNumberFormatting, " 0\n");
    zoffsetFormatting   = str("AddZOffset ", nanoscribeNumberFormatting, "\n");
    addzdriveFormatting = str("AddZDrivePosition ", nanoscribeNumberFormatting, " %%change z block from %d to %d\nAddZOffset ", nanoscribeNumberFormatting, "\n");
}

SimpleNanoscribePathWriter::SimpleNanoscribePathWriter(PathSplitter &_splitter, std::shared_ptr<SimpleNanoscribeConfig> _config, std::string file, double epsilon, bool generic_ntool, bool generic_z)
    :
    splitter(_splitter),
    config(std::move(_config)),
    firstTime(true),
    current_z_block(0),
    overrideGalvoMode(splitter.config.wallAngle == 90.0) {
    resumeAtStart = false;
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

const bool BEGIN = true;
const bool END   = false;
std::string *getDelimString(std::shared_ptr<SimpleNanoscribeConfig> &config, int type, bool begin) {
    switch (type) {
        case PATHTYPE_TOOLPATH_PERIMETER : return begin ? &config->beginPerimeters : &config->endPerimeters;
        case PATHTYPE_TOOLPATH_SURFACE   : return begin ? &config->beginSurfaces   : &config->endSurfaces;
        case PATHTYPE_TOOLPATH_INFILLING :
        default:                           return begin ? &config->beginInfillings : &config->endInfillings;
    }
}
const char *getNameString(int type) {
    switch (type) {
        case PATHTYPE_TOOLPATH_PERIMETER : return "perimeter";
        case PATHTYPE_TOOLPATH_SURFACE   : return "surface";
        case PATHTYPE_TOOLPATH_INFILLING : return "infilling";
        default:                           return "unknown";
    }
}

bool SimpleNanoscribePathWriter::endWriter() {
    if (this->isopen && this->err.empty()) {
        if (!firstTime) {
            std::string *endtype = getDelimString(config, lastType, END);
            if (!endtype->empty()) {
                if (fputs(endtype->c_str(), this->f) < 0) {
                    err = str("error: cannot write ", getNameString(lastType), " starting for tool ", ntool, " in file ", this->filename);
                    return false;
                }
            }
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
    if (delegateWork) {
        int idx = findOrCreateSubwriter(type, radius, ntool, z);
        subwriters[idx]->square = std::move(square);
    }
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
            bool okX = lengthX <= config->maxSquareLen;
            bool okY = lengthY <= config->maxSquareLen;
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
                    clp::cInt semi_maxSquareLen_i;
                    switch (config->scanmode) {
                    case GalvoScanMode:
                        semi_maxSquareLen   = config->maxSquareLen / 2.0;
                        semi_maxSquareLen_i = (clp::cInt)std::round(semi_maxSquareLen);
                        if (galvoCenter) {
                            StagePosition = squareCenter;
                            StagePosNanoscribeX = StagePosition.X * config->factor_internal_to_nanoscribe;
                            StagePosNanoscribeY = StagePosition.Y * config->factor_internal_to_nanoscribe;
                        } else {
                            //double spareLenX = (config->maxSquareLen - (square[2].X - square[0].X));
                            //double spareLenY = (config->maxSquareLen - (square[2].Y - square[0].Y));
                            StagePosition.X = square[2].X - semi_maxSquareLen_i;
                            StagePosition.Y = square[2].Y - semi_maxSquareLen_i;
                            StagePosNanoscribeX = (square[2].X - semi_maxSquareLen) * config->factor_internal_to_nanoscribe;
                            StagePosNanoscribeY = (square[2].Y - semi_maxSquareLen) * config->factor_internal_to_nanoscribe;
                        }
                        currentWindowMin = currentWindowMax = StagePosition;
                        currentWindowMin.X -= semi_maxSquareLen_i;
                        currentWindowMin.Y -= semi_maxSquareLen_i;
                        currentWindowMax.X += semi_maxSquareLen_i;
                        currentWindowMax.Y += semi_maxSquareLen_i;
                        break;
                    case PiezoScanMode:
                    default:
                        double spareLen = (config->NanoscribePiezoRangeInternalUnits - config->maxSquareLen) / 2;
                        if (std::abs(spareLen) < epsilon) { //this should be replaced by a proper epsilon value specific for this case, instead of reusing the z_epsilon, which is in mesh file units 
                            spareLen = 0;
                        }
                        if (spareLen < 0) {
                            err = str("error: maxSquareLen was ", config->maxSquareLen, "but it should not be greater than ", config->NanoscribePiezoRangeInternalUnits, " in file ", this->filename);
                            return false;
                        }
                        //align the square to the upper right corner of the piezo range, so the Piezo range remains valid for as long as possible if slanted walls are used (wallAngle!=90)
                        clp::cInt desp  = (clp::cInt)std::round(-config->NanoscribePiezoRangeInternalUnits + spareLen);
                        StagePosition.X = square[2].X + desp;
                        StagePosition.Y = square[2].Y + desp;
                        StagePosNanoscribeX = StagePosition.X * config->factor_internal_to_nanoscribe;
                        StagePosNanoscribeY = StagePosition.Y * config->factor_internal_to_nanoscribe;
                        currentWindowMin = currentWindowMax = StagePosition;
                        desp = (clp::cInt)std::round(spareLen);
                        currentWindowMin.X += desp;
                        currentWindowMin.Y += desp;
                        desp = (clp::cInt)std::round(config->NanoscribePiezoRangeInternalUnits - spareLen);
                        currentWindowMax.X += desp;
                        currentWindowMax.Y += desp;
                    }
                    if (fprintf(this->f, config->stagegotoFormatting.c_str(), 'X', StagePosNanoscribeX) < 0) {
                        err = str("error writing stage displacement to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                        return false;
                    }
                    if (fprintf(this->f, config->stagegotoFormatting.c_str(), 'Y', StagePosNanoscribeY) < 0) {
                        err = str("error writing stage displacement to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                        return false;
                    }
                    if (firstTime) {
                    }
                }
                if (!setupStateForToolpaths(firstTime, type, ntool, z)) return false;
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
                PathSplitter subsplitter(subconf, splitter.res);
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
        lastZ     = z;
        lastNTool = ntool;
        lastType  = type;
        square.clear();
    }
    return true;
}

bool SimpleNanoscribePathWriter::setupStateForToolpaths(bool firstTime, int type, int ntool, double z) {
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
        std::string &begin = (*config->toolChanges)[ntool].first;
        if (!begin.empty()) {
            if (fputs(begin.c_str(), this->f) < 0) {
                err = str("error: cannot write tool starting for tool ", ntool, " in file ", this->filename);
                return false;
            }
        }
        //we suppose the script is starting at ZOffset=0
        if (current_z_block != 0) {
            double dz = current_z_block*NanoscribePiezoRange;
            if (fprintf(this->f, config->addzdriveFormatting.c_str(), dz, current_z_block, z_block, -dz) < 0) {
                err = str("error writing Z block change to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                return false;
            }
        }
        if (z != 0) {
            if (fprintf(this->f, config->zoffsetFormatting.c_str(), nanoscribe_z - current_z_block*NanoscribePiezoRange) < 0) {
                err = str("error writing Z block change to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                return false;
            }
        }
        std::string *begintype = getDelimString(config, type, BEGIN);
        if (!begintype->empty()) {
            if (fputs(begintype->c_str(), this->f) < 0) {
                err = str("error: cannot write ", getNameString(type), " starting for tool ", ntool, " in file ", this->filename);
                return false;
            }
        }
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
                double dz = (z_block - current_z_block)*NanoscribePiezoRange;
                if (fprintf(this->f, config->addzdriveFormatting.c_str(), dz, current_z_block, z_block, -dz) < 0) {
                    err = str("error writing Z block change to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                    return false;
                }
                current_z_block = z_block;
            }
            double nanoscribe_last_z = lastZ * config->factor_input_to_nanoscribe;
            if (fprintf(this->f, config->zoffsetFormatting.c_str(), nanoscribe_z - nanoscribe_last_z) < 0) {
                err = str("error writing Z block change to file <", this->filename, "> in SimpleNanoscribePathWriter::writePathsSpecific()");
                return false;
            }
        }
        if (type != lastType) {
            std::string *begin = getDelimString(config, type,     BEGIN);
            std::string *end   = getDelimString(config, lastType, END);
            if (!end->empty()) {
                if (fputs(end->c_str(), this->f) < 0) {
                    err = str("error: cannot write ", getNameString(lastType), " ending for tool ", lastNTool, " in file ", this->filename);
                    return false;
                }
            }
            if (!begin->empty()) {
                if (fputs(begin->c_str(), this->f) < 0) {
                    err = str("error: cannot write ", getNameString(type), " starting for tool ", ntool, " in file ", this->filename);
                    return false;
                }
            }
        }
    }
    return true;
}


NanoscribeSplittingPathWriter::NanoscribeSplittingPathWriter(bool resume, std::shared_ptr<FileHeader> _header, std::shared_ptr<ClippingResources> _res, MultiSpec &spec, SimpleNanoscribeConfigs _nanoconfigs, PathSplitterConfigs _splitterconfs, std::string file, bool generic_ntool, bool generic_z) {
    header      = std::move(_header);
    nanoconfigs = std::move(_nanoconfigs);
    double epsilon = spec.global.z_epsilon;
    SplittingSubPathWriterCreator callback = [this, epsilon](bool resume, int idx, PathSplitter& splitter, std::string &filename, std::string suffix, bool generic_type, bool generic_ntool, bool generic_z) {
        int i       = nanoconfigs.size() == 1 ? 0 : idx;
        double eps  = epsilon * nanoconfigs[i]->factor_internal_to_input;
        auto writer = std::make_shared<SimpleNanoscribePathWriter>(splitter, nanoconfigs[i], filename + suffix, eps, generic_ntool, generic_z);
        std::shared_ptr<PathWriter> result;
        if (!header) {
            result = writer;
        } else {
            auto delegator = std::make_shared<PathWriterDelegator>(filename+suffix);

            delegator->addWriter(std::make_shared<SimpleNanoscribePathWriter>(splitter, nanoconfigs[i], filename + suffix, eps, generic_ntool, generic_z), [](int type, int ntool, double z) {return true; });
            delegator->addWriter(std::make_shared<PathsFileWriter>(resume, filename + suffix, (FILE*)NULL, header, PATHFORMAT_INT64), [](int type, int ntool, double z) {return true; });
            
            result = delegator;
        }
        return result;
    };
    if (resume) {
        fprintf(stderr, "WARNING: nanoscribe files will not be resumed but overwritten\n");
        if (header) {
            fprintf(stderr, "         HOWEVER, their associated debug files will be correctly resumed\n");
        }
    }
    setup(resume, std::move(_res), (int)spec.numspecs, spec.global.config.get(), callback, std::move(_splitterconfs), std::move(file), true, generic_ntool, generic_z);
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

bool NanoscribeSplittingPathWriter::finishBeforeClose() {
    bool ok = true;
    std::vector<std::string> debugnames(states.size());
    for (auto state = states.begin(); state != states.end(); ++state) {
        int n = (int)(state - states.begin());
        std::string fname = state->filename_prefix;
        debugnames[n] = fname + ".debug";
        std::string &fname_debug = debugnames[n];
        addExtension(fname, GWLEXTENSION);
        addExtension(fname_debug, GWLEXTENSION);
        FILEOwner main(fname.c_str(), "wt");
        if (!main.isopen()) {
            err = str("main GWL output file <", fname, ">: file could not be open");
            return false;
        }
        FILEOwner debug(fname_debug.c_str(), "wt");
        if (!debug.isopen()) {
            err = str("main debug GWL output file <", fname_debug, ">: file could not be open");
            return false;
        }
        std::string &globalBegin = nanoconfigs[n]->beginGlobalScript;
        if (!globalBegin.empty()) {
            if (fputs(globalBegin.c_str(), main.f) < 0) {
                err = str("error: cannot write global script beggining for file ", fname);
                ok = false;
            }
            if (fputs(globalBegin.c_str(), debug.f) < 0) {
                err = str("error: cannot write global script beggining for file ", fname_debug);
                ok = false;
            }
        }
        if (ok) {
            auto &subwriters = state->subwriters;
            auto &splitter = state->splitter;
            double factor = nanoconfigs[n]->factor_internal_to_nanoscribe;
            auto includeNanoscribeSubscript = [&main, &debug, factor](SimpleNanoscribePathWriter* writer, clp::Path &square) {
                bool ok = true;
                if (!writer->firstTime) {
                    clp::DoublePoint center((square[0].X + square[2].X) / 2 * factor, (square[0].Y + square[2].Y) / 2 * factor);
                    ok = ok && fprintf(debug.f, "StageScanMode\n") >= 0;
                    ok = ok && writeSquare(debug.f, square, factor);
                    ok = ok && fprintf(debug.f, "Write\nTextPositionX %f\nTextPositionY %f\nWriteText \"%s\"\n", center.X, center.Y, writer->filename.c_str()) >= 0;
                    ok = ok && fprintf(debug.f, "include %s\n", writer->filename.c_str()) >= 0;
                    ok = ok && fprintf(main.f, "include %s\n", writer->filename.c_str()) >= 0;
                    if (writer->current_z_block != 0) {
                        double dz = -writer->current_z_block*NanoscribePiezoRange;
                        ok = fprintf(main.f, writer->config->addzdriveFormatting.c_str(), dz, writer->current_z_block, 0, -dz) >= 0;
                        ok = ok && (fprintf(debug.f, writer->config->addzdriveFormatting.c_str(), dz, writer->current_z_block, 0, -dz) >= 0);
                    }
                    if (writer->lastZ != 0) {
                        double dz = -writer->lastZ*writer->config->factor_input_to_nanoscribe - writer->current_z_block*NanoscribePiezoRange;
                        ok = fprintf(main.f, writer->config->zoffsetFormatting.c_str(), dz) >= 0;
                        ok = ok && fprintf(debug.f, writer->config->zoffsetFormatting.c_str(), dz) >= 0;
                    }
                }
                return ok;
            };
            auto includeSubWriter = [this, &includeNanoscribeSubscript, &fname, &subwriters, &splitter, &ok](int x, int y) {
                //we are sure that the object is a SimpleNanoscribePathWriter, as we created it with the callback defined in the constructor
                SimpleNanoscribePathWriter *writer;
                if (header) {
                    auto delegator = dynamic_cast<PathWriterDelegator*>(subwriters.at(x, y).get());
                    writer         = dynamic_cast<SimpleNanoscribePathWriter*>(delegator->getWriter(0));
                } else {
                    writer         = dynamic_cast<SimpleNanoscribePathWriter*>(subwriters.at(x, y).get());
                }
                if (writer->delegateWork) {
                    for (auto &subw : writer->subwriters) {
                        if (subw->delegateWork) {
                            err = str("IMPLEMENTATION error: Work delegation in MultiFilePathsWriter should not be nested at most than one level!");
                            ok = false;
                            return;
                        }
                        if (!includeNanoscribeSubscript(subw.get(), splitter.buffer.at(x, y).originalSquare)) {
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
                    for (int x = subwriters.numx - 1; x >= 0; --x) {
                        for (int y = subwriters.numy - 1; y >= 0; --y) {
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
                if (fputs(globalEnd.c_str(), main.f) < 0) {
                    err = str("error: cannot write global script ending for file ", fname);
                    ok = false;
                }
                if (fputs(globalEnd.c_str(), debug.f) < 0) {
                    err = str("error: cannot write global script ending for file ", fname_debug);
                    ok = false;
                }
            }
        }
        if (!main.close()) {
            err = str("GWL output file <", fname, ">: could not be closed!!!");
            ok = false;
        }
        if (!debug.close()) {
            err = str("GWL output file <", fname_debug, ">: could not be closed!!!");
            ok = false;
        }
        if (!ok) break;
    }
    if (ok && (states.size() > 1)) {
        std::string fname_debug = filename + ".debug";
        addExtension(fname_debug, GWLEXTENSION);
        FILEOwner debug(fname_debug.c_str(), "wt");
        if (!debug.isopen()) {
            err = str("main overall debug GWL output file <", fname_debug, ">: file could not be open");
            return false;
        }
        for (auto &debugname : debugnames) {
            if (fprintf(debug.f, "FindInterfaceAt 0\ninclude %s\n", debugname.c_str()) < 0) {
                err = str("error including file ", debugname.c_str(), " in file ", fname_debug);
                ok = false;
                break;
            }
        }
        if (!debug.close()) {
            err = str("GWL output file <", fname_debug, ">: could not be closed!!!");
            return false;
        }
    }
    return ok;
}
