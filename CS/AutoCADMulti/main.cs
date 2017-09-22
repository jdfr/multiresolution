using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Autodesk.AutoCAD.Runtime;
using Autodesk.AutoCAD.Geometry;
using Autodesk.AutoCAD.DatabaseServices;
using Autodesk.AutoCAD.ApplicationServices;

// AutoCAD plugin adding command-line functions to use the multislicing engine

/*HOW TO USE THIS PLUGIN:
 *    a) compile the project (the dependencies should also be compiled and, if necessary, put in the same output folder)
 *    b) open AutoCAD
 *    c) in AutoCAD, create or open a document
 *    d) in AutoCAD, execute the command NETLOAD in AutoCAD, navigate to the directory containing the DLL for this project, and select the DLL for this plugin (AutoCADMulti.dll)
 *    e) in AutoCAD, execute the commands/functions (multislicer_unload_dll, multislice, externalslice, loadpaths)
 */

[assembly: CommandClass(typeof(AutoCADMulti.main))]

namespace AutoCADMulti {

    public class main {

        static string basepath = System.IO.Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location);

        //this singleton is not thread-safe, so commands and functions are not, either!!!!

        static MultiSlicerServices services = null;

        public static MultiSlicerServices getServices() {
            if (services == null) {
                services = new MultiSlicerServices(basepath);
            }
            return services;
        }

        //this command is useful to recompile the DLL without having to exit AutoCAD
        [CommandMethod("multislicer_unload_dll")]
        public static void multislicer_unload_dll() {
            if (services != null) {
                services.dodispose();
                services = null;
            }
        }

        public delegate Object MultislicerAction(MultiSlicerServices services, string configname, string file, TypedValue []tvarr);
        public Object lispAction(ResultBuffer rb, int numAdditionalArguments, MultislicerAction action) {
            Object ret = null;
            if (rb == null) return ret;
            TypedValue[] tvarr = rb.AsArray();
            if (tvarr.Length < (2+numAdditionalArguments)) return ret;
            TypedValue param1 = tvarr[0];
            TypedValue param2 = tvarr[1];
            if (param1.TypeCode!=(int)LispDataType.Text) return ret;
            if (param2.TypeCode!=(int)LispDataType.Text) return ret;
            string configname = param1.Value as string;
            string file       = param2.Value as string;
            TypedValue[] newarr = new TypedValue[tvarr.Length-2];
            Array.Copy(tvarr, 2, newarr, 0, newarr.Length);
            return action(getServices(), configname, file, newarr);
        }

        //this function provides a convenient command-line mode to access the functionality of the plugin to multislice
        [LispFunction("multislice")]
        public Object multislice(ResultBuffer rb) {
            return lispAction(rb, 1, (MultiSlicerServices services, string configname, string stlfile, TypedValue[] tvarr) => {
                Object ret = null;
                if (tvarr.Length < 1) return ret;
                TypedValue param1 = tvarr[0];
                if (param1.TypeCode!=(int)LispDataType.Text) return ret;
                string arguments = param1.Value as string;
                services.multislice(configname, false, arguments, stlfile);
                return ret;
            });
        }

        //this function provides a convenient command-line mode to access the functionality of the plugin to use the external slicer
        [LispFunction("externalslice")]
        public Object externalslice(ResultBuffer rb) {
            return lispAction(rb, 1, (MultiSlicerServices services, string configname, string stlfile, TypedValue[] tvarr) => {
                Object ret = null;
                if (tvarr.Length < 1) return ret;
                TypedValue param1 = tvarr[0];
                if (param1.TypeCode != (int)LispDataType.Double) return ret;
                double zstep = (double)param1.Value;
                services.externalSlice(configname, zstep, stlfile);
                return ret;
            });
        }

        //this function provides a convenient command-line mode to access the functionality of the plugin to load pathfiles
        [LispFunction("loadpaths")]
        public Object loadpaths(ResultBuffer rb) {
            return lispAction(rb, 0, (MultiSlicerServices services, string configname, string pathsfile, TypedValue[] tvarr) => {
                services.loadAddSlices(configname, pathsfile, false, false, 0);
                return null;
            });
        }
    }
}
