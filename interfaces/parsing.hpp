#ifndef  PARSING_HEADER
#define  PARSING_HEADER

/********************************************************
FEATURE-RICH FUNCTIONALITY FOR READING ARGUMENTS
*********************************************************/

/*It would be really nice to replace our homegrown configuration file parser by boost:program_options's standard one.
Unfortunately, our values can contain # characters inside them (specifically, in the python expressions),
and boost:program_options would consider them as comment starts, even if they are within quotes, because 
boost::program_options strips comments before any lexical analysis. Thus, we will continue to use our clunky Configuration class.*/

#include "pathwriter_dxf.hpp"
#include "pathwriter_nanoscribe.hpp"

//in BOOST 1.55, boost::program_options causes a warning about use of deprecated std::auto_ptr in gcc 5.2 (auto_ptr will dissapear in c++17)
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <boost/program_options.hpp>
#ifdef __GNUC__
#pragma   GCC diagnostic pop
#endif

namespace po = boost::program_options;

typedef struct NanoscribeSpec {
    bool useSpec;
    bool isGlobal;
    bool generic_ntool, generic_z;
    std::string filename;
    SimpleNanoscribeConfigs nanos;
    std::vector<PathSplitterConfig> splits;
    std::shared_ptr<ToolChanges> toolChanges;
    NanoscribeSpec() : useSpec(false) {}
} NanoscribeSpec;

typedef std::pair<int, po::parsed_options> OptionsByTool;
typedef struct OptionsByToolSpec {
    std::vector<OptionsByTool> optionsByTool;
    int maxProcess;
} OptionsByToolSpec;

class ParserLocalAndGlobal {
public:
    std::shared_ptr<po::options_description> globalDescription;
    std::shared_ptr<po::options_description>  localDescription;
    ParserLocalAndGlobal(std::shared_ptr<po::options_description> g, std::shared_ptr<po::options_description> l) : globalDescription(std::move(g)), localDescription(std::move(l)) {}
    void setParsedOptions(po::parsed_options globals, po::parsed_options allPerProcess);
    void setParsedOptions(std::vector<std::string> &args, const char *CommandLineOrigin);
    virtual void globalCallback() {};                                             //redefined in subclasses
    virtual void perProcessCallback(int k, po::variables_map &processOptions) {}; //redefined in subclasses
    virtual void finishCallback() {};
protected:
    po::variables_map globalOptions;
    OptionsByToolSpec perProcessOptions;
    void separatePerProcess(po::parsed_options &allPerProcess);
};

struct ContextToParseNanoOptions;
typedef struct ContextToParseNanoOptions ContextToParseNanoOptions;

class ParserAllLocalAndGlobal : public ParserLocalAndGlobal {
public:
    MetricFactors  &factors;
    MultiSpec      &spec;
    NanoscribeSpec *nanoSpec;
    ParserAllLocalAndGlobal(MetricFactors &f, MultiSpec &s, std::shared_ptr<po::options_description> g, std::shared_ptr<po::options_description> l, NanoscribeSpec *n = NULL) : factors(f), spec(s), nanoSpec(n), ParserLocalAndGlobal(std::move(g), std::move(l)) {}
    ParserAllLocalAndGlobal(MetricFactors &f, MultiSpec &s, NanoscribeSpec *n = NULL);
    virtual void globalCallback();
    virtual void perProcessCallback(int k, po::variables_map &processOptions);
    virtual void finishCallback();
protected:
    std::shared_ptr<ContextToParseNanoOptions> nanoContext;
};

po::options_description         globalOptionsGenerator(bool alsoNano);
po::options_description     perProcessOptionsGenerator(bool alsoNano);
po::options_description     nanoGlobalOptionsGenerator();
po::options_description nanoPerProcessOptionsGenerator();

std::vector<po::parsed_options> sortOptions(std::vector<const po::options_description*> &optss, const po::positional_options_description &posit, int positionalArgumentsIdx, const char *CommandLineOrigin, std::vector<std::string> &args);

std::vector<std::string> getArgs(int argc, const char ** argv, int numskip=1);

#endif