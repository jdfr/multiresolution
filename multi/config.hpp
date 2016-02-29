#ifndef CONFIG_HEADER
#define CONFIG_HEADER

#include <vector>
#include <map>
#include <string>
#include <sstream>

//slurp file
std::string get_file_contents(const char *filename, bool binary, bool &ok);

std::vector<std::string> split(std::string &input, const char *escape_chars, const char *separator_chars, const char * quote_chars);
std::vector<std::string> split(std::string &input, const char *escape_chars, const char *separator_chars, const char * quote_chars, char startComment, char endComment);
inline std::vector<std::string> normalizedSplit(std::string &input) {return split(input, "", " \t\n\v\f\r", "\"", '#', '\n');}


/*this is a class to read key/value configuration pairs from a file with the following format (whitespace is trimmed both from keys and values):
    key1 : value1 ;
    key2 : value2 ;
*/
class Configuration {
    std::map<std::string, std::string> store;
public:
    bool has_err;
    std::string err;
    Configuration() : has_err(false) {}
    void load(const char *filename);
    template<typename S1, typename S2> void update(S1 key, S2 value) { store[std::string(std::move(key))] = std::move(value); }
    bool hasKey(const std::string key);
    bool hasKey(const char *      key);
    std::string getValue(const std::string key);
    std::string getValue(const char *      key);
};

typedef struct MetricFactors {
    std::string err;
    double input_to_internal, internal_to_input,
           input_to_slicer, slicer_to_internal,
           param_to_internal, internal_to_param,
           internal_to_nanoscribe, nanoscribe_to_internal;
    bool doparamscale;
    bool donanoscribescale;
    bool init_done;
    MetricFactors() : init_done(false) {}
    MetricFactors(Configuration &config, bool _doparamscale, bool _donanoscribe=false) { init(config, _doparamscale, _donanoscribe); };
    void init(Configuration &config, bool _doparamscale, bool _donanoscribe=false);
    void loadNanoscribeFactors(Configuration &config);
} MetricFactors;

//this is a hack to convert a series of things to a string
template<typename... Args> std::string str(Args... args) {
    std::ostringstream fmt;
    int dummy[sizeof...(Args)] = { (fmt << args, 0)... };
    return std::string(fmt.str());
}

#endif
