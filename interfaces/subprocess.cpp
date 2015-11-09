#include "subprocess.hpp"


#include <stdio.h>

#if defined(_WIN32) || defined(_WIN64)
#  define INWINDOWS
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#  include <sstream>
#else
#  include <unistd.h>
#  include <stdlib.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  define PIPE_READ  0
#  define PIPE_WRITE 1
#endif

std::string SubProcessManager::start() {
    if (inUse) return std::string("already in use");

    pipeIN = pipeOUT = NULL;

#ifdef INWINDOWS

    std::ostringstream fmt;
    fmt << '"' << execpath << '"';
    for (auto arg = args.begin(); arg != args.end(); ++arg) {
        fmt << ' ' << '"' << *arg << '"';
    }
    std::string cmdline = fmt.str();

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    INR = INW = OUTR = OUTW = NULL;

    si.cb = sizeof(STARTUPINFO);

    SECURITY_ATTRIBUTES saAttr;

    if (pipeInput || pipeOutput) {
        //si.wShowWindow = SW_HIDE;
        si.dwFlags |= STARTF_USESTDHANDLES /*| STARTF_USESHOWWINDOW*/;
        si.hStdError = NULL;

        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        if (pipeInput) {
            // Create a pipe for the child process's STDIN. 
            if (!CreatePipe(&INR, &INW, &saAttr, 0))                 { return std::string("error Stdin CreatePipe\n"); }
            // Ensure the write handle to the pipe for STDIN is not inherited. 
            if (!SetHandleInformation(INW, HANDLE_FLAG_INHERIT, 0))  { return std::string("error Stdin SetHandleInformation\n"); }
            si.hStdInput = INR;
        }

        if (pipeOutput) {
            // Create a pipe for the child process's STDOUT. 
            if (!CreatePipe(&OUTR, &OUTW, &saAttr, 0))               { return std::string("Stdout CreatePipe"); }
            // Ensure the read handle to the pipe for STDOUT is not inherited.
            if (!SetHandleInformation(OUTR, HANDLE_FLAG_INHERIT, 0)) { return std::string("Stdout SetHandleInformation"); }
            si.hStdOutput = OUTW;
        }

    }

    // Start the child process. 
    if (!CreateProcess(NULL,   // No module name (use command line)
        const_cast<char *>(cmdline.c_str()),        // Command line
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        1, // Set handle inheritance
        CREATE_NO_WINDOW,              // No creation flags
        NULL,           // Use parent's environment block
        workdir.c_str(),  // starting directory 
        &si,            // Pointer to STARTUPINFO structure
        &pi)           // Pointer to PROCESS_INFORMATION structure
        )
    {
        return str("CreateProcess failed (", GetLastError(), ")");
    }

    if (pipeInput) {
        int descriptor = _open_osfhandle((intptr_t)INW, 0);
        pipeIN = _fdopen(descriptor, "wb");
    }

    if (pipeOutput) {
        int descriptor = _open_osfhandle((intptr_t)OUTR, _O_RDONLY);
        pipeOUT = _fdopen(descriptor, "rb");
    }

#else

    if (pipeInput) {
        if (pipe(aStdinPipe) < 0) {
            return std::string("allocating pipe for child input redirect");
        }
    }

    if (pipeOutput) {
        if (pipe(aStdoutPipe) < 0) {
            close(aStdinPipe[PIPE_READ]);
            close(aStdinPipe[PIPE_WRITE]);
            return std::string("allocating pipe for child output redirect");
        }
    }

    pid = fork();

    if (pid < 0) {
        return std::string("fork failed!\n");
    }

    if (pid == 0) {
        chdir(workdir.c_str());

        if (pipeInput) {
            // redirect stdin
            if (dup2(aStdinPipe[PIPE_READ], STDIN_FILENO) == -1) {
                fprintf(stderr, "error redirecting stdin");
                exit(-1);
            }
            // these are for use by parent only
            close(aStdinPipe[PIPE_READ]);
            close(aStdinPipe[PIPE_WRITE]);
        }

        if (pipeOutput) {
            // redirect stdout
            if (dup2(aStdoutPipe[PIPE_WRITE], STDOUT_FILENO) == -1) {
                fprintf(stderr, "error redirecting stdout");
                exit(-1);
            }
            // these are for use by parent only
            close(aStdoutPipe[PIPE_READ]);
            close(aStdoutPipe[PIPE_WRITE]);
        }


        std::vector<const char *> argv;
		argv.reserve(args.size()+2);
        argv.push_back("subprocess");
        for (auto arg = args.begin(); arg != args.end(); ++arg) {
            argv.push_back(arg->c_str());
        }
        argv.push_back(NULL);
        execv(execpath.c_str(), const_cast<char* const*>(&(argv[0])));

        fprintf(stderr, "error trying to execute %s", execpath.c_str());
        exit(-1);
    } else {
        // close unused file descriptors (these are for child only) and get the file pointers
        if (pipeInput) {
            close(aStdinPipe[PIPE_READ]);
            pipeIN  = fdopen(aStdinPipe [PIPE_WRITE], "w");
        }
        if (pipeOutput) {
            close(aStdoutPipe[PIPE_WRITE]);
            pipeOUT = fdopen(aStdoutPipe[PIPE_READ],  "r");
        }
    }

#endif

    inUse = true;
    return std::string();
}

void SubProcessManager::wait() {
    if (inUse) {
        inUse = false;
        if (pipeInput) {
            fclose(pipeIN);
#ifdef INWINDOWS
            CloseHandle(INR);
#endif
        }
        if (pipeOutput) {
            fclose(pipeOUT);
#ifdef INWINDOWS
            CloseHandle(OUTW);
#endif
        }
#ifdef INWINDOWS
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
#else
        waitpid(pid, &status, 0);
#endif
    }
}
