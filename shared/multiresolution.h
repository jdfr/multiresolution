//shared library interface

#ifndef MULTIRESOLUTION_HEADER
#define MULTIRESOLUTION_HEADER

#if ( defined(_WIN32) || defined(_WIN64) ) //THIS COVERS BOTH MSVS AND MINGW
#    ifdef LIBRARY_EXPORTS
#        define LIBRARY_API __declspec(dllexport)
#    else
#        define LIBRARY_API __declspec(dllimport)
#    endif
#    include <comutil.h>
     typedef long long int coord_type;
#elif defined(__GNUC__) //NO NEED FOR __declspec IN GCC LAND
#    define LIBRARY_API
     typedef const char * BSTR;
     //"long long"=="long" in x64 land, but we use "long long" because GCC is horribly pedantic about types
     typedef long long int coord_type;
#else
#    error Compiler not supported for now
#endif

struct SharedLibraryConfig;   typedef SharedLibraryConfig   *     ConfigHandle;
struct SharedLibraryState;    typedef SharedLibraryState    *      StateHandle;
struct SharedLibrarySlice;    typedef SharedLibrarySlice    * InputSliceHandle;
struct SharedLibraryResult;   typedef SharedLibraryResult   *    ResultsHandle;
struct SharedLibraryPaths;    typedef SharedLibraryPaths    *      PathsHandle;

typedef struct Slices3DSpecInfo {
    int numinputslices;
    int numoutputslices;
    double *zs;
} Slices3DSpecInfo;

typedef struct InputSliceInfo {
    InputSliceHandle slice;
    int* numpointsArray;
} InputSliceInfo;

//this is used by getDesiredPaths()
typedef int OutputSliceInfo_PathType;
#define PathToolPath 0
#define PathProcessed 1
#define PathContour 2

typedef struct OutputSliceInfo {
    int numpaths;
    int *numpointsArray;
    coord_type **pathsArray;
    double z;
    int ntool;
} OutputSliceInfo;

//this is used in struct LoadPathInfo
typedef int LoadPathInfo_PathType;
#define PATHTYPE_RAW_CONTOUR       0
#define PATHTYPE_PROCESSED_CONTOUR 1
#define PATHTYPE_TOOLPATH          2

//this is used in struct LoadPathInfo
typedef int LoadPathInfo_PathFormat;
#define PATHFORMAT_INT64     0
#define PATHFORMAT_DOUBLE    1
#define PATHFORMAT_DOUBLE_3D 2

typedef struct LoadPathInfo {
    int numpaths;
    int *numpointsArray;
    void **pathsArray; //can be either double** or coord_type**
    double scaling;
    double z;
    LoadPathInfo_PathType type;
    LoadPathInfo_PathFormat saveFormat;
    int ntool;
    int numRecord; //-1 if EOF
} LoadPathInfo;

typedef struct LoadPathFileInfo {
    PathsHandle pathfile;
    int numRecords;
    int ntools;
} LoadPathFileInfo;

#ifdef __cplusplus
extern "C" {
#endif

    //getErrorText can be called for handles to arguments, results, and slices
    LIBRARY_API  BSTR getErrorText(void* anyValue);

    // COMMON FUNCTIONS

    LIBRARY_API  ConfigHandle readConfiguration(char *configfilename);

    LIBRARY_API  StateHandle parseArgumentsMainStyle(ConfigHandle config, int doscale, int argc, const char** argv);

    LIBRARY_API  StateHandle parseArguments(ConfigHandle config, int doscale, char* arguments);

    LIBRARY_API  void freeState(StateHandle arguments);

    LIBRARY_API  void freeConfig(ConfigHandle config);

    LIBRARY_API  InputSliceInfo createInputSlice(int numpaths);

    LIBRARY_API  coord_type** getPathsArray(InputSliceHandle slice);

    LIBRARY_API  void freeInputSlice(InputSliceHandle slice);

    LIBRARY_API OutputSliceInfo getOutputSliceInfo(ResultsHandle result, int ntool, OutputSliceInfo_PathType pathtype);

    LIBRARY_API  void freeResult(ResultsHandle result);

    LIBRARY_API  int alsoComplementary(ResultsHandle result, int ntool);

    // 2D FUNCTIONS

    LIBRARY_API  ResultsHandle computeResult(InputSliceHandle slice, StateHandle state);

    // 3D SCHEDULING FUNCTIONS (to do the optimal and right thing, the output interface should be different from the 2D case, but we reuse it to save LOCs)

    LIBRARY_API Slices3DSpecInfo computeSlicesZs(StateHandle state, double zmin, double zmax);

    //error messages from this function can be queried from the StateHandle argument
    LIBRARY_API void receiveInputSlice(InputSliceHandle slice, StateHandle state);

    /*may return NULL to mean that no output is ready yet. Because of that,
    error strings can be queried from the InputSliceHandle argument*/
    LIBRARY_API ResultsHandle giveOutputIfAvailable(StateHandle state);

    // PATH LOADING FUNCTIONS

    LIBRARY_API  LoadPathFileInfo loadPathsFile(char *pathsfilename);
    LIBRARY_API  void             freePathsFile(PathsHandle paths);
    LIBRARY_API  LoadPathInfo     loadNextPaths(PathsHandle paths);

#ifdef __cplusplus
}
#endif

#endif
