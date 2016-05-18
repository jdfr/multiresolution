#ifndef APPUTIL_HEADER
#define APPUTIL_HEADER

#include <stdio.h>
#include <time.h>
#include <vector>

#ifdef _WIN32
#   include <Windows.h>
#else //POSIX
#   include <sys/time.h>
#endif

class TimeMeasurements {
protected:
    std::vector<clock_t> cputimes;
    std::vector<double>  walltimes;
#ifdef _WIN32
    static double getWallTime() {
        LARGE_INTEGER time,freq;
        if (!QueryPerformanceFrequency(&freq)) return 0; // error
        if (!QueryPerformanceCounter(&time))   return 0; // error
        return (double)time.QuadPart / freq.QuadPart;
    }
#else //POSIX
    static double getWallTime() {
        struct timeval time;
        if (gettimeofday(&time,NULL)) return 0; // error
        return (double)time.tv_sec + (double)time.tv_usec * .000001;
    }
#endif
public:
    TimeMeasurements(int numMeasurements = 2) {
        cputimes .reserve(numMeasurements + 1);
        walltimes.reserve(numMeasurements + 1);
    }
    void measureTime() {
        cputimes .push_back(clock());
        walltimes.push_back(getWallTime());
    }
    double getCPUTimeMeasurement(int idx) {
        return (double)(cputimes[idx] - cputimes[idx-1]) / CLOCKS_PER_SEC;
    }
    double getWallTimeMeasurement(int idx) {
        return walltimes[idx] - walltimes[idx-1];
    }
    void printMeasurement(FILE *f, const char *format, int idx) {
        fprintf(f, format, getCPUTimeMeasurement(idx), getWallTimeMeasurement(idx));
    }
    void printLastMeasurement(FILE *f, const char *format) {
        printMeasurement(f, format, (int)cputimes.size()-1);
    }
};

//simple type for using RAII with FILE*
typedef struct FILEOwner {
    FILE *f;
    FILEOwner() : f(NULL) {}
    FILEOwner(const char *fname, const char *mode) : f(fopen(fname, mode)) {}
    ~FILEOwner()  { close(); }
    bool isopen() { return f!=NULL; }
    bool open(const char *fname, const char *mode) {
        if (!close()) return false;
        f = fopen(fname, mode);
        return isopen();
    }
    bool close()   {
        bool ret = true;
        if (f != NULL) {
            ret = fclose(f)==0;
            f = NULL;
        }
        return ret;
    }
} FILEOwner;

#endif