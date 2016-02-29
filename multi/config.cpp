#include "config.hpp"
#include <stdio.h>
#include <sys/stat.h>

#include <boost/tokenizer.hpp>
using boost::tokenizer;
using boost::escaped_list_separator;

//extend boost::escaped_list_separator to handle simple comments (with single-char comment separators)

struct escaped_list_error : public std::runtime_error {
    escaped_list_error(const std::string& what_arg) :std::runtime_error(what_arg) {}
};

template <class Char, class Traits = BOOST_DEDUCED_TYPENAME std::basic_string<Char>::traits_type > class escaped_list_separator_with_comments {

private:
    typedef std::basic_string<Char, Traits> string_type;
    struct char_eq {
        Char e_;
        char_eq(Char e) :e_(e) {}
        bool operator()(Char c) {
            return Traits::eq(e_, c);
        }
    };
    string_type  escape_;
    string_type  c_;
    string_type  quote_;
    Char startComment, endComment;
    bool last_;

    bool is_escape(Char e) {
        char_eq f(e);
        return std::find_if(escape_.begin(), escape_.end(), f) != escape_.end();
    }
    bool is_c(Char e) {
        char_eq f(e);
        return std::find_if(c_.begin(), c_.end(), f) != c_.end();
    }
    bool is_quote(Char e) {
        char_eq f(e);
        return std::find_if(quote_.begin(), quote_.end(), f) != quote_.end();
    }
    template <typename iterator, typename Token>
    void do_escape(iterator& next, iterator end, Token& tok) {
        if (++next == end)
            throw escaped_list_error(std::string("cannot end with escape"));
        if (Traits::eq(*next, 'n')) {
            tok += '\n';
            return;
        } else if (is_quote(*next)) {
            tok += *next;
            return;
        } else if (is_c(*next)) {
            tok += *next;
            return;
        } else if (is_escape(*next)) {
            tok += *next;
            return;
        } else
            throw escaped_list_error(std::string("unknown escape sequence"));
    }

public:

    explicit escaped_list_separator_with_comments(Char  e = '\\',
        Char c = ',', Char  q = '\"', Char startc = '#', Char endc = '\n')
        : escape_(1, e), c_(1, c), quote_(1, q), last_(false), startComment(startc), endComment(endc) {
    }

    escaped_list_separator_with_comments(string_type e, string_type c, string_type q, Char startc, Char endc)
    : escape_(e), c_(c), quote_(q), last_(false), startComment(startc), endComment(endc)  {
    }

    void reset() { last_ = false; }

    template <typename InputIterator, typename Token>
    bool operator()(InputIterator& next, InputIterator end, Token& tok) {
        bool bInQuote = false;
        tok = Token();

        if (next == end) {
            if (last_) {
                last_ = false;
                return true;
            } else
                return false;
        }
        last_ = false;
        for (; next != end; ++next) {
            if ((!bInQuote) && Traits::eq(*next, startComment)) {
                ++next;
                for (; next != end; ++next) {
                    if (Traits::eq(*next, endComment)) {
                        ++next;
                        break;
                    }
                }
                return true;
            }
            if (is_escape(*next)) {
                do_escape(next, end, tok);
            } else if (is_c(*next)) {
                if (!bInQuote) {
                    // If we are not in quote, then we are done
                    ++next;
                    // The last character was a c, that means there is
                    // 1 more blank field
                    last_ = true;
                    return true;
                } else tok += *next;
            } else if (is_quote(*next)) {
                bInQuote = !bInQuote;
            } else {
                tok += *next;
            }
        }
        return true;
    }
};


long GetFileSize(const char *filename) {
    struct stat stat_buf;
    int rc = stat(filename, &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

std::string get_file_contents(const char *filename, bool binary, bool &ok) {
    ok = true;
    long fsize = GetFileSize(filename);
    FILE *fp = fopen(filename, binary ? "rb" : "rt");
    if (fp) {
        std::string contents;
        contents.resize(fsize);
        size_t num = fread(&contents[0], 1, contents.size(), fp);
        fclose(fp);
        if (binary) {
            if (num != fsize) {
                ok = false;
                return str("error reading file <", filename, ">\n");
            }
        } else {
            contents.resize(num); //avoid problems with trailing zeros/garbage in text mode
        }
        return(contents);
    } else {
        ok = false;
        return str("could not open file <", filename, ">\n");
    }
}

template<typename Tokenizer> std::vector<std::string> split_implementation(std::string &input, Tokenizer tok) {
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

std::vector<std::string> split(std::string &input, const char *escape_chars, const char *separator_chars, const char * quote_chars) {
    typedef escaped_list_separator<char> sep;
    return split_implementation(input, tokenizer<sep>(input, sep(escape_chars, separator_chars, quote_chars)));
}

std::vector<std::string> split(std::string &input, const char *escape_chars, const char *separator_chars, const char * quote_chars, char startComment, char endComment) {
    typedef escaped_list_separator_with_comments<char> sep;
    return split_implementation(input, tokenizer<sep>(input, sep(escape_chars, separator_chars, quote_chars, startComment, endComment)));
}

bool notspace(char x) { return !std::isspace(x); }

inline std::string &ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    return s;
}

// trim from start
std::string &ltrim(std::string &s, char startComment, char endComment) {
    auto endit = std::find_if(s.begin(), s.end(), notspace);
    while ((endit != s.end()) && (*endit == startComment)) {
        for (; endit != s.end(); ++endit) {
            if (*endit == endComment) {
                ++endit;
                break;
            }
        }
        endit = std::find_if(endit, s.end(), notspace);
    }
    s.erase(s.begin(), endit);
    return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
    return s;
}

// trim from both ends
static inline std::string &trim(std::string &s, char startComment, char endComment) {
    return ltrim(rtrim(s), startComment, endComment);
}

static inline std::string &trim(std::string &s) {
    return ltrim(rtrim(s));
}

void Configuration::load(const char *filename) {
    bool ok = true;
    const bool binary = true;
    std::string contents = get_file_contents(filename, binary, ok);
    has_err = !ok;
    if (!ok) {
        err = std::move(contents);
        return;
    }
    std::vector<std::string> pairs = split(contents, "", ";", "");
    contents = std::string();

    for (auto pair = pairs.begin(); pair != pairs.end(); ++pair) {
        *pair = ltrim(*pair, '#', '\n');
        size_t pos = pair->find_first_of(':');
        if (pos != std::string::npos) {
            std::string key = pair->substr(0, pos);
            key = trim(key);
            if (key.empty() || hasKey(key)) continue;
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

bool getDoubleConfigVal(Configuration &config, const char *name, std::string &err, double &result) {
    if (config.hasKey(name)) {
        std::string val = config.getValue(name);
        char *endptr;
        result = strtod(val.c_str(), &endptr);
        if (*endptr != 0) {
            err = str("cannot parse <", val, "> (", name, ") into a double value");
            return false;
        }
    } else {
        err = str("the configuration value ", name, " was not found in the configuration file!");
        return false;
    }
    return true;
}

void MetricFactors::init(Configuration &config, bool _doparamscale) {
    init_done = true;
    doparamscale = _doparamscale;
    if (!getDoubleConfigVal(config, "INPUT_TO_SLICER_FACTOR", err, input_to_slicer))    return;
    if (!getDoubleConfigVal(config, "SLICER_TO_INTERNAL_FACTOR", err, slicer_to_internal)) return;
    input_to_internal = input_to_slicer * slicer_to_internal;
    internal_to_input = 1 / input_to_internal;
    if (doparamscale) {
        double input_to_param;
        if (!getDoubleConfigVal(config, "INPUT_TO_PARAMETER_FACTOR", err, input_to_param)) return;
        param_to_internal = input_to_internal / input_to_param;
        internal_to_param = 1 / param_to_internal;
    } else {
        param_to_internal = 1;
        internal_to_param = 1;
    }
}
