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
using ACM = AutoCADMulti;

namespace AutoCADMultiGUI {

    //This class contains the GUI interface
    public partial class maindialog : Form {
        bool doclose;
        Exception closeException;

        string configdir;
        string stldir;
        string pathsdir;

        main.singletonClear singletonClear=null;

        ACM.MultiSlicerServices services;

        public maindialog(string basepath, ACM.MultiSlicerServices s, main.singletonClear singletonClear) {
            try {
                this.singletonClear = singletonClear;
                doclose             = false;
                configdir           = basepath;
                stldir              = basepath;
                pathsdir            = basepath;
                InitializeComponent();
                configFileTextBox.Text = System.IO.Path.Combine(configdir, "config.txt");
                this.MinimumSize       = this.Size;
                services               = s;
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
            txtbox.AppendText(services.getParameterHelp(showGlobals, showPerProcess, showExample));
            frm.ClientSize = txtbox.Size;
            frm.Controls.Add(txtbox);
            frm.Show();
            frm.Select();
        }

        //load configuration
        private void configFileCheck_Click(object sender, EventArgs e) {
            try {
                services.loadConfig(configFileTextBox.Text);
            } catch (ApplicationException ae) {
                Autodesk.AutoCAD.ApplicationServices.Application.ShowAlertDialog(ae.Message);
            }
        }

        //load *.paths file
        private unsafe void loadAddSlices_Click(object sender, EventArgs e) {
            int justNtool;
            bool useJustNtool = Int32.TryParse(ntoolTextBox.Text, out justNtool);
            services.loadAddSlices(configFileTextBox.Text, pathsFileTextBox.Text, loadGetOnlyToolpaths.Checked, useJustNtool, justNtool);
        }

        //slicing common boilerplate
        private void sliceAddslices_Click(object sender, EventArgs e) {
            if (useMultislicing.Checked) {
                services.multislice(configFileTextBox.Text, sliceGetOnlyToolpaths.Checked, paramTextBox.Text.Trim(), stlFileTextBox.Text);
            } else {
                double zstep = 0;
                if (!Double.TryParse(sliceStepTextBox.Text, out zstep)) {
                    throw new ApplicationException("Invalid Z step value: " + sliceStepTextBox.Text);
                }
                services.externalSlice(configFileTextBox.Text, zstep, stlFileTextBox.Text);
            }
       }


    }
}
