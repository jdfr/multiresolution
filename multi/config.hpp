#ifndef CONFIG_HEADER
#define CONFIG_HEADER

#include <map>
#include <string>
#include <sstream>

//slurp file
std::string get_file_contents(const char *filename, bool &ok);

#include <boost/tokenizer.hpp>
using boost::tokenizer;
using boost::escaped_list_separator;

std::vector<std::string> split(std::string &input, escaped_list_separator<char> &tokspec);

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
    bool hasKey(const std::string key);
    bool hasKey(const char *      key);
    std::string getValue(const std::string key);
    std::string getValue(const char *      key);
};

//this si a hack to convert a series of things to a string
template<typename... Args> std::string str(Args... args) {
    std::ostringstream fmt;
    int dummy[sizeof...(Args)] = { (fmt << args, 0)... };
    return std::string(fmt.str());
}

#endif
