#include "sliceviewer.hpp"
#include <sstream>

std::string SlicesViewer::start() {

    args.reserve(7);
    args.push_back(scriptPath);
    args.push_back(windowname.empty() ? std::string("_") : windowname);
    args.push_back(std::string(use2D ? "2d" : "3d"));
    args.push_back(std::string(pipeInput ? "pipe" : "file"));
    if (!pipeInput) {
        args.push_back(inputfile);
    }
    if (use_custom_formatting && (!custom_formatting.empty())) {
        args.push_back(custom_formatting);
    }
    return SubProcessManager::start();

}

