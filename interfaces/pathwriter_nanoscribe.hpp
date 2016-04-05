#ifndef PATHWRITER_NANOSCRIBE_HEADER
#define PATHWRITER_NANOSCRIBE_HEADER

#include "pathwriter_multifile.hpp"

enum NanoscribeScanMode { PiezoScanMode, GalvoScanMode };
enum GalvoPositionMode { GalvoAlwaysCenter, GalvoMinimizeMovements };

typedef std::vector<std::pair<std::string, std::string>> ToolChanges;

typedef struct SimpleNanoscribeConfig {
    //these are configuration values, to be set by the user code
    double maxSquareLen; //in internal units

    NanoscribeScanMode scanmode;
    GalvoPositionMode galvomode;

    bool snapToGrid;
    clp::cInt gridStep; //in internal units

    std::string nanoscribeNumberFormatting;
    std::shared_ptr<ToolChanges> toolChanges;
    std::string beginScript, endScript;
    std::string beginGlobalScript, endGlobalScript;
    std::string beginPerimeters, endPerimeters;
    std::string beginInfillings, endInfillings;

    //these values are generated by calling init()
    double factor_internal_to_nanoscribe;
    SnapToGridSpec snapspec;
    std::string stagegotoFormatting;
    std::string pointLineFormatting;
    std::string zoffsetFormatting;
    std::string addzdriveFormatting;
    double NanoscribePiezoRangeInternalUnits;
    double factor_input_to_nanoscribe;
    double factor_internal_to_input;

    void init(MetricFactors &factors);
} SimpleNanoscribeConfig;

typedef std::vector<std::shared_ptr<SimpleNanoscribeConfig>> SimpleNanoscribeConfigs;

//convert toolpaths to Nanoscribe's GWL script, IN ONE VOLUME,
//(i.e., not suitable for toolpaths over a volume bigger than the volume addressable with the piezo/galvo).
//it is intended to be used with NanoscribeSplittingPathWriter
//ALSO: does not take into account the tool, except to write appropriate chunks of text in the scripts
//TODO: right now, handling of Z positions beyond the 300um limit is done transparently, but this will probably have to change to write in blocks by Z range
class SimpleNanoscribePathWriter : public PathWriterMultiFile<SimpleNanoscribePathWriter> {
    friend class NanoscribeSplittingPathWriter;
public:
    SimpleNanoscribePathWriter(PathSplitter &splitter, std::shared_ptr<SimpleNanoscribeConfig> _config, std::string file, double epsilon, bool generic_ntool, bool generic_z);
    std::shared_ptr<SimpleNanoscribePathWriter> createSubWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z);
    bool startWriter();
    bool endWriter();
    bool writePathsSpecific(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    bool specificClose();
    //WARNING: writePaths() should not be called directly for this class
    virtual bool writeEnclosedPaths(PathSplitter::EnclosedPaths &encl, int type, double radius, int ntool, double z, double scaling, bool isClosed);
protected:
    bool canBePrintedInCurrentWindow(clp::Paths &paths);
    bool setupStateForToolpaths(bool firstTime, int type, int ntool, double z);
    PathSplitter &splitter;
    std::shared_ptr<SimpleNanoscribeConfig> config;
    clp::IntPoint currentWindowMin;
    clp::IntPoint currentWindowMax;
    clp::IntPoint StagePosition;
    clp::Path square;
    clp::Path lastSquare;
    double lastZ;
    int lastNTool;
    int lastType;
    int current_z_block;
    bool firstTime;
    bool overrideGalvoMode;
};

//specialization of SplittingPathWriter for Nanoscribe: it writes a main GWL script that includes all other GWL scripts
class NanoscribeSplittingPathWriter : public SplittingPathWriter {
public:
    //either a single PathSplitterConfig, or one for each tool
    NanoscribeSplittingPathWriter(MultiSpec &_spec, SimpleNanoscribeConfigs _nanoconfigs, PathSplitterConfigs _splitterconfs, std::string file, bool generic_ntool = true, bool generic_z = true);
    virtual ~NanoscribeSplittingPathWriter() { close(); }
    virtual bool finishAfterClose(); //use this method to write a main script that includes all subscripts
protected:
    SimpleNanoscribeConfigs nanoconfigs;
};

#endif