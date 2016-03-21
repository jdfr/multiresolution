//shared library interface

#ifndef MULTIRESOLUTION_HEADER
#define MULTIRESOLUTION_HEADER

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

//this is used by getDesiredPaths()
typedef int OutputSliceInfo_PathType;
#define PathToolPath 0
#define PathProcessed 1
#define PathContour 2

//do not declare the shared library definitions if we only want the above definitions
#ifndef INCLUDE_MULTIRESOLUTION_ONLY_FOR_DEFINITIONS 

#if ( defined(_WIN32) || defined(_WIN64) ) 
#    if (!(defined(_MSC_VER) || defined(__GNUC__))) //THIS IS VALID ONLY FOR MSVS AND MINGW
#        error Compiler not supported for now
#    endif
#    ifdef LIBRARY_EXPORTS
#        define LIBRARY_API __declspec(dllexport)
#    else
#        define LIBRARY_API __declspec(dllimport)
#    endif
     typedef long long int coord_type;
#elif defined(__GNUC__)
     typedef long long int coord_type;
#    if __GNUC__ >= 4
#        define LIBRARY_API __attribute__ ((visibility ("default")))
#    else
#        define LIBRARY_API
#    endif
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

typedef struct OutputSliceInfo {
    int numpaths;
    int *numpointsArray;
    coord_type **pathsArray;
    double z;
    int ntool;
} OutputSliceInfo;

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

typedef struct ParamsExtractInfo {
    coord_type * processRadiuses;
    int numProcesses;
} ParamsExtractInfo;

typedef struct ConfigExtractInfo {
    double factor_input_to_internal;
    double factor_internal_to_input;
    double factor_slicer_to_internal;
} ConfigExtractInfo;

#ifdef __cplusplus
extern "C" {
#endif

    //getErrorText can be called for handles to arguments, results, and slices
    LIBRARY_API  char * getErrorText(void* anyValue);

    // COMMON FUNCTIONS

    LIBRARY_API  ConfigHandle readConfiguration(char *configfilename);

    LIBRARY_API  char * getParameterHelp(int showGlobals, int showPerProcess, int showExample);

    LIBRARY_API  void  freeParameterHelp(char *helpstr);

    LIBRARY_API  StateHandle parseArgumentsMainStyle(ConfigHandle config, int argc, const char** argv);

    LIBRARY_API  StateHandle parseArguments(ConfigHandle config, char* arguments);

    LIBRARY_API ParamsExtractInfo getParamsExtract(StateHandle state);

    LIBRARY_API ConfigExtractInfo getConfigExtract(ConfigHandle config);

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
    LIBRARY_API void receiveInputSlice(StateHandle state, InputSliceHandle slice);

    //compute more slices, if possible
    LIBRARY_API void computeOutputSlices(StateHandle state);

    /*may return NULL to mean that no output is ready yet. Because of that,
    error strings can be queried from the InputSliceHandle argument*/
    LIBRARY_API ResultsHandle giveOutputIfAvailable(StateHandle state);

    // FUNCTIONS FOR ONLINE FEEDBACK IN 3D SCHEDULING

    //important: after using the slice here, it is "spent" (cannot be used in another context), but you still have to use freeInputSlice on it!!!
    LIBRARY_API void receiveAdditionalAdditiveContours(StateHandle state, double z, InputSliceHandle slice);

    LIBRARY_API void purgueAdditionalAdditiveContours(StateHandle state);

    // PATH LOADING FUNCTIONS

    LIBRARY_API  LoadPathFileInfo loadPathsFile(char *pathsfilename);
    LIBRARY_API  void             freePathsFile(PathsHandle paths);
    LIBRARY_API  LoadPathInfo     loadNextPaths(PathsHandle paths);

#ifdef __cplusplus
}
#endif

#endif //INCLUDE_MULTIRESOLUTION_ONLY_FOR_DEFINITIONS

#endif //MULTIRESOLUTION_HEADER
