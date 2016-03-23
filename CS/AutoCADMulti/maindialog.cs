using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using SD = System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Autodesk.AutoCAD.Geometry;
using Autodesk.AutoCAD.ApplicationServices;
using Autodesk.AutoCAD.DatabaseServices;
using Autodesk.AutoCAD.Colors;
using SI = MultiSlicerInterface;

namespace AutoCADMulti {
    public partial class maindialog : Form {
        bool doclose;
        Exception closeException;

        string dllpath;
        string configdir, currentconfigname;
        string stldir;
        string pathsdir;

        SI.MultiSlicerDllHandler dll;

        main.singletonClear singletonClear=null;

        private static readonly byte VALM = 128;
        private static readonly byte VALG = 200;
        private static readonly byte VALT = 255;
        private static readonly byte VALC = 64;
        private static readonly Color[] processColors = {
          Color.FromRgb(VALT, 0, 0),
          Color.FromRgb(0, VALT, 0),
          Color.FromRgb(0, 0, VALT),
          Color.FromRgb(VALT, VALT, 0),
          Color.FromRgb(VALT, 0, VALT),
          Color.FromRgb(0, VALT, VALT),
        };
        private static readonly Color otherColor = Color.FromRgb(VALM, VALM, VALM);
        private static readonly Color globalContourColor = Color.FromRgb(VALG, VALG, VALG);
        private static readonly Color[] contourColors = {
          Color.FromRgb(VALC, 0, 0),
          Color.FromRgb(0, VALC, 0),
          Color.FromRgb(0, 0, VALC),
          Color.FromRgb(VALC, VALC, 0),
          Color.FromRgb(VALC, 0, VALC),
          Color.FromRgb(0, VALC, VALC),
        };

        public maindialog(main.singletonClear singletonClear) {
            try {
                this.singletonClear = singletonClear;
                doclose             = false;
                string basepath     = System.IO.Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location);
                configdir           = basepath;
                stldir              = basepath;
                pathsdir            = basepath;
                currentconfigname   = null;
                InitializeComponent();
                configFileTextBox.Text = System.IO.Path.Combine(configdir, "config.txt");
                this.MinimumSize       = this.Size;
                dllpath = System.IO.Path.Combine(basepath, "multires.dll");
                dll     = new SI.MultiSlicerDllHandler(dllpath);
            } catch (ApplicationException e) {
                closeException = e;
                doclose = true;
            }
        }

        //Close the form if there is an initialization error
        private void maindialog_Load(object sender, EventArgs e) {
            if (doclose) {
                this.Close();
                Autodesk.AutoCAD.ApplicationServices.Application.ShowAlertDialog(closeException.Message);
            }
        }

        //handle DLL and singleton on closing
        private void maindialog_FormClosed(object sender, FormClosedEventArgs e) {
            if (singletonClear != null) {
                singletonClear();
            }
            if (dll != null) {
                dll.dodispose();
                dll = null;
            }
        }

        //interface logic
        private void useMultislicing_CheckedChanged(object sender, EventArgs e) {
            bool multislicing             = useMultislicing.Checked;
            sliceStepLabel       .Enabled = !multislicing;
            sliceStepTextBox     .Enabled = !multislicing;
            paramLabel           .Enabled =  multislicing;
            helpButton           .Enabled =  multislicing;
            sliceGetOnlyToolpaths.Enabled =  multislicing;
            paramTextBox         .Enabled =  multislicing;
        }

        //helper method for selecting files
        private void useOpenFileDialog(string filter, ref string dir, TextBox textbox) {
            string originaldir        = ofdialog.InitialDirectory;
            ofdialog.Filter           = filter;
            ofdialog.FilterIndex      = 1;
            ofdialog.Multiselect      = false;
            ofdialog.InitialDirectory = dir;
            ofdialog.AddExtension     = true;
            ofdialog.FileName         = "";
            if (ofdialog.ShowDialog() == System.Windows.Forms.DialogResult.OK) {
                string filename       = ofdialog.FileName;
                textbox.Text          = filename;
                dir                   = System.IO.Path.GetDirectoryName(filename);
            }
            ofdialog.InitialDirectory = originaldir;
        }

        private void configFileButton_Click(object sender, EventArgs e) {
            useOpenFileDialog("multislicing configuration files (*.txt)|*.txt", ref configdir, configFileTextBox);
        }

        private void stlFileButton_Click(object sender, EventArgs e) {
            useOpenFileDialog("3D mesh file (*.stl)|*.stl", ref stldir, stlFileTextBox);
        }

        private void pathsFileButton_Click(object sender, EventArgs e) {
            useOpenFileDialog("Multislicing paths file (*.paths)|*.paths", ref pathsdir, pathsFileTextBox);
        }

        private void helpButton_Click(object sender, EventArgs e) {
            Form frm          = new Form();
            frm.Text          = "Multislicing Parameter Help";
            TextBox txtbox    = new TextBox();
            txtbox.Font       = new System.Drawing.Font(SD.FontFamily.GenericMonospace, txtbox.Font.Size);
            txtbox.Multiline  = true;
            txtbox.ScrollBars = ScrollBars.Both;
            txtbox.ReadOnly   = true;
            txtbox.SetBounds(0, 0, 600, 600);
            const bool showGlobals    = true;
            const bool showPerProcess = true;
            const bool showExample    = true;
            txtbox.AppendText(dll.getParameterHelpString(showGlobals, showPerProcess, showExample).Replace("\n", "\r\n"));
            frm.ClientSize = txtbox.Size;
            frm.Controls.Add(txtbox);
            frm.Show();
            frm.Select();
        }

        private delegate void AutoCadDocumentModifier(Transaction tr, BlockTableRecord btr);

        //AutoCAD document modification boilerplate
        private void modifyAutoCADDocument(AutoCadDocumentModifier modifier) {
            var doc = Autodesk.AutoCAD.ApplicationServices.Application.DocumentManager.MdiActiveDocument;
            var db  = doc.Database;
            var ed  = doc.Editor;
            using (DocumentLock lck      = doc.LockDocument()) {
                using (Transaction tr    = db.TransactionManager.StartTransaction()) {
                    BlockTable bt        = db.BlockTableId.GetObject(OpenMode.ForWrite) as BlockTable;
                    BlockTableRecord btr = tr.GetObject(bt[BlockTableRecord.ModelSpace], OpenMode.ForWrite) as BlockTableRecord;
                    modifier(tr, btr);
                    tr.Commit();
                    ed.Regen();
                    ed.UpdateScreen();
                }
            }
        }

        private void addPolyline(Transaction tr, BlockTableRecord btr, Polyline p) {
            btr.AppendEntity(p);
            tr.AddNewlyCreatedDBObject(p, true);
        }

        //helper method to load the DLL configuration
        private void loadConfig() {
            string configname = configFileTextBox.Text;
            if ((configname == null) || (configname.Length == 0)) {
                throw new ApplicationException("no config file was defined!");
            }
            if (configname != currentconfigname) {
                dll.loadConfiguration(configname);
                currentconfigname = configname;
            }
        }

        //load configuration
        private void configFileCheck_Click(object sender, EventArgs e) {
            try {
                loadConfig();
            } catch (ApplicationException ae) {
                Autodesk.AutoCAD.ApplicationServices.Application.ShowAlertDialog(ae.Message);
            }
        }

        private delegate void ActionForFile(string fname);

        //boilerplate to perform action over a file
        private void doActionForFile(TextBox txtbox_fname, ActionForFile action) {
            string file = txtbox_fname.Text;
            if (!System.IO.File.Exists(file)) {
                Autodesk.AutoCAD.ApplicationServices.Application.ShowAlertDialog("This file does not exist: " + file);
                return;
            }
            try {
                loadConfig();
                action(file);
            } catch (ApplicationException ae) {
                Autodesk.AutoCAD.ApplicationServices.Application.ShowAlertDialog(ae.Message);
            }
        }

        //load *.paths file
        private unsafe void loadAddSlices_Click(object sender, EventArgs e) {
            doActionForFile(pathsFileTextBox, (string pathsfile) => {
                int justNtool;
                bool useJustNtool = Int32.TryParse(ntoolTextBox.Text, out justNtool);
                modifyAutoCADDocument((Transaction tr, BlockTableRecord btr) => {
                    using (SI.MultiSlicerLoader loader = new SI.MultiSlicerLoader(dll, pathsfile)) {
                        int numRecords  = loader.pathsFileNumRecords();
                        long**   paths  = null;
                        double** pathsd = null;
                        bool isDouble   = false;
                        SI.LoadPathInfo info = new SI.LoadPathInfo();
                        bool onlyToolpaths = loadGetOnlyToolpaths.Checked;
                        while (loader.readNextPathFromFile(ref isDouble, ref info)) {
                            if (onlyToolpaths && info.type != (int)MultiSlicerInterface.MultiCfg.LoadPathType.PATHTYPE_TOOLPATH) {
                                continue;
                            }
                            if (isDouble) {
                                pathsd = (double**)info.pathsArray;
                            } else {
                                paths = (long**)info.pathsArray;
                            }
                            long*   path  = null;
                            double* pathd = null;
                            double scalingFactor = info.scaling;
                            int ntool = info.ntool;
                            if (useJustNtool && ntool != justNtool) {
                                continue;
                            }
                            Color col;
                            switch (info.type) {
                                case (int)MultiSlicerInterface.MultiCfg.LoadPathType.PATHTYPE_TOOLPATH:
                                    col = processColors[ntool]; break;
                                case (int)MultiSlicerInterface.MultiCfg.LoadPathType.PATHTYPE_RAW_CONTOUR:
                                    col = globalContourColor;   break;
                                case (int)MultiSlicerInterface.MultiCfg.LoadPathType.PATHTYPE_PROCESSED_CONTOUR:
                                    col = contourColors[ntool]; break;
                                default:
                                    col = otherColor;           break; //this should never happen
                            }
                            for (int i = 0; i < info.numpaths; i++) {
                                int numpoints = *(info.numpointsArray++);
                                Polyline p = new Polyline();
                                if (numpoints > 0) {
                                    if (isDouble) {
                                        pathd = *(pathsd++);
                                        for (int j = 0; j < numpoints; j++) {
                                            p.AddVertexAt(j, new Point2d((*(pathd++)), (*(pathd++))), 0, 0, 0);
                                        }
                                    } else {
                                        path = *(paths++);
                                        for (int j = 0; j < numpoints; j++) {
                                            p.AddVertexAt(j, new Point2d((*(path++)) * scalingFactor, (*(path++)) * scalingFactor), 0, 0, 0);
                                        }
                                    }
                                }
                                p.Color     = col;
                                p.Elevation = info.z;
                                addPolyline(tr, btr, p);
                            }
                        }
                    }
                });
            });
        }

        //slicing common boilerplate
        private void sliceAddslices_Click(object sender, EventArgs e) {
            doActionForFile(stlFileTextBox, (string stlfile) => {
                if (useMultislicing.Checked) {
                    multislice(stlfile);
                } else {
                    externalSlice(stlfile);
                }
            });
        }

        //helper method to create a slicer manager
        private SI.ExternalSlicerManager createExternalSlicerManager(string stlfile) {
            const bool repair      = true;
            const bool incremental = true;
            return new SI.ExternalSlicerManager(dll.slicerworkdir, dll.slicerexepath, dll.use_slicerdebugfile ? "debug.autocad.txt" : null, repair, incremental, stlfile);
        }

        //helper method to show in autocad an slice from the external slicer
        private void showExternalSlice(SI.IntPoint[][] paths, double scalingFactor, double z, Transaction tr, BlockTableRecord btr) {
            for (int pi = 0; pi < paths.Length; ++pi) {
                SI.IntPoint[] path = paths[pi];
                Polyline p = new Polyline();
                for (int ppi = 0; ppi < path.Length; ++ppi) {
                    p.AddVertexAt(ppi, new Point2d(path[ppi].x * scalingFactor, path[ppi].y * scalingFactor), 0, 0, 0);
                }
                p.Color     = globalContourColor;
                p.Elevation = z;
                addPolyline(tr, btr, p);
            }
        }

        //use only external slice
        private void externalSlice(string stlfile) {
            double zstep = 0;
            if (!Double.TryParse(sliceStepTextBox.Text, out zstep)) {
                throw new ApplicationException("Invalid Z step value: " + sliceStepTextBox.Text);
            }
            using (SI.ExternalSlicerManager external = createExternalSlicerManager(stlfile)) {
                try {
                    double[] zs = external.prepareSTLSimple(zstep);
                    int numslices = zs.Length;
                    double scalingFactor = external.scalingFactor;
                    modifyAutoCADDocument((Transaction tr, BlockTableRecord btr) => {
                        for (int i = 0; i < numslices; ++i) {
                            SI.IntPoint[][] paths = external.readSlice();
                            showExternalSlice(paths, scalingFactor, zs[i], tr, btr);
                        }
                    });
                } catch {
                    external.terminate();
                    throw;
                }
            }
        }

        //multislicing boilerplate code
        private void multislice(string stlfile) {
            string arguments = paramTextBox.Text.Trim();
            if (arguments.Length==0) {
                throw new ApplicationException("Arguments must be provided!!!");
            }
            using (SI.ExternalSlicerManager external = createExternalSlicerManager(stlfile)) {
                try {
                    using (SI.MultiSlicerHandler handler = new SI.MultiSlicerHandler(dll, arguments)) {
                        bool alsoContours = !sliceGetOnlyToolpaths.Checked && (handler.alsoContours);
                        modifyAutoCADDocument((Transaction tr, BlockTableRecord btr) => {
                            if (handler.usingScheduler) {
                                multislice_3dscheduler(external, handler, alsoContours, tr, btr);
                            } else {
                                multislice_2dsimple   (external, handler, alsoContours, tr, btr);
                            }
                        });
                    }
                } catch {
                    external.terminate();
                    throw;
                }
            }
        }

        //helper method to read an output slice
        private unsafe void readOutputSlice(void* result, bool mode2D, double z, int sliceIdx, MultiSlicerInterface.MultiCfg.PathType pathtype, Color[] colorList, Transaction tr, BlockTableRecord btr) {
            Color col;
            SI.OutputSliceInfo info = dll.getOutputSliceInfo(result, sliceIdx, (int)pathtype);
            int numpaths         = info.numpaths;
            if (numpaths == 0) {
                return;
            }
            int* numpointss      = info.numpointsArray;
            long **paths         = info.pathsArray;
            double scalingFactor = dll.factor_internal_to_input;
            if (!mode2D) {
                z   = info.z * dll.factor_internal_to_input;
                col = colorList[info.ntool];
            } else {
                col = colorList[sliceIdx];
            }
            for (int i = 0; i < numpaths; i++) {
                int numpoints = *(numpointss++);
                long* path = *(paths++);
                Polyline p = new Polyline();
                if (numpoints > 0) {
                    for (int j = 0; j < numpoints; j++) {
                        p.AddVertexAt(j, new Point2d((*(path++)) * scalingFactor, (*(path++)) * scalingFactor), 0, 0, 0);
                    }
                    p.Color     = col;
                    p.Elevation = z;
                    addPolyline(tr, btr, p);
                }
            }
        }

        //helper method to read all output
        private unsafe void readOutputSlices(void* result, bool mode2D, bool alsoContours, double z, int sliceIdx, Transaction tr, BlockTableRecord btr) {
            /*if (dll.alsoComplementary(result, j) {
                readOutputSlice(result, mode2D, z, sliceIdx, SI.MultiCfg.PathType.PathInfillingAreas, processColors, tr, btr);
            }*/
            //something like the commented code above would be necessary to retrieve the areas to infill, if we were going to handle infillings outside of the multislicing engine
            readOutputSlice(result, mode2D, z, sliceIdx, SI.MultiCfg.PathType.PathToolPath, processColors, tr, btr);
            if (!alsoContours) return;
            readOutputSlice(result, mode2D, z, sliceIdx, SI.MultiCfg.PathType.PathContour,  contourColors, tr, btr);
        }

        //2d simple multislicing
        private unsafe void multislice_2dsimple(SI.ExternalSlicerManager external, SI.MultiSlicerHandler handler, bool alsoContours, Transaction tr, BlockTableRecord btr) {
            double zstep  = handler.z_uniform_step;
            double[] zs   = handler.use_z_base ? external.prepareSTLSimple(zstep, handler.z_base) :
                                                 external.prepareSTLSimple(zstep);
            int numslices = zs.Length;
            for (int i = 0; i < numslices; ++i) {
                SI.IntPoint[][] rawslice = external.readSlice();
                if (rawslice.Length == 0) {
                    continue;
                }
                if (alsoContours) {
                    showExternalSlice(rawslice, external.scalingFactor, zs[i], tr, btr);
                }
                void* slice  = handler.feedRawSliceIntoDll(rawslice);
                rawslice     = null;
                void* result = handler.computeSlice2D(slice); //the slice is freed in computeSlice2D()
                if (result == null) {
                    throw new ApplicationException("Error in compute2D: " + dll.err);
                }
                try {
                    const bool mode2D = true;
                    int ntools = handler.numProcesses;
                    for (int j = 0; j < ntools; ++j) {
                        readOutputSlices(result, mode2D, alsoContours, zs[i], j, tr, btr);
                    }
                } finally {
                    if (result != null) dll.freeResult(result);
                }
            }

        }

        //multislicing with 3d scheduling
        private unsafe void multislice_3dscheduler(SI.ExternalSlicerManager external, SI.MultiSlicerHandler handler, bool alsoContours, Transaction tr, BlockTableRecord btr) {
            
            double minx, maxx, miny, maxy, minz, maxz;
            external.getBoundingBox(out minx, out maxx, out miny, out maxy, out minz, out maxz);
            double[] zs = handler.getSlicesZs(minz, maxz);
            external.sendZs(zs);

            for (int i = 0; i < zs.Length; ++i) {
                SI.IntPoint[][] rawslice = external.readSlice();
                if ((rawslice.Length > 0) && (alsoContours)) {
                    showExternalSlice(rawslice, external.scalingFactor, zs[i], tr, btr);
                }
                //here, it may be the moment to use dll.receiveAdditionalAdditiveContours() if we were using online feedback...
                void* slice = handler.feedRawSliceIntoDll(rawslice);
                rawslice    = null;
                handler.feedSliceToScheduler(slice); //the slice is freed in feedSliceToScheduler()
                while(true) {
                    void* result = handler.getResultFromScheduler();
                    if (result == null) {
                        break;
                    }
                    try {
                        const bool mode2D         = false;
                        const double z_not_used   = 0.0; //for !mode2D, z is not used
                        const int single_sliceIdx = 0;   //for !mode2D, there is a single result, not one for each tool
                        readOutputSlices(result, mode2D, alsoContours, z_not_used, single_sliceIdx, tr, btr);
                    } finally {
                        dll.freeResult(result);
                    }
                }

            }
        }

    }
}
