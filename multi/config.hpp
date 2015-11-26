#ifndef CONFIG_HEADER
#define CONFIG_HEADER

#include <vector>
#include <map>
#include <string>
#include <sstream>

//slurp file
std::string get_file_contents(const char *filename, bool &ok);

std::vector<std::string> split(std::string &input, const char *escape_chars, const char *separator_chars, const char * quote_chars);
std::vector<std::string> split(std::string &input, const char *escape_chars, const char *separator_chars, const char * quote_chars, char startComment, char endComment);

/*this is a class to read key/value configuration pairs from a file with the following format (whitespace is trimmed both from keys and values):
    key1 : value1 ;
    key2 : value2 ;
*/
class Configuration {
    std::map<std::string, std::string> store;
public:
    bool has_err;
    std::string err;
    Configuration(const char *filename);
    template<typename S1, typename S2> void update(S1 key, S2 value) { store[std::string(std::move(key))] = std::move(value); }
    bool hasKey(const std::string key);
    bool hasKey(const char *      key);
    std::string getValue(const std::string key);
    std::string getValue(const char *      key);
};

typedef struct MetricFactors {
    std::string err;
    double input_to_internal, internal_to_input, input_to_slicer, slicer_to_internal;
    MetricFactors(Configuration &config);
} MetricFactors;

//this si a hack to convert a series of things to a string
template<typename... Args> std::string str(Args... args) {
    std::ostringstream fmt;
    int dummy[sizeof...(Args)] = { (fmt << args, 0)... };
    return std::string(fmt.str());
}

#endif
