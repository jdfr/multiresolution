#ifndef  SIMPLEPARSING_HEADER
#define  SIMPLEPARSING_HEADER

#include "iopaths.hpp"
#include <sstream>

/********************************************************
SIMPLE CLASS FOR READING ARGUMENTS
*********************************************************/

#if defined(_MSC_VER)
#  define strtoll _strtoi64
#endif

enum ParamMode { ParamString, ParamFile };

//this class is rather inefficient, it exists in its current form to have less boilerplate when reading arguments and checking them
class ParamReader {
public:
    std::string err;
    std::ostringstream fmt;
    std::string errorAppendix;
    int argidx;
    int argc;
    const char **argv;
    double scale;
    ParamReader(int _argidx, int _argc, const char **_argv) : argidx(_argidx), argc(_argc), argv(_argv), scale(0.0) {}
    ParamReader(std::string &args, ParamMode mode, double s = 0.0) : scale(s) { setState(args, mode); };
    ParamReader(const char *args, ParamMode mode, double s = 0.0) : scale(s) { std::string sargs(args);  setState(sargs, mode); }
    ParamReader(const std::vector<std::string> &args, double s = 0.0) : scale(s) { setState(args); }
    static ParamReader getParamReaderWithOptionalResponseFile(int argc, const char **argv, int numskip = 1);

#ifdef __GNUC__
    //this is needed because GCC does not implement stringstream's move constructor
    //WARNING: NON_COMPLETELY CLEAN MOVE SEMANTICS DUE TO THE INABILITY TO MOVE A STRINGSTREAM IN GCC. NOT IMPORTANT ANYWAY FOR OUR USE CASE
    ParamReader(ParamReader &&pr) : err(std::move(pr.err)), argidx(pr.argidx), argc(pr.argc), argv(pr.argv), scale(pr.scale), splitted(std::move(pr.splitted)), local_argv(std::move(pr.local_argv)) {}
#endif
    //these methods are templated in order to laziy generate a error message from several arguments if there is a problem
    template<typename... Args> void writeError(Args... args) {
        fmt << "error reading ";
        int dummy[sizeof...(Args)] = { (fmt << args, 0)... };
        fmt << " (arg.number " << (argidx + 1) << " / " << argc << ")";
    }

    template<typename... Args> bool checkEnoughParams(Args... args) {
        if (argidx >= argc) {
            writeError(args...);
            fmt << ", not enough arguments\n" << errorAppendix;
            return false;
        }
        return true;
    }
    template<typename... Args> bool checkNumber(const char *param, const char *endptr, Args... args) {
        bool ok = (*endptr) == 0;
        if (ok) {
            ++argidx;
        } else {
            writeError(args...);
            fmt << ", <" << param << "> is not a valid number\n" << errorAppendix;
        }
        return ok;
    }
    template<typename... Args> bool readParam(double &param, Args... args) { if (!checkEnoughParams(args...)) return false; const char *p = argv[argidx]; char *e; param = strtod(p, &e);     return checkNumber(p, e, args...); }
    template<typename... Args> bool readParam(int64  &param, Args... args) { if (!checkEnoughParams(args...)) return false; const char *p = argv[argidx]; char *e; param = strtoll(p, &e, 10); return checkNumber(p, e, args...); }
    template<typename... Args> bool readParam(int    &param, Args... args) { if (!checkEnoughParams(args...)) return false; const char *p = argv[argidx]; char *e; param = (int)strtol(p, &e, 10); return checkNumber(p, e, args...); }
    template<typename... Args> bool readParam(unsigned int &param, Args... args) { if (!checkEnoughParams(args...)) return false; const char *p = argv[argidx]; char *e; param = (unsigned int)strtol(p, &e, 10); return checkNumber(p, e, args...); }
    template<typename... Args> bool readParam(bool   &param, const char* trueval, Args... args) { if (!checkEnoughParams(args...)) return false; param = strcmp(argv[argidx++], trueval) == 0; return true; }
    template<typename... Args> bool readParam(const char* &param, Args... args) { if (!checkEnoughParams(args...)) return false; param = argv[argidx++];                return true; }
    template<typename... Args> bool readScaled(int64  &param, Args... args) {
        if (scale == 0.0)
            return readParam(param, args...);
        else {
            double val;
            bool ok = readParam(val, args...);
            if (ok) param = (int64)(val*scale);
            return ok;
        }
    }
    template<typename... Args> bool readScaled(double  &param, Args... args) {
        bool ok = readParam(param, args...);
        if (ok && (scale != 0.0)) param *= scale;
        return ok;
    }
    template<typename... Args> bool readKeyword(const char* keyword, bool justFirstChar, Args... args) {
        if (strlen(keyword) == 0) {
            fmt << "PROGRAMMING ERROR: requested keyword is empty!!!!!!";
            return false;
        }
        const char * word;
        if (!readParam(word, args...))  { return false; }
        bool ok;
        if (justFirstChar) {
            ok = tolower(keyword[0]) == tolower(word[0]);
        } else {
            ok = strcmp(keyword, word) == 0;
        }
        if (!ok) {
            writeError(args...);
            fmt << "The keyword " << keyword << " was expected, <" << word << "> was read\n";
        }
        return ok;
    }
protected:
    std::vector<std::string> splitted;
    std::vector<const char*> local_argv;
    void setState(std::string &args, ParamMode mode);
    void setState(const std::vector<std::string> &args);
};

#endif