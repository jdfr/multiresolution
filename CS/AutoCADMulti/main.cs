using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Autodesk.AutoCAD.Runtime;
using Autodesk.AutoCAD.Geometry;
using Autodesk.AutoCAD.DatabaseServices;
using Autodesk.AutoCAD.ApplicationServices;

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
