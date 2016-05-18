#include "pathwriter_nanoscribe.hpp"
#include "parsing.hpp"
#include "apputil.hpp"
#include <iostream>
#include <iomanip>
#include <limits>

std::string openFile(std::string &pathsfilename, FILEOwner &f, FileHeader &header) {
    if (!f.open(pathsfilename.c_str(), "rb")) { return str("Could not open file ", pathsfilename); }

    std::string err = header.readFromFile(f.f);
    if (!err.empty()) { return str("Error reading file header for ", pathsfilename, ": ", err); }
    return std::string();
}

template<typename Function> std::string processToolpaths(std::string &pathsfilename, Function function) {
    FILEOwner f;
    FileHeader fileheader;
    std::string err = openFile(pathsfilename, f, fileheader);
    if (!err.empty()) return err;

    SliceHeader sliceheader;
    IOPaths iop(f.f);
    clp::Paths output;
    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string err = sliceheader.readFromFile(f.f);
        if (!err.empty()) { return str("Error reading ", currentRecord, "-th slice header: ", err); }

        if (!((sliceheader.type == PATHTYPE_TOOLPATH_PERIMETER) || (sliceheader.type == PATHTYPE_TOOLPATH_INFILLING))) {
            fseek(f.f, (long)(sliceheader.totalSize - sliceheader.headerSize), SEEK_CUR);
            continue;
        }

        if (sliceheader.saveFormat == PATHFORMAT_INT64) {
            if (!iop.readClipperPaths(output)) {
                err = str("error reading integer paths from record ", currentRecord, " in file ", pathsfilename, ": error <", iop.errs[0].message, "> in ", iop.errs[0].function);
                break;
            }
        } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE) {
            //TODO: modify the code so we do not have to convert back and forth the coordinates in this case!
            if (!iop.readDoublePaths(output, 1 / sliceheader.scaling)) {
                err = str("error reading double paths from record ", currentRecord, " in file ", pathsfilename, ": error <", iop.errs[0].message, "> in ", iop.errs[0].function);
                break;
            }
        } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE_3D) {
            err = str("In file ", pathsfilename, ", for path ", currentRecord, ", save mode is 3D, but we currently cannot convert 3D paths to GWL\n");
            break;
        } else {
            err = str("In file ", pathsfilename, ", for path ", currentRecord, ", save mode not understood: ", sliceheader.saveFormat, "\n");
            break;
        }

        err = function(fileheader, currentRecord, sliceheader, output);
        output.clear();
        if (!err.empty()) break;
    }
    return err;
}

typedef struct MainSpec {
    std::vector<const char *>                             opts_names;
    std::vector<std::shared_ptr<po::options_description>> opts;
    std::vector<const po::options_description*>           opts_naked;
    std::vector<po::parsed_options>                       optsBySystem;
    int mainOptsIdx;
    int globalOptsIdx;
    int perProcOptsIdx;
    MainSpec();
    void slurpAllOptions(int argc, const char ** argv);
    void usage();
    inline po::variables_map getMap(int idx) {
        po::variables_map map;
        try {
            po::store(optsBySystem[idx], map);
        } catch (std::exception &e) {
            throw po::error(str("Error reading ", opts_names[idx], ": ", e.what()));
        }
        return map;
    }
    inline bool nonEmptyOpts(int idx) {
        return !optsBySystem[idx].options.empty();
    }
} MainSpec;

MainSpec::MainSpec() {
    opts.reserve(3);
    opts_names.reserve(3);
    opts_naked.reserve(3);
    optsBySystem.reserve(3);

    mainOptsIdx = (int)opts.size();
    opts_names.push_back("Main options");
    opts.emplace_back(std::make_shared<po::options_description>(opts_names.back()));
    opts.back()->add_options()
        ("help",
            "produce help message")
        ("config",
            po::value<std::string>()->default_value("config.txt"),
            "configuration input file (if no file is provided, it is assumed to be config.txt)")
        ("load",
            po::value<std::string>()->value_name("filename"),
            "input file in *.paths format")
        ("z-epsilon",
            po::value<double>()->default_value(1e-6)->value_name("z_epsilon"),
            "Z values are considered to be the same if they differ less than this, in the mesh file units")
        ("bb",
            po::value<std::vector<double>>()->multitoken(),
            "bounding box in the XY plane, in mesh file units. If it is not provided, it will be computed from the input file. This bounding box is used to set up the partitioning of the toolpaths into several domains, one for each regional GWL script.")
        ;
    addResponseFileOption(*opts.back());

    globalOptsIdx = (int)opts.size();
    opts_names.push_back("Global options");
    opts.emplace_back(std::make_shared<po::options_description>(std::move(nanoGlobalOptionsGenerator())));

    perProcOptsIdx = (int)opts.size();
    opts_names.push_back("Per-process options");
    opts.emplace_back(std::make_shared<po::options_description>(std::move(nanoPerProcessOptionsGenerator())));

    for (auto &opt : opts) {
        opts_naked.push_back(opt.get());
    }
}

void MainSpec::slurpAllOptions(int argc, const char ** argv) {
    auto args = getArgs(argc, argv);
    optsBySystem = sortOptions(opts_naked, po::positional_options_description(), mainOptsIdx, NULL, args);
}

void MainSpec::usage() {
    std::cout << "Utility to convert toolpaths from the native *.paths file format to GWL scripts. The options are exactly the same as in the main command line application.\n  If there is no ambiguity, options can be specified as prefixes of their full names.\n";
    for (auto opt : opts) {
        std::cout << *opt << "\n";
    }
}

int main(int argc, const char** argv) {
    TimeMeasurements tm;
    tm.measureTime();
    std::shared_ptr<Configuration> config = std::make_shared<Configuration>();
    MetricFactors factors;
    NanoscribeSpec nanoSpec;
    std::string pathsfile;
    std::shared_ptr<MultiSpec> multispec; //this is a skeleton, we only use it for the fields used by the nanoscribe writing machinery
    std::shared_ptr<FileHeader> fileheader; //this is for debugging purposes
    BBox bb;
    bool specified_bb;
    try {
        const bool doscale = true;
        MainSpec mainSpec;
        if (argc == 1) {
            mainSpec.usage();
            return 1;
        }
        mainSpec.slurpAllOptions(argc, argv);

        po::variables_map mainOpts = mainSpec.getMap(mainSpec.mainOptsIdx);

        if (mainOpts.count("help")) {
            mainSpec.usage();
            return 1;
        }

        if (mainOpts.count("load")) {
            pathsfile = std::move(mainOpts["load"].as<std::string>());
        } else {
            fprintf(stderr, "Error: load parameter has not been specified!");
            return -1;
        }
        
        const bool doDebug = false;
        if (doDebug) {
            fileheader = std::make_shared<FileHeader>();
            FILEOwner f;
            std::string err = openFile(pathsfile, f, *fileheader);
            if (!err.empty()) {
                fprintf(stderr, err.c_str());
                return -1;
            }
        }

        std::string configfilename = std::move(mainOpts["config"].as<std::string>());

        if (!fileExists(configfilename.c_str())) { fprintf(stderr, "Could not open config file %s!!!!", configfilename.c_str()); return -1; }

        config->load(configfilename.c_str());

        if (config->has_err) { fprintf(stderr, config->err.c_str()); return -1; }

        factors.init(*config, doscale);
        if (!factors.err.empty()) { fprintf(stderr, factors.err.c_str()); return -1; }

        multispec = std::make_shared<MultiSpec>(config);
        multispec->global.z_epsilon = mainOpts["z-epsilon"].as<double>();
        {
            const bool applyMotionPlanner = true;
            ParserNanoLocalAndGlobal parser(applyMotionPlanner, *config, factors, nanoSpec, mainSpec.opts[mainSpec.globalOptsIdx], mainSpec.opts[mainSpec.perProcOptsIdx]);
            parser.setParsedOptions(mainSpec.optsBySystem[mainSpec.globalOptsIdx], mainSpec.optsBySystem[mainSpec.perProcOptsIdx]);
            if (!nanoSpec.useSpec) { fprintf(stderr, "Error: --nanoscribe parameter was not specified"); return -1; }
            multispec->numspecs = parser.ntools;
        }

        specified_bb = mainOpts.count("bb") != 0;
        if (specified_bb) {
            std::vector<double> vals = std::move(mainOpts["bb"].as<std::vector<double>>());
            if (vals.size() < 4) {
                fprintf(stderr, "Error: if the --bb option is specified, at least four values have to be supplied!\n");
                return -1;
            }
            bb.minx = (clp::cInt)(vals[0] * factors.input_to_internal);
            bb.miny = (clp::cInt)(vals[1] * factors.input_to_internal);
            bb.maxx = (clp::cInt)(vals[2] * factors.input_to_internal);
            bb.maxy = (clp::cInt)(vals[3] * factors.input_to_internal);
        }

    } catch (std::exception &e) {
        fprintf(stderr, e.what()); return -1;
    }

    try {

        if (!specified_bb) {
            int ntoolpaths = 0;
            //have to read the whole input file to find the BB!
            bb.minx = bb.miny = (std::numeric_limits<clp::cInt>::max)();
            bb.maxx = bb.maxy = (std::numeric_limits<clp::cInt>::min)();
            auto getBB = [&bb, &ntoolpaths](FileHeader &fileheader, int idx, SliceHeader &header, clp::Paths &paths) {
                if (!paths.empty()) ++ntoolpaths;
                for (auto &path : paths) {
                    for (auto &point : path) {
                        if (bb.minx > point.X) bb.minx = point.X;
                        if (bb.maxx < point.X) bb.maxx = point.X;
                        if (bb.miny > point.Y) bb.miny = point.Y;
                        if (bb.maxy < point.Y) bb.maxy = point.Y;
                    }
                }
                return std::string();
            };
            std::string err = processToolpaths(pathsfile, getBB);
            if ( err.empty() && (ntoolpaths == 0)) { err = str("Error: input file ", pathsfile, " has no non-empty toolpaths\n"); }
            if (!err.empty())                      { fprintf(stderr, err.c_str()); return -1; }
        }

        for (auto & split : nanoSpec.splits) {
            split.min.X = bb.minx;
            split.min.Y = bb.miny;
            split.max.X = bb.maxx;
            split.max.Y = bb.maxy;
        }

        std::shared_ptr<ClippingResources> clipres = std::make_shared<ClippingResources>(std::shared_ptr<MultiSpec>());
        NanoscribeSplittingPathWriter pathsplitter(false, fileheader, clipres, *multispec, std::move(nanoSpec.nanos), std::move(nanoSpec.splits), std::move(nanoSpec.filename), nanoSpec.generic_ntool, nanoSpec.generic_z);

        if (!pathsplitter.err.empty()) { fprintf(stderr, "%s\n", pathsplitter.err.c_str()); return -1; }

        int numtools = (int)multispec->numspecs;

        auto saveToolpaths = [&pathsplitter, numtools](FileHeader &fileheader, int idx, SliceHeader &header, clp::Paths &paths) {
            fprintf(stdout, "  -> reading record %d/%ld...\n", idx, fileheader.numRecords);
            if (header.ntool < 0)         return str("Error: toolpath ", idx, " has negative ntool!\n");
            if (header.ntool >= numtools) return str("Error: toolpath ", idx, " has ntool ", header.ntool, "-th but command line arguments specified numtools=", numtools, "\n");
            if (!pathsplitter.writePaths(paths, (int)header.type, 0.0, (int)header.ntool, header.z, header.scaling, false)) return pathsplitter.err;
            return std::string();
        };

        std::string err = processToolpaths(pathsfile, saveToolpaths);

        if (!err.empty()) { fprintf(stderr, err.c_str()); return -1; }

        pathsplitter.close();

        if (!err.empty()) { fprintf(stderr, err.c_str()); return -1; }

    } catch (std::exception &e) {
        fprintf(stderr, "Unhandled exception while writing the GWL scripts.\n   Exception    type: %s\n   Exception message: %s\n", typeid(e).name(), e.what()); return -1;
        return -1;
    }

    tm.measureTime();
    tm.printLastMeasurement(stdout, "TOTAL TIME: CPU %f, WALL TIME %f\n");
    return 0;
}


