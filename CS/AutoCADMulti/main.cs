using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Autodesk.AutoCAD.Runtime;
using Autodesk.AutoCAD.Geometry;
using Autodesk.AutoCAD.DatabaseServices;
using Autodesk.AutoCAD.ApplicationServices;

/*HOW TO USE THIS PLUGIN:
 *    a) compile the project (the dependencies should also be compiled and, if necessary, put in the same output folder)
 *    b) open AutoCAD
 *    c) in AutoCAD, tcreate or open a document
 *    d) in AutoCAD, execute the command NETLOAD in AutoCAD, navigate to the directory containing the DLL for this project, and select the DLL for this plugin (AutoCADMulti.dll)
 *    e) in AutoCAD, execute the command MULTISLICER
 */

[assembly: CommandClass(typeof(AutoCADMulti.main))]

namespace AutoCADMulti {

    public class main {

        static maindialog singleton = null;

        static public void clearSingleton() {
            singleton = null;
        }

        public delegate void singletonClear();

        [CommandMethod("multislicer")]
        public void multislicer() {
            //this is not thread-safe
            if (singleton == null) {
                singleton = new maindialog(clearSingleton);
                Application.ShowModelessDialog(singleton);
            } else {
                singleton.Activate();
            }
        }
    }
}
