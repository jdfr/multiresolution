#include "simpleparsing.hpp"
#include "config.hpp"

void ParamReader::setState(std::string &args, ParamMode mode) {
    if (mode == ParamString) {
        splitted = normalizedSplit(args);
        local_argv.resize(splitted.size());
        for (int k = 0; k<local_argv.size(); ++k) {
            local_argv[k] = splitted[k].c_str();
        }
        argv = &(local_argv[0]);
        argc = (int)local_argv.size();
        argidx = 0;
    } else {
        bool ok = true;
        const bool binary = false;
        std::string contents = get_file_contents(args.c_str(), binary, ok);
        if (ok) {
            setState(contents, ParamString);
        } else {
            argc = 0;
            argv = NULL;
            argidx = 0;
            err = std::move(contents);
        }
    }
}

void ParamReader::setState(const std::vector<std::string> &args) {
    local_argv.resize(args.size());
    for (int k = 0; k<local_argv.size(); ++k) {
        local_argv[k] = args[k].c_str();
    }
    argv = &(local_argv[0]);
    argc = (int)local_argv.size();
    argidx = 0;
}


ParamReader ParamReader::getParamReaderWithOptionalResponseFile(int argc, const char **argv, int numskip) {
    //first argument is exec's filename
    if (argc == (numskip + 1)) {
        return ParamReader(argv[numskip], ParamFile);
    } else {
        return ParamReader(0, argc - numskip, argv + numskip);
    }
}

