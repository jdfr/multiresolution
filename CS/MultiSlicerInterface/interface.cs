using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace MultiSlicerInterface {

    public static class MultiCfg {
        //enum defined for the field type in LoadPathInfo
        public enum LoadPathType : int {
            PATHTYPE_RAW_CONTOUR        = 0,
            PATHTYPE_PROCESSED_CONTOUR  = 1,
            PATHTYPE_TOOLPATH_PERIMETER = 2,
            PATHTYPE_TOOLPATH_INFILLING = 3,
            PATHTYPE_TOOLPATH_SURFACE   = 4
        };
        //enum defined for getOutputSliceInfo()
        public enum PathType : int {
            PathInfillingAreas    = 0,
            PathContour           = 1,
            PathToolPathPerimeter = 2,
            PathToolPathInfilling = 3,
            PathToolPathSurface   = 4,
        };
    }

    //ClipperLib::IntPoint
    public struct IntPoint {
        public long x;
        public long y;
    }

    //BASIC INTERFACE FOR THE EXTERNAL STANDALONE SLICER PROCESS
    //In a true use case, it is probable that the slices to be processed by the DLL will be generated from other sources,
    //but for demo purposes, we use this class to wrap our own slicer
    public class ExternalSlicerManager : IDisposable {
        public Process proc;
        public BinaryWriter stdin;
        public BinaryReader stdout;
        public bool repair;
        public bool incremental;
        public string stlfile;
        public long numSlice;
        public double scalingFactor;
        private bool disposed;

        public ExternalSlicerManager(string workdir, string execpath, string debugFile, bool repair, bool incremental, string stlfile) {
            this.repair        = repair;
            this.incremental   = incremental;
            this.stlfile       = stlfile;
            this.numSlice      = 0;
            this.scalingFactor = 0;
            proc = new Process();
            proc.StartInfo.FileName = execpath;
            if (stlfile[0] != '"') stlfile = '"' + stlfile + '"';
            bool useDebugFile = (debugFile != null) && (debugFile.Length > 0);
            proc.StartInfo.Arguments = (useDebugFile ? debugFile + " " : "") +
                                        (repair ? "repair " : "norepair ") +
                                        (incremental ? "incremental " : "noincremental ") +
                                        stlfile;
            proc.StartInfo.WorkingDirectory = workdir;

            proc.StartInfo.UseShellExecute = false;
            proc.StartInfo.ErrorDialog     = false;
            proc.StartInfo.CreateNoWindow  = true;
            //proc.StartInfo.RedirectStandardError = true;
            proc.StartInfo.RedirectStandardInput  = true;
            proc.StartInfo.RedirectStandardOutput = true;

            bool ok = proc.Start();

            //proc.ErrorDataReceived += ProcErrorDataHandler;
            stdin    = new BinaryWriter(proc.StandardInput.BaseStream);
            stdout   = new BinaryReader(proc.StandardOutput.BaseStream);
            disposed = false;
        }

        void IDisposable.Dispose() {
            dodispose();
        }

        public void dodispose() {
            if (!disposed) {
                //handle.Free();
                //stdin.Write((long)(-1));
                //stdin.Flush();
                stdin.Close();
                stdout.Close();
                proc.Dispose();
                //GC.SuppressFinalize(this);
                disposed = true;
            }
        }

        ~ExternalSlicerManager() {
            dodispose();
        }

        public void terminate() {
            try {
                proc.Kill();
            } catch {
            }
        }

        public void getBoundingBox(out double minx, out double maxx, out double miny, out double maxy, out double minz, out double maxz) {
            if (!repair) {
                long need_repair = stdout.ReadInt64();
                if (need_repair != 0) {
                    throw new ApplicationException("The STL needs to be repaired!");
                }
            }
            minx = stdout.ReadDouble();
            maxx = stdout.ReadDouble();
            miny = stdout.ReadDouble();
            maxy = stdout.ReadDouble();
            minz = stdout.ReadDouble();
            maxz = stdout.ReadDouble();
            scalingFactor = stdout.ReadDouble();
        }

        public void sendZs(double[] values) {
            stdin.Write((long)values.Length);
            for (int i = 0; i < values.Length; i++) {
                stdin.Write(values[i]);
            }
            stdin.Flush();
        }

        public double[] prepareSTLSimple(double zstep, double zbase = Double.NaN) {
            double minx, maxx, miny, maxy, minz, maxz;
            getBoundingBox(out minx, out maxx, out miny, out maxy, out minz, out maxz);
            if (!Double.IsNaN(zbase)) {
                minz = zbase;
            }
            List<double> zs = new List<double>();
            for (double j = minz + zstep / 2; j <= maxz; j += zstep) {
                zs.Add(j);
            }
            double[] zsa = zs.ToArray();
            sendZs(zsa);
            return zsa;
        }

        public IntPoint[][] readSlice() {
            numSlice++;
            IntPoint[][] ret = new IntPoint[(int)stdout.ReadInt64()][];
            for (int i = 0; i < ret.Length; i++) {
                ret[i] = new IntPoint[(int)stdout.ReadInt64()];
            }
            for (int i = 0; i < ret.Length; i++) {
                IntPoint[] array = ret[i];
                for (int j = 0; j < array.Length; j++) {
                    array[j].x = stdout.ReadInt64();
                    array[j].y = stdout.ReadInt64();
                }
            }
            return ret;
        }
    }


    //return types from C++ DLL

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct OutputSliceInfo {
        public int numpaths;
        public int* numpointsArray;
        public long** pathsArray;
        public double z;
        public int ntool;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct InputSliceInfo {
        public void* slice;
        public int* numpointsArray;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct Slices3DSpecInfo {
        public int numinputslices;
        public int numoutputslices;
        public double* zs;
    }

    //enum defined for the field saveFormat in LoadPathInfo
    public enum LoadPathFormat : int { PATHFORMAT_INT64 = 0, PATHFORMAT_DOUBLE = 1, PATHFORMAT_DOUBLE_3D = 2 };

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct LoadPathInfo {
        public int numpaths;
        public int* numpointsArray;
        public void** pathsArray; //can be either double** or long**
        public double scaling;
        public double z;
        public int type;
        public int saveFormat;
        public int ntool;
        public int numRecord;
    };

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct LoadPathFileInfo {
        public void* pathfile;
        public int numRecords;
        public int ntools;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ParamsExtractInfo {
        public long* processRadiuses;
        public int numProcesses;
        public int alsoContours;
        public int usingScheduler;
        public int use_z_base;
        public double z_uniform_step;
        public double z_base;
    }

    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct ConfigExtractInfo {
        public double factor_input_to_internal;
        public double factor_internal_to_input;
        public double factor_slicer_to_internal;
        public IntPtr slicerWorkdir;
        public IntPtr slicerExePath;
        public IntPtr slicerDebugFile;
        public int useSlicerDebugFile;
    }

    //RAW DLL INTERFACE
    //This object is not prepared for multi-threading yet (checking for errors is not thread-safe), so it should not be used concurrently in several threads
    [SuppressUnmanagedCodeSecurity]
    public class MultiSlicerDllHandler : IDisposable {

        //use delegates instead of [DllImport(...)] because it is more flexible (able to modify the DLL while not loaded)

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate IntPtr getErrorTextDelegate(void* value);
        public getErrorTextDelegate getErrorText;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void* readConfigurationDelegate([MarshalAs(UnmanagedType.LPStr)] string configfile);
        public readConfigurationDelegate readConfiguration;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate IntPtr getParameterHelpDelegate(int showGlobals, int showPerProcess, int showExample);
        public getParameterHelpDelegate getParameterHelp;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void freeParameterHelpDelegate(IntPtr helpstr);
        public freeParameterHelpDelegate freeParameterHelp;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void* parseArgumentsDelegate(void* config, [MarshalAs(UnmanagedType.LPStr)] string parameters);
        public parseArgumentsDelegate parseArguments;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate ParamsExtractInfo getParamsExtractDelegate(void* state);
        public getParamsExtractDelegate getParamsExtract;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate ConfigExtractInfo getConfigExtractDelegate(void* config);
        public getConfigExtractDelegate getConfigExtract;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void freeConfigDelegate(void* config);
        public freeConfigDelegate freeConfig;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void freeStateDelegate(void* state);
        public freeStateDelegate freeState;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate InputSliceInfo createInputSliceDelegate(int numpaths);
        public createInputSliceDelegate createInputSlice;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate long** getPathsArrayDelegate(void* slice);
        public getPathsArrayDelegate getPathsArray;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void* computeResultDelegate(void* slice, void* state);
        public computeResultDelegate computeResult;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void freeInputSliceDelegate(void* slice);
        public freeInputSliceDelegate freeInputSlice;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate int alsoComplementaryDelegate(void* result, int ntool);
        public alsoComplementaryDelegate alsoComplementary;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate OutputSliceInfo getOutputSliceInfoDelegate(void* result, int ntool, int pathtype);
        public getOutputSliceInfoDelegate getOutputSliceInfo;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void freeResultDelegate(void* result);
        public freeResultDelegate freeResult;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate Slices3DSpecInfo computeSlicesZsDelegate(void* state, double zmin, double zmax);
        public computeSlicesZsDelegate computeSlicesZs;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void receiveAdditionalAdditiveContoursDelegate(void* state, double z, void* slice);
        public receiveAdditionalAdditiveContoursDelegate receiveAdditionalAdditiveContours;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void receiveInputSliceDelegate(void* state, void* slice);
        public receiveInputSliceDelegate receiveInputSlice;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void computeOutputSlicesDelegate(void* state);
        public computeOutputSlicesDelegate computeOutputSlices;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void* giveOutputIfAvailableDelegate(void* state);
        public giveOutputIfAvailableDelegate giveOutputIfAvailable;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate LoadPathFileInfo loadPathsFileDelegate([MarshalAs(UnmanagedType.LPStr)] string pathsfile);
        public loadPathsFileDelegate loadPathsFile;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate void freePathsFileDelegate(void* pathshandle);
        public freePathsFileDelegate freePathsFile;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl), SuppressUnmanagedCodeSecurity]
        public unsafe delegate LoadPathInfo loadNextPathsDelegate(void* pathshandle);
        public loadNextPathsDelegate loadNextPaths;

        [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        internal static extern IntPtr LoadLibrary(string libname);

        [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        internal static extern IntPtr GetModuleHandle(string libname);

        [DllImport("kernel32.dll", CharSet = CharSet.Auto)]
        internal static extern bool FreeLibrary(IntPtr hModule);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
        internal static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
        internal static extern uint GetLastError();

        internal IntPtr DllPointer = IntPtr.Zero;

        public string err;

        public bool disposed;

        public unsafe void* config;
        public double factor_input_to_internal;
        public double factor_internal_to_input;
        public double factor_slicer_to_internal;

        public bool use_slicerdebugfile;
        public string slicerdebugfile;
        public string slicerexepath;
        public string slicerworkdir;

        //basic dll initialization
        public MultiSlicerDllHandler(string dllpath) {
            disposed = true;
            err = null;
            unsafe {
                config = null;
                //uint code1 = GetLastError();
                uint codea = GetLastError();
                DllPointer = LoadLibrary(dllpath);
                uint codeb = GetLastError();
                if (DllPointer == IntPtr.Zero) {
                    uint code2 = GetLastError();
                    //throw new ApplicationException("error codes " + code1 + " and " + code2 + ". Could not load this DLL: " + DLLNAME);
                    throw new ApplicationException("Could not load this DLL: " + dllpath);
                }
                disposed = false;
                try {
                    getErrorText                      = (getErrorTextDelegate)                     Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "getErrorText"),                      typeof(getErrorTextDelegate));
                    readConfiguration                 = (readConfigurationDelegate)                Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "readConfiguration"),                 typeof(readConfigurationDelegate));
                    getParameterHelp                  = (getParameterHelpDelegate)                 Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "getParameterHelp"),                  typeof(getParameterHelpDelegate));
                    freeParameterHelp                 = (freeParameterHelpDelegate)                Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "freeParameterHelp"),                 typeof(freeParameterHelpDelegate));
                    parseArguments                    = (parseArgumentsDelegate)                   Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "parseArguments"),                    typeof(parseArgumentsDelegate));
                    getParamsExtract                  = (getParamsExtractDelegate)                 Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "getParamsExtract"),                  typeof(getParamsExtractDelegate));
                    getConfigExtract                  = (getConfigExtractDelegate)                 Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "getConfigExtract"),                  typeof(getConfigExtractDelegate));
                    freeState                         = (freeStateDelegate)                        Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "freeState"),                         typeof(freeStateDelegate));
                    freeConfig                        = (freeConfigDelegate)                       Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "freeConfig"),                        typeof(freeConfigDelegate));
                    createInputSlice                  = (createInputSliceDelegate)                 Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "createInputSlice"),                  typeof(createInputSliceDelegate));
                    getPathsArray                     = (getPathsArrayDelegate)                    Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "getPathsArray"),                     typeof(getPathsArrayDelegate));
                    computeResult                     = (computeResultDelegate)                    Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "computeResult"),                     typeof(computeResultDelegate));
                    freeInputSlice                    = (freeInputSliceDelegate)                   Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "freeInputSlice"),                    typeof(freeInputSliceDelegate));
                    alsoComplementary                 = (alsoComplementaryDelegate)                Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "alsoComplementary"),                 typeof(alsoComplementaryDelegate));
                    getOutputSliceInfo                = (getOutputSliceInfoDelegate)               Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "getOutputSliceInfo"),                typeof(getOutputSliceInfoDelegate));
                    freeResult                        = (freeResultDelegate)                       Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "freeResult"),                        typeof(freeResultDelegate));
                    computeSlicesZs                   = (computeSlicesZsDelegate)                  Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "computeSlicesZs"),                   typeof(computeSlicesZsDelegate));
                    receiveAdditionalAdditiveContours = (receiveAdditionalAdditiveContoursDelegate)Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "receiveAdditionalAdditiveContours"), typeof(receiveAdditionalAdditiveContoursDelegate));
                    receiveInputSlice                 = (receiveInputSliceDelegate)                Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "receiveInputSlice"),                 typeof(receiveInputSliceDelegate));
                    computeOutputSlices               = (computeOutputSlicesDelegate)              Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "computeOutputSlices"),               typeof(computeOutputSlicesDelegate));
                    giveOutputIfAvailable             = (giveOutputIfAvailableDelegate)            Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "giveOutputIfAvailable"),             typeof(giveOutputIfAvailableDelegate));
                    loadPathsFile                     = (loadPathsFileDelegate)                    Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "loadPathsFile"),                     typeof(loadPathsFileDelegate));
                    freePathsFile                     = (freePathsFileDelegate)                    Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "freePathsFile"),                     typeof(freePathsFileDelegate));
                    loadNextPaths                     = (loadNextPathsDelegate)                    Marshal.GetDelegateForFunctionPointer(GetProcAddress(DllPointer, "loadNextPaths"),                     typeof(loadNextPathsDelegate));

                } catch (Exception e) {
                    uint code  = GetLastError();
                    FreeLibrary(DllPointer);
                    DllPointer = IntPtr.Zero;
                    throw new ApplicationException("Error loading DLL functions (GetLastError was " + code + ") ", e);
                }

            }
        }

        public void loadConfiguration(string configfilename) {
            if (configfilename!=null) unsafe {
                void * newconfig = readConfiguration(configfilename);
                if (hasError(newconfig)) {
                    freeConfig(newconfig);
                    throw new ApplicationException("Error loading DLL configuration (the reported error was " + err + ") ");
                }
                ConfigExtractInfo info    = getConfigExtract(newconfig);
                if (config != null) {
                    freeConfig(config);
                }
                config = newconfig;
                factor_input_to_internal  = info.factor_input_to_internal;
                factor_internal_to_input  = info.factor_internal_to_input;
                factor_slicer_to_internal = info.factor_slicer_to_internal;
                slicerworkdir             = Marshal.PtrToStringAnsi(info.slicerWorkdir);
                slicerexepath             = Marshal.PtrToStringAnsi(info.slicerExePath);
                use_slicerdebugfile       = info.useSlicerDebugFile!=0;
                if (use_slicerdebugfile) {
                    slicerdebugfile       = Marshal.PtrToStringAnsi(info.slicerDebugFile);
                }
            }
        }

        public void dodispose() {
            if (!disposed) unsafe {
                    GC.SuppressFinalize(this);

                    if (DllPointer != IntPtr.Zero) try {
                            if (config != null) freeConfig(config);
                        } finally {
                            FreeLibrary(DllPointer);
                            DllPointer = IntPtr.Zero;
                            disposed = true;
                        }
                }
        }

        void IDisposable.Dispose() {
            dodispose();
        }


        ~MultiSlicerDllHandler() {
            dodispose();
        }

        public string getParameterHelpString(bool showGlobals, bool showPerProcess, bool showExample) {
            unsafe {
                IntPtr helpstr = getParameterHelp(showGlobals ? 1 : 0, showPerProcess ? 1 : 0, showExample ? 1 : 0);
                string ret = Marshal.PtrToStringAnsi(helpstr);
                freeParameterHelp(helpstr);
                return ret;
            }
        }

        public unsafe bool hasError(void* obj) {
            IntPtr ret = getErrorText(obj);
            if (ret == IntPtr.Zero) return false;
            string err_str = Marshal.PtrToStringAnsi(ret);
            if (err_str.Length == 0) return false;
            err = err_str;
            return true;
        }
    }

    //BASIC WRAPPER 1: USE THE DLL TO LOAD *.PATHS FILES
    [SuppressUnmanagedCodeSecurity]
    public class MultiSlicerLoader : IDisposable {
        public MultiSlicerDllHandler dll;
        public bool disposed;

        public unsafe LoadPathFileInfo pathsfilestate;
        public bool pathsfileeof;
        public string pathsfilename;


        public MultiSlicerLoader(MultiSlicerDllHandler d, string fname) {
            unsafe {
                disposed = false;
                pathsfilestate.pathfile = null;

                dll = d;
                pathsfilestate = dll.loadPathsFile(fname);
                if (pathsfilestate.pathfile == null) {
                    throw new ApplicationException("The library could not open file " + fname);
                }
                if (dll.hasError(pathsfilestate.pathfile)) {
                    throw new ApplicationException("Error trying to open file " + fname);
                }
                pathsfileeof = false;
                pathsfilename = fname;
            }
        }

        public void dodispose() {
            if (!disposed) unsafe {
                    disposed = true;
                    GC.SuppressFinalize(this);

                    if ((dll != null) && (pathsfilestate.pathfile != null)) dll.freePathsFile(pathsfilestate.pathfile);
                }
        }

        void IDisposable.Dispose() {
            dodispose();
        }


        ~MultiSlicerLoader() {
            dodispose();
        }

        public int pathsFileNTools() {
            unsafe {
                if (pathsfilestate.pathfile == null) {
                    throw new ApplicationException("Tried to read pathsfile's number of tools, but no pathsfile is currently open!");
                }
                return pathsfilestate.ntools;
            }
        }

        public int pathsFileNumRecords() {
            unsafe {
                if (pathsfilestate.pathfile == null) {
                    throw new ApplicationException("Tried to read pathsfile's number of records, but no pathsfile is currently open!");
                }
                return pathsfilestate.numRecords;
            }
        }

        public void closePathsFile() {
            unsafe {
                if (pathsfilestate.pathfile != null) {
                    dll.freePathsFile(pathsfilestate.pathfile);
                    pathsfilestate.pathfile = null;
                }
            }
        }

        public bool readNextPathFromFile(ref bool pathsAreDoubles, ref MultiSlicerInterface.LoadPathInfo info) {
            unsafe {
                if (pathsfileeof) {
                    return false;
                }
                info = dll.loadNextPaths(pathsfilestate.pathfile);
                if (dll.hasError(pathsfilestate.pathfile)) throw new ApplicationException("Error while loading paths: " + dll.err);
                if (info.numRecord < 0) {
                    pathsfileeof = true;
                    return false;
                }
                pathsAreDoubles = info.saveFormat != (int)LoadPathFormat.PATHFORMAT_INT64;
                if (info.saveFormat == (int)LoadPathFormat.PATHFORMAT_DOUBLE_3D) {
                    throw new ApplicationException("In file " + pathsfilename + ", record " + info.numRecord + " is a 3D Paths, but 3D Paths cannot currently be loaded");
                }
                return true;
            }
        }

    }

    //BASIC WRAPPER 2: BASIC METHODS TO USE THE DLL TO PROCESS RAW SLICES AND GENERATE PROCESSED CONTOURS AND TOOLPATHS
    [SuppressUnmanagedCodeSecurity]
    public class MultiSlicerHandler : IDisposable {
        public MultiSlicerDllHandler dll;
        public bool disposed;

        public unsafe void* state;

        public int numProcesses;
        public double[] processRadiuses;

        public bool alsoContours;
        public bool usingScheduler;
        public bool use_z_base;

        public double z_base;
        public double z_uniform_step;

        public int numoutputslices = 0, numreceivedoutputslices = 0;

        public MultiSlicerHandler(MultiSlicerDllHandler d, string arguments) {
            dll = d;
            disposed = false;
            unsafe {
                state = null;

                state = dll.parseArguments(dll.config, arguments);
                if (dll.hasError(state)) {
                    if (state != null) {
                        dll.freeState(state);
                        state = null;
                    }
                    throw new ApplicationException("Error parsing arguments for the multislicer: " + dll.err);
                }
                ParamsExtractInfo info = dll.getParamsExtract(state);
                numProcesses    = info.numProcesses;
                processRadiuses = new double[numProcesses];
                alsoContours    = info.alsoContours != 0;
                usingScheduler  = info.usingScheduler != 0;
                use_z_base      = info.use_z_base != 0;
                if (use_z_base) {
                    z_base = info.z_base;
                }
                if (!usingScheduler) {
                    z_uniform_step = info.z_uniform_step;
                }
                for (int k = 0; k < numProcesses; ++k) {
                    processRadiuses[k] = info.processRadiuses[k] * dll.factor_internal_to_input;
                }
            }
        }

        public void dodispose() {
            if (!disposed) unsafe {
                    GC.SuppressFinalize(this);

                    if (state != null) dll.freeState(state);
                }
        }

        void IDisposable.Dispose() {
            dodispose();
        }


        ~MultiSlicerHandler() {
            dodispose();
        }

        public unsafe void* feedRawSliceIntoDll(IntPoint[][] rawslice) {
            int numpaths = (int)rawslice.Length;
            InputSliceInfo info = dll.createInputSlice(numpaths);
            try {
                int* numpointss = info.numpointsArray;
                for (int i = 0; i < numpaths; i++) {
                    *(numpointss++) = (int)rawslice[i].Length;
                }
                long** paths = dll.getPathsArray(info.slice);
                numpointss = info.numpointsArray;
                for (int i = 0; i < numpaths; ++i) {
                    int numpoints = *(numpointss++);
                    //System.Windows.Forms.Application.DoEvents();
                    long* path = *(paths++);
                    IntPoint[] pth = rawslice[i];
                    for (int j = 0; j < numpoints; j++) {
                        *(path++) = (long)(pth[j].x * dll.factor_slicer_to_internal);
                        *(path++) = (long)(pth[j].y * dll.factor_slicer_to_internal);
                    }
                }
            } catch {
                dll.freeInputSlice(info.slice);
                throw;
            }
            return info.slice;

        }

        //this is an example of how to use receiveAdditionalAdditiveContours(), actual implementations will likely use it to feed online data instead of slices from STL files
        public unsafe void addAdditionalAdditiveContour(double z, ExternalSlicerManager external) {
            void* slice = feedRawSliceIntoDll(external.readSlice());
            try {
                dll.receiveAdditionalAdditiveContours(state, z * dll.factor_input_to_internal, slice);
            } finally {
                dll.freeInputSlice(slice);
            }
        }

        public unsafe OutputSliceInfo readOutputSlice(void* obj, int idx, MultiCfg.PathType pathtype) {
            return dll.getOutputSliceInfo(obj, idx, (int)pathtype);
        }

        public unsafe void* computeSlice2D(void* slice) {
            void* result = null;
            try {
                result = dll.computeResult(slice, state);
                if (dll.hasError(result)) {
                    dll.freeInputSlice(slice);
                    slice = null;
                    if (result != null) dll.freeResult(result);
                    return null;
                }
            } finally {
                if (slice != null) dll.freeInputSlice(slice);
            }
            return result;
        }

        public unsafe double[] getSlicesZs(double zmin, double zmax) {
            zmin *= dll.factor_input_to_internal;
            zmax *= dll.factor_input_to_internal;
            Slices3DSpecInfo ret = dll.computeSlicesZs(state, zmin, zmax);
            if (dll.hasError(state)) {
                throw new ApplicationException("Error in computeSlicesZs(): " + dll.err);
            }
            double []sliceZs = new double[ret.numinputslices];
            double* zsp = ret.zs;
            for (int k = 0; k < ret.numinputslices; ++k) {
                sliceZs[k] = (*(zsp++)) * dll.factor_internal_to_input;
            }
            numoutputslices = ret.numoutputslices;
            return sliceZs;
        }

        public unsafe void feedSliceToScheduler(void* slice) {
            dll.receiveInputSlice(state, slice);
            dll.freeInputSlice(slice);
            if (dll.hasError(state)) {
                throw new ApplicationException("Error in receiveInputSlice(): " + dll.err);
            }
            dll.computeOutputSlices(state);
            if (dll.hasError(state)) {
                throw new ApplicationException("Error in computeOutputSlices(): " + dll.err);
            }
        }

        public unsafe void* getResultFromScheduler() {
            if (numreceivedoutputslices == numoutputslices) {
                return null;
            }
            void* result = dll.giveOutputIfAvailable(state);
            if (result != null) {
                numreceivedoutputslices++;
            }
            if (dll.hasError(state)) {
                if (result != null) {
                    dll.freeResult(result);
                }
                throw new ApplicationException("Error in giveOutputIfAvailable(): " + dll.err);
            }
            return result;
        }
    }

}
