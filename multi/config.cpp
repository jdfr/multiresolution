#include "config.hpp"
#include <stdio.h>
#include <sys/stat.h>
#include <vector>
#include <sstream>
#include <cctype>
#include <algorithm> 

long GetFileSize(const char *filename) {
    struct stat stat_buf;
    int rc = stat(filename, &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

std::string get_file_contents(const char *filename, bool &ok) {
    ok = true;
    long fsize = GetFileSize(filename);
    FILE *fp = fopen(filename, "rb"); //do not mess with encodings here
    if (fp) {
        std::string contents;
        contents.resize(fsize);
        fread(&contents[0], 1, contents.size(), fp);
        fclose(fp);
        return(contents);
    } else {
        std::ostringstream fmt;
        fmt << "could not open file <" << filename << ">\n";
        std::string err = fmt.str();
        ok = false;
        return err;
    }
}

std::vector<std::string> split(std::string &input, escaped_list_separator<char> &tokspec) {
    tokenizer<escaped_list_separator<char>> tok(input, tokspec);
    //cannot do this: multispace segments are not passed in one chunk, but one by one
    //return std::vector<std::string>(tok.begin(), tok.end());
    std::vector<std::string> ret;
    for (auto it = tok.begin(); it != tok.end(); ++it) {
        if (!it->empty()) {
            ret.push_back(*it);
        }
    }
    return ret;
}

bool notspace(char x) { return !std::isspace(x); }

// trim from start
static inline std::string &ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
    return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
    return ltrim(rtrim(s));
}

Configuration::Configuration(const char *filename) {
    bool ok = true;
    std::string contents = get_file_contents(filename, ok);
    has_err = !ok;
    if (!ok) {
        err = std::move(contents);
        return;
    }
    escaped_list_separator<char> tokpairs("", ";", "");// "\"");
    //escaped_list_separator<char> tokelems("", ":", "");// "\"");

    std::vector<std::string> pairs = split(contents, tokpairs);
    contents = std::string();

    for (auto pair = pairs.begin(); pair != pairs.end(); ++pair) {
        size_t pos = pair->find_first_of(':');
        if (pos != std::string::npos) {
            std::string key   = pair->substr(0, pos);
                        key   = trim(key);
            if (hasKey(key)) continue;
            std::string value = pair->substr(pos + 1);
                        value = trim(value);
            store.emplace(std::move(key), std::move(value));
        }
    }
}

bool Configuration::hasKey(const std::string key) {
    return store.find(key) != store.end();
}

std::string Configuration::getValue(const std::string key) {
    auto iter = store.find(key);
    return (iter == store.end()) ? std::string() : iter->second;
}

bool Configuration::hasKey(const char * key) {
    return hasKey(std::string(key));
}

std::string Configuration::getValue(const char * key) {
    return getValue(std::string(key));
}

