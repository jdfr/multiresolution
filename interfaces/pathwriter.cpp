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

bool PathWriterDelegator::start() {
    if (!isopen) {
        for (auto &sub : subs) {
            if (!sub.second->start()) {
                err = str("Error starting subwriter ", sub.second->filename, ": ", sub.second->err);
                return false;
            }
        }
        isopen = true;
    }
    return true;
}

bool PathWriterDelegator::close() {
    bool ok = true;
    for (auto &sub : subs) {
        if (!sub.second->close()) {
            err = str("Error closing subwriter ", sub.second->filename, ": ", sub.second->err);
            ok = false;
        }
    }
    return ok;
}

bool PathWriterDelegator::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    if (!isopen) {
        if (!start()) return false;
    }
    for (auto &sub : subs) {
        if (sub.first(type, ntool, z)) {
            if (!sub.second->writePaths(paths, type, radius, ntool, z, scaling, isClosed)) {
                err = str("Error writing paths to ", sub.second->filename, ": ", sub.second->err);
                return false;
            }
        }
    }
    return true;
}

bool PathWriterDelegator::writeEnclosedPaths(PathSplitter::EnclosedPaths &encl, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    if (!isopen) {
        if (!start()) return false;
    }
    for (auto &sub : subs) {
        if (sub.first(type, ntool, z)) {
            if (!sub.second->writeEnclosedPaths(encl, type, radius, ntool, z, scaling, isClosed)) {
                err = str("Error writing paths to ", sub.second->filename, ": ", sub.second->err);
                return false;
            }
        }
    }
    return true;
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
            int numToSkip = fileheader->numRecordsOffset();
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

bool SplittingPathWriter::setup(std::shared_ptr<ClippingResources> _res, int ntools, Configuration *_cfg, SplittingSubPathWriterCreator &callback, PathSplitterConfigs splitterconfs, std::string file, bool generic_type, bool generic_ntool, bool generic_z) {
    filename = std::move(file);
    numtools = ntools;
    isopen   = false;
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
    //initialize stuff
    states.reserve(splitterconfs.size());
    std::string prefix;
    for (auto conf = splitterconfs.begin(); conf != splitterconfs.end(); ++conf) {
        int n = (int)(conf - splitterconfs.begin());
        if (non_generic_ntool_name) {
            prefix = str(filename, ".N", n);
        } else {
            prefix = filename; //assign each iteration because the state takes ownership of the string
        }

        states.emplace_back(_res, prefix, std::move(*conf));// , _cfg);
        auto &splitter   = states.back().splitter;
        auto &subwriters = states.back().subwriters;

        //initialize splitter
        if (!splitter.setup()) {
            err = str("Error while setting up the ", n, "-th path splitter: ", states.back().splitter.err);
            return false;
        }

        //initialize subwriters
        auto numx = splitter.numx;
        auto numy = splitter.numy;
        subwriters.reset(numx, numy);
        int num0x = numx < 10 ? 1 : (int)std::ceil(std::log10(numx-1));
        int num0y = numy < 10 ? 1 : (int)std::ceil(std::log10(numy-1));
        for (int x = 0; x < numx; ++x) {
            for (int y = 0; y < numy; ++y) {
                std::string suffix = str(".N", n, '.', std::setw(num0x), std::setfill('0'), x, '.', std::setw(num0y), std::setfill('0'), y);
                subwriters.at(x, y) = callback(n, splitter, filename, std::move(suffix), generic_type, generic_ntool, generic_z);
            }
        }
    }

    return true;
}

bool SplittingPathWriter::start() {
    if (!isopen) {
        for (auto &state : states) {
            for (auto &subw : state.subwriters.data) {
                if (!subw->start()) {
                    err = str("Error starting subwriter ", subw->filename, ": ", subw->err);
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
    for (auto &state : states) {
        for (auto &subw : state.subwriters.data) {
            if (!subw->close()) {
                err = str("Error closing subwriter ", subw->filename, ": ", subw->err);
                ok = false;
            }
        }
    }
    if (ok) {
        ok = finishAfterClose();
    }
    return ok;
}

bool SplittingPathWriter::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    int idx        = (states.size() == 1) ? 0 : ntool;
    auto &splitter = states[idx].splitter;
    auto &subw     = states[idx].subwriters;
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

