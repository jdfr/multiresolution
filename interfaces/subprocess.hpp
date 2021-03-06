#ifndef SUBPROCESS_HEADER
#define SUBPROCESS_HEADER

#include "common.hpp"
#include "config.hpp"

#if defined(_WIN32) || defined(_WIN64)
#  define INWINDOWS
#  include <windows.h>
#  include <io.h>
#endif

#include <stdio.h>
#include <string>

class SubProcessManager {
public:
    std::vector<std::string> args;
    std::string workdir;
    std::string execpath;
    std::string exename;
    bool pipeInput, pipeOutput;
    FILE *pipeIN, *pipeOUT;
    SubProcessManager(bool pipeI = false, bool pipeO = false) : pipeInput(pipeI), pipeOutput(pipeO), inUse(false), exename("subprocess") {}
    virtual ~SubProcessManager() { wait(); }
    virtual std::string start();
    virtual bool started() { return inUse; }
    virtual void wait();
    virtual void kill();
protected:
    bool inUse;
    virtual void closePipes();
#ifdef INWINDOWS
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    HANDLE INR, INW, OUTR, OUTW;
#else
    int status, pid;
    int aStdinPipe[2];
    int aStdoutPipe[2]; 
#endif
};

#endif