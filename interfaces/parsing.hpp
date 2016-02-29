#ifndef  PARSING_HEADER
#define  PARSING_HEADER

/********************************************************
FEATURE-RICH FUNCTIONALITY FOR READING ARGUMENTS
*********************************************************/

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

typedef struct ContextToParseNanoOptions {
    MetricFactors &factors;
    po::variables_map *nanoGlobal;
    NanoscribeSpec &spec;
    ContextToParseNanoOptions(MetricFactors &f, po::variables_map *ng, NanoscribeSpec &s) : factors(f), nanoGlobal(ng), spec(s) {}
} ContextToParseNanoOptions;

/*Functions to parse the command line.

It would be really nice to replace our homegrown configuration file parser by boost:program_options's standard one.
Unfortunately, our values can contain # characters inside them (specifically, in the python expressions),
and boost:program_options would consider them as comment starts, even if they are within quotes, because 
boost::program_options strips comments before any lexical analysis. Thus, we will continue to use our clunky Configuration class.*/

const po::options_description *     globalOptions();
const po::options_description * perProcessOptions();

const po::options_description * nanoGlobalOptions();

std::vector<po::parsed_options> sortOptions(std::vector<const po::options_description*> &optss, const po::positional_options_description &posit, int positionalArgumentsIdx, const char *CommandLineOrigin, std::vector<std::string> &args);
po::parsed_options parseCommandLine(po::options_description &opts, const po::positional_options_description &posit, const char *CommandLineOrigin, std::vector<std::string> &args);

std::vector<std::string> getArgs(int argc, const char ** argv, int numskip=1);

void        parseNanoGlobal(ContextToParseNanoOptions *nanoContext);
std::string parseGlobal    (GlobalSpec &spec, po::parsed_options &optionList, MetricFactors &factors);
std::string parsePerProcess( MultiSpec &spec, po::parsed_options &optionList, MetricFactors &factors, ContextToParseNanoOptions *nanoContext=NULL);
std::string parseAll       ( MultiSpec &spec, po::parsed_options &globalOptionList, po::parsed_options &perProcOptionList, MetricFactors &factors, ContextToParseNanoOptions *nanoContext=NULL);
std::string parseAll       ( MultiSpec &spec, const char *CommandLineOrigin, std::vector<std::string> &args,               MetricFactors &factors, ContextToParseNanoOptions *nanoContext=NULL);

void composeParameterHelp(bool globals, bool perProcess, bool example, std::ostream &output);
void composeParameterHelp(bool globals, bool perProcess, bool example, std::string &output);

#endif