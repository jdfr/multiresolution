using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Autodesk.AutoCAD.Runtime;
using Autodesk.AutoCAD.Geometry;
using Autodesk.AutoCAD.DatabaseServices;
using Autodesk.AutoCAD.ApplicationServices;
using ACM = AutoCADMulti;

// AutoCAD plugin adding a bare-bones GUI to use the multislicing engine

/*HOW TO USE THIS PLUGIN:
 *    a) compile the project (the dependencies should also be compiled and, if necessary, put in the same output folder)
 *    b) open AutoCAD
 *    c) in AutoCAD, create or open a document
 *    d) in AutoCAD, execute the command NETLOAD in AutoCAD, navigate to the directory containing the DLL for this project, and select the DLL for this plugin (AutoCADMultiGUI.dll)
 *    e) in AutoCAD, execute the command MULTISLICER_GUI
 */

[assembly: CommandClass(typeof(AutoCADMultiGUI.main))]

namespace AutoCADMultiGUI {

    public class main {

        static string basepath = System.IO.Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location);

        //these singletons (this one and the one from AutoCADMulti) are not thread-safe, so the command is not, either!!!!

        static maindialog singletondialog = null;

        public delegate void singletonClear();
        static public void clearSingletonDialog() {
            singletondialog = null;
            ACM.main.multislicer_unload_dll();
        }

        //this command launches the visual plugin
        [CommandMethod("multislicer_gui")]
        public void multislicer_gui() {
            if (singletondialog == null) {
                singletondialog = new maindialog(basepath, ACM.main.getServices(), clearSingletonDialog);
                Application.ShowModelessDialog(singletondialog);
            } else {
                singletondialog.Activate();
            }
        }

    }
}
