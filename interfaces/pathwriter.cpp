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
            f = fopen(filename.c_str(), resumeAtStart ? "r+b" : "wb");
            if (f == NULL) {
                err = str("output pathsfile <", filename, ">: file could not be open");
                return false;
            }
        }
        isOpen = true;
        if (resumeAtStart) {
            FileHeader fheader; //attention: this will break if the file's FileHeader has a different size than the current one!
            err = fheader.readFromFile(f);
            if (!err.empty()) return false;
            numRecords = fheader.numRecords;
            //fseek(f, 0 , SEEK_END); //fast but brittle
            int64 totalSize;
            for (int i = 0; i < numRecords; ++i) { //slower but more robust
                if (fread(&totalSize,  sizeof(totalSize),  1, f) != 1) { err = str("output pathsfile <", filename, ">: could not read size of record ", i); return false; }
                long toSkip = (long)(totalSize - sizeof(totalSize));
                if (toSkip>0) if (fseek(f, toSkip, SEEK_CUR)!=0) { err = str("output pathsfile <", filename, ">: could skip record ", i); return false; }
                /*there is no way to portably truncate a file using the stdio.h interface.
                However, we assume that it is not actually necessary to do it, because we will eventually overwrite all the contents
                (and if not, the final FileHeader's numToRecords is anyway used to iterate over the file's contents, so it is not relevant if there is some gargabe at the end of the file)*/
            }
        } else {
            err = fileheader->writeToFile(f, false);
            if (fwrite(&numRecords, sizeof(numRecords), 1, f) != 1) {
                err = str("output pathsfile <", filename, ">: could not write number of records");
                return false;
            }
            if (!err.empty()) return false;
        }
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

bool SplittingPathWriter::setup(bool resume, std::shared_ptr<ClippingResources> _res, int ntools, Configuration *_cfg, SplittingSubPathWriterCreator &callback, PathSplitterConfigs splitterconfs, std::string file, bool generic_type, bool generic_ntool, bool generic_z) {
    alreadyfinished = false;
    resumeAtStart = resume;
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
                subwriters.at(x, y) = callback(resumeAtStart, n, splitter, filename, std::move(suffix), generic_type, generic_ntool, generic_z);
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
    if (alreadyfinished) return true;
    bool ok = true;
    if (!finishBeforeClose()) ok = false;
    for (auto &state : states) {
        for (auto &subw : state.subwriters.data) {
            if (!subw->close()) {
                err = str("Error closing subwriter ", subw->filename, ": ", subw->err);
                ok = false;
            }
        }
    }
    alreadyfinished = true;
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

