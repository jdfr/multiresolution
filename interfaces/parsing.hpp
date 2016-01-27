#ifndef  PARSING_HEADER
#define  PARSING_HEADER

/********************************************************
FEATURE-RICH FUNCTIONALITY FOR READING ARGUMENTS
*********************************************************/

#include "spec.hpp"

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

/*Functions to parse the command line.

It would be really nice to replace our homegrown configuration file parser by boost:program_options's standard one.
Unfortunately, our values can contain # characters inside them (specifically, in the python expressions),
and boost:program_options would consider them as comment starts, even if they are within quotes, because 
boost::program_options strips comments before any lexical analysis. Thus, we will continue to use our clunky Configuration class.*/

const po::options_description *     globalOptions();
const po::options_description * perProcessOptions();

std::vector<po::parsed_options> sortOptions(std::vector<const po::options_description*> &optss, const po::positional_options_description &posit, int positionalArgumentsIdx, const char *CommandLineOrigin, std::vector<std::string> &args);
po::parsed_options parseCommandLine(po::options_description &opts, const po::positional_options_description &posit, const char *CommandLineOrigin, std::vector<std::string> &args);
inline double getScale(MetricFactors &factors) { return factors.doparamscale ? factors.param_to_internal : 0.0; }

std::vector<std::string> getArgs(int argc, const char ** argv, int numskip=1);

inline double    getScaled(double    val, double scale, bool doscale) { return doscale ? val*scale : val; }
inline clp::cInt getScaled(clp::cInt val, double scale, bool doscale) { return doscale ? (clp::cInt)(val*scale) : val; }

std::string parseGlobal    (GlobalSpec &spec, po::parsed_options &optionList, double scale = 0.0);
std::string parsePerProcess( MultiSpec &spec, po::parsed_options &optionList, double scale = 0.0);
std::string parseAll(MultiSpec &spec, po::parsed_options &globalOptionList, po::parsed_options &perProcOptionList, double scale = 0.0);
std::string parseAll(MultiSpec &spec, const char *CommandLineOrigin, std::vector<std::string> &args, double scale = 0.0);

void composeParameterHelp(bool globals, bool perProcess, bool example, std::ostream &output);
void composeParameterHelp(bool globals, bool perProcess, bool example, std::string &output);

#endif