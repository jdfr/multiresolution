#include "showcontours.hpp"
#include "iopaths.hpp"
#include "subprocess.hpp"
#include <stdio.h>
#include <sstream>

void showContours(std::vector<clp::Paths> &contours, ShowContoursInfo &info) {
    //int n = 10;
    //n = contours.size() < n ? contours.size() : n;
    auto n = contours.size();
    std::vector<clp::Paths*> args(n);
    for (int k = 0; k < n; ++k) {
        args[k] = &(contours[k]);
    }
    auto first = args[0];
    args[0] = args[n - 1];
    args[n - 1] = first;
    showContours(args, info);
}

void showContours(std::vector<clp::Paths*> &contours, ShowContoursInfo &info) {
    if (contours.empty()) {
        return;
    }

    //pipe input if requested, but no need to pipe output
    SubProcessManager subp(info.usePipe, false);

    
    subp.args.push_back(info.showContoursScriptPath);
    subp.args.push_back(info.windowname.empty() ? std::string("_") : info.windowname);
    subp.args.push_back(std::string(info.usePipe ? "pipe" : "files"));
    if (!info.usePipe) {
        for (int i = 0; i< contours.size(); ++i) {
            std::ostringstream fmtfile;
            fmtfile << info.showContoursDir << info.filesep << "file" << i << ".paths";
            FILE *f = fopen(fmtfile.str().c_str(), "wb");
            IOPaths iop(f);
            if (!iop.writeClipperPaths(*(contours[i]), PathOpen)) {
                fclose(f);
                fprintf(stderr, "Could not write contents to file %s\n", fmtfile.str().c_str());
                return;
            };
            fclose(f);
            subp.args.push_back(fmtfile.str());
        }
    }

    subp.execpath = info.pythonExecutablePath;
    subp.workdir     = info.showContoursDir;

    std::string err = subp.start();

    if (!err.empty()) {
        fprintf(stderr, err.c_str());
        return;
    }

    if (info.usePipe) {
        int64 numpaths = contours.size();
        fwrite(&numpaths, sizeof(numpaths), 1, subp.pipeIN);
        IOPaths iop(subp.pipeIN);
        for (int i = 0; i< contours.size(); ++i) {
            if (!iop.writeClipperPaths(*(contours[i]), PathOpen)) {
                fprintf(stderr, "Could not write the %d-th contour to the subprocess's stdin pipe!!!\n", i);
            };
        }
        fflush(subp.pipeIN);
    }

    subp.wait();

}