namespace AutoCADMultiGUI {
    partial class maindialog {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing) {
            if (disposing && (components != null)) {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent() {
            this.tabs = new System.Windows.Forms.TabControl();
            this.tabConfig = new System.Windows.Forms.TabPage();
            this.configFileCheck = new System.Windows.Forms.Button();
            this.configFileButton = new System.Windows.Forms.Button();
            this.configFileTextBox = new System.Windows.Forms.TextBox();
            this.tabSlice = new System.Windows.Forms.TabPage();
            this.helpButton = new System.Windows.Forms.Button();
            this.sliceGetOnlyToolpaths = new System.Windows.Forms.CheckBox();
            this.paramTextBox = new System.Windows.Forms.TextBox();
            this.paramLabel = new System.Windows.Forms.Label();
            this.useMultislicing = new System.Windows.Forms.CheckBox();
            this.sliceAddslices = new System.Windows.Forms.Button();
            this.sliceStepTextBox = new System.Windows.Forms.TextBox();
            this.sliceStepLabel = new System.Windows.Forms.Label();
            this.stlFileTextBox = new System.Windows.Forms.TextBox();
            this.stlFileButton = new System.Windows.Forms.Button();
            this.tabLoad = new System.Windows.Forms.TabPage();
            this.ntoolTextBox = new System.Windows.Forms.TextBox();
            this.labelOnlyNtool = new System.Windows.Forms.Label();
            this.loadAddSlices = new System.Windows.Forms.Button();
            this.loadGetOnlyToolpaths = new System.Windows.Forms.CheckBox();
            this.pathsFileTextBox = new System.Windows.Forms.TextBox();
            this.pathsFileButton = new System.Windows.Forms.Button();
            this.ofdialog = new System.Windows.Forms.OpenFileDialog();
            this.tabs.SuspendLayout();
            this.tabConfig.SuspendLayout();
            this.tabSlice.SuspendLayout();
            this.tabLoad.SuspendLayout();
            this.SuspendLayout();
            // 
            // tabs
            // 
            this.tabs.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.tabs.Controls.Add(this.tabConfig);
            this.tabs.Controls.Add(this.tabSlice);
            this.tabs.Controls.Add(this.tabLoad);
            this.tabs.Location = new System.Drawing.Point(0, 2);
            this.tabs.Name = "tabs";
            this.tabs.SelectedIndex = 0;
            this.tabs.Size = new System.Drawing.Size(476, 333);
            this.tabs.TabIndex = 0;
            // 
            // tabConfig
            // 
            this.tabConfig.Controls.Add(this.configFileCheck);
            this.tabConfig.Controls.Add(this.configFileButton);
            this.tabConfig.Controls.Add(this.configFileTextBox);
            this.tabConfig.Location = new System.Drawing.Point(4, 22);
            this.tabConfig.Name = "tabConfig";
            this.tabConfig.Padding = new System.Windows.Forms.Padding(3);
            this.tabConfig.Size = new System.Drawing.Size(468, 307);
            this.tabConfig.TabIndex = 0;
            this.tabConfig.Text = "config";
            this.tabConfig.UseVisualStyleBackColor = true;
            // 
            // configFileCheck
            // 
            this.configFileCheck.Anchor = System.Windows.Forms.AnchorStyles.Top;
            this.configFileCheck.Location = new System.Drawing.Point(174, 33);
            this.configFileCheck.Name = "configFileCheck";
            this.configFileCheck.Size = new System.Drawing.Size(108, 23);
            this.configFileCheck.TabIndex = 2;
            this.configFileCheck.Text = "check config file";
            this.configFileCheck.UseVisualStyleBackColor = true;
            this.configFileCheck.Click += new System.EventHandler(this.configFileCheck_Click);
            // 
            // configFileButton
            // 
            this.configFileButton.Location = new System.Drawing.Point(9, 4);
            this.configFileButton.Name = "configFileButton";
            this.configFileButton.Size = new System.Drawing.Size(108, 23);
            this.configFileButton.TabIndex = 1;
            this.configFileButton.Text = "select config file";
            this.configFileButton.UseVisualStyleBackColor = true;
            this.configFileButton.Click += new System.EventHandler(this.configFileButton_Click);
            // 
            // configFileTextBox
            // 
            this.configFileTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.configFileTextBox.Location = new System.Drawing.Point(123, 7);
            this.configFileTextBox.Name = "configFileTextBox";
            this.configFileTextBox.Size = new System.Drawing.Size(335, 20);
            this.configFileTextBox.TabIndex = 0;
            // 
            // tabSlice
            // 
            this.tabSlice.Controls.Add(this.helpButton);
            this.tabSlice.Controls.Add(this.sliceGetOnlyToolpaths);
            this.tabSlice.Controls.Add(this.paramTextBox);
            this.tabSlice.Controls.Add(this.paramLabel);
            this.tabSlice.Controls.Add(this.useMultislicing);
            this.tabSlice.Controls.Add(this.sliceAddslices);
            this.tabSlice.Controls.Add(this.sliceStepTextBox);
            this.tabSlice.Controls.Add(this.sliceStepLabel);
            this.tabSlice.Controls.Add(this.stlFileTextBox);
            this.tabSlice.Controls.Add(this.stlFileButton);
            this.tabSlice.Location = new System.Drawing.Point(4, 22);
            this.tabSlice.Name = "tabSlice";
            this.tabSlice.Padding = new System.Windows.Forms.Padding(3);
            this.tabSlice.Size = new System.Drawing.Size(468, 307);
            this.tabSlice.TabIndex = 1;
            this.tabSlice.Text = "slice";
            this.tabSlice.UseVisualStyleBackColor = true;
            // 
            // helpButton
            // 
            this.helpButton.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.helpButton.Location = new System.Drawing.Point(363, 87);
            this.helpButton.Name = "helpButton";
            this.helpButton.Size = new System.Drawing.Size(90, 23);
            this.helpButton.TabIndex = 9;
            this.helpButton.Text = "Parameter help";
            this.helpButton.UseVisualStyleBackColor = true;
            this.helpButton.Click += new System.EventHandler(this.helpButton_Click);
            // 
            // sliceGetOnlyToolpaths
            // 
            this.sliceGetOnlyToolpaths.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.sliceGetOnlyToolpaths.AutoSize = true;
            this.sliceGetOnlyToolpaths.Location = new System.Drawing.Point(266, 91);
            this.sliceGetOnlyToolpaths.Name = "sliceGetOnlyToolpaths";
            this.sliceGetOnlyToolpaths.Size = new System.Drawing.Size(91, 17);
            this.sliceGetOnlyToolpaths.TabIndex = 8;
            this.sliceGetOnlyToolpaths.Text = "only toolpaths";
            this.sliceGetOnlyToolpaths.UseVisualStyleBackColor = true;
            // 
            // paramTextBox
            // 
            this.paramTextBox.AcceptsReturn = true;
            this.paramTextBox.AcceptsTab = true;
            this.paramTextBox.AllowDrop = true;
            this.paramTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.paramTextBox.Location = new System.Drawing.Point(6, 118);
            this.paramTextBox.Multiline = true;
            this.paramTextBox.Name = "paramTextBox";
            this.paramTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Both;
            this.paramTextBox.Size = new System.Drawing.Size(452, 154);
            this.paramTextBox.TabIndex = 7;
            this.paramTextBox.WordWrap = false;
            // 
            // paramLabel
            // 
            this.paramLabel.AutoSize = true;
            this.paramLabel.Location = new System.Drawing.Point(11, 94);
            this.paramLabel.Name = "paramLabel";
            this.paramLabel.Size = new System.Drawing.Size(112, 13);
            this.paramLabel.TabIndex = 6;
            this.paramLabel.Text = "multislicing parameters";
            // 
            // useMultislicing
            // 
            this.useMultislicing.AutoSize = true;
            this.useMultislicing.Checked = true;
            this.useMultislicing.CheckState = System.Windows.Forms.CheckState.Checked;
            this.useMultislicing.Location = new System.Drawing.Point(11, 36);
            this.useMultislicing.Name = "useMultislicing";
            this.useMultislicing.Size = new System.Drawing.Size(76, 17);
            this.useMultislicing.TabIndex = 5;
            this.useMultislicing.Text = "multislicing";
            this.useMultislicing.UseVisualStyleBackColor = true;
            this.useMultislicing.CheckedChanged += new System.EventHandler(this.useMultislicing_CheckedChanged);
            // 
            // sliceAddslices
            // 
            this.sliceAddslices.Anchor = System.Windows.Forms.AnchorStyles.Bottom;
            this.sliceAddslices.Location = new System.Drawing.Point(194, 278);
            this.sliceAddslices.Name = "sliceAddslices";
            this.sliceAddslices.Size = new System.Drawing.Size(75, 23);
            this.sliceAddslices.TabIndex = 4;
            this.sliceAddslices.Text = "add slices";
            this.sliceAddslices.UseVisualStyleBackColor = true;
            this.sliceAddslices.Click += new System.EventHandler(this.sliceAddslices_Click);
            // 
            // sliceStepTextBox
            // 
            this.sliceStepTextBox.Enabled = false;
            this.sliceStepTextBox.Location = new System.Drawing.Point(67, 59);
            this.sliceStepTextBox.Name = "sliceStepTextBox";
            this.sliceStepTextBox.Size = new System.Drawing.Size(100, 20);
            this.sliceStepTextBox.TabIndex = 3;
            // 
            // sliceStepLabel
            // 
            this.sliceStepLabel.AutoSize = true;
            this.sliceStepLabel.Enabled = false;
            this.sliceStepLabel.Location = new System.Drawing.Point(8, 62);
            this.sliceStepLabel.Name = "sliceStepLabel";
            this.sliceStepLabel.Size = new System.Drawing.Size(53, 13);
            this.sliceStepLabel.TabIndex = 2;
            this.sliceStepLabel.Text = "Slice step";
            // 
            // stlFileTextBox
            // 
            this.stlFileTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.stlFileTextBox.Location = new System.Drawing.Point(107, 9);
            this.stlFileTextBox.Name = "stlFileTextBox";
            this.stlFileTextBox.Size = new System.Drawing.Size(358, 20);
            this.stlFileTextBox.TabIndex = 1;
            // 
            // stlFileButton
            // 
            this.stlFileButton.Location = new System.Drawing.Point(9, 7);
            this.stlFileButton.Name = "stlFileButton";
            this.stlFileButton.Size = new System.Drawing.Size(91, 23);
            this.stlFileButton.TabIndex = 0;
            this.stlFileButton.Text = "select STL file";
            this.stlFileButton.UseVisualStyleBackColor = true;
            this.stlFileButton.Click += new System.EventHandler(this.stlFileButton_Click);
            // 
            // tabLoad
            // 
            this.tabLoad.Controls.Add(this.ntoolTextBox);
            this.tabLoad.Controls.Add(this.labelOnlyNtool);
            this.tabLoad.Controls.Add(this.loadAddSlices);
            this.tabLoad.Controls.Add(this.loadGetOnlyToolpaths);
            this.tabLoad.Controls.Add(this.pathsFileTextBox);
            this.tabLoad.Controls.Add(this.pathsFileButton);
            this.tabLoad.Location = new System.Drawing.Point(4, 22);
            this.tabLoad.Name = "tabLoad";
            this.tabLoad.Padding = new System.Windows.Forms.Padding(3);
            this.tabLoad.Size = new System.Drawing.Size(468, 307);
            this.tabLoad.TabIndex = 3;
            this.tabLoad.Text = "load multifile";
            this.tabLoad.UseVisualStyleBackColor = true;
            // 
            // ntoolTextBox
            // 
            this.ntoolTextBox.Location = new System.Drawing.Point(186, 34);
            this.ntoolTextBox.Name = "ntoolTextBox";
            this.ntoolTextBox.Size = new System.Drawing.Size(30, 20);
            this.ntoolTextBox.TabIndex = 5;
            // 
            // labelOnlyNtool
            // 
            this.labelOnlyNtool.AutoSize = true;
            this.labelOnlyNtool.Location = new System.Drawing.Point(124, 37);
            this.labelOnlyNtool.Name = "labelOnlyNtool";
            this.labelOnlyNtool.Size = new System.Drawing.Size(55, 13);
            this.labelOnlyNtool.TabIndex = 4;
            this.labelOnlyNtool.Text = "only ntool:";
            // 
            // loadAddSlices
            // 
            this.loadAddSlices.Anchor = System.Windows.Forms.AnchorStyles.Top;
            this.loadAddSlices.Location = new System.Drawing.Point(186, 73);
            this.loadAddSlices.Name = "loadAddSlices";
            this.loadAddSlices.Size = new System.Drawing.Size(75, 23);
            this.loadAddSlices.TabIndex = 3;
            this.loadAddSlices.Text = "add slices";
            this.loadAddSlices.UseVisualStyleBackColor = true;
            this.loadAddSlices.Click += new System.EventHandler(this.loadAddSlices_Click);
            // 
            // loadGetOnlyToolpaths
            // 
            this.loadGetOnlyToolpaths.AutoSize = true;
            this.loadGetOnlyToolpaths.Location = new System.Drawing.Point(9, 37);
            this.loadGetOnlyToolpaths.Name = "loadGetOnlyToolpaths";
            this.loadGetOnlyToolpaths.Size = new System.Drawing.Size(91, 17);
            this.loadGetOnlyToolpaths.TabIndex = 2;
            this.loadGetOnlyToolpaths.Text = "only toolpaths";
            this.loadGetOnlyToolpaths.UseVisualStyleBackColor = true;
            // 
            // pathsFileTextBox
            // 
            this.pathsFileTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.pathsFileTextBox.Location = new System.Drawing.Point(114, 7);
            this.pathsFileTextBox.Name = "pathsFileTextBox";
            this.pathsFileTextBox.Size = new System.Drawing.Size(348, 20);
            this.pathsFileTextBox.TabIndex = 1;
            // 
            // pathsFileButton
            // 
            this.pathsFileButton.Location = new System.Drawing.Point(9, 7);
            this.pathsFileButton.Name = "pathsFileButton";
            this.pathsFileButton.Size = new System.Drawing.Size(99, 23);
            this.pathsFileButton.TabIndex = 0;
            this.pathsFileButton.Text = "select *.paths file";
            this.pathsFileButton.UseVisualStyleBackColor = true;
            this.pathsFileButton.Click += new System.EventHandler(this.pathsFileButton_Click);
            // 
            // ofdialog
            // 
            this.ofdialog.FileName = "FileName";
            // 
            // maindialog
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(474, 333);
            this.Controls.Add(this.tabs);
            this.Name = "maindialog";
            this.Text = "Multislicer interface";
            this.FormClosed += new System.Windows.Forms.FormClosedEventHandler(this.maindialog_FormClosed);
            this.Load += new System.EventHandler(this.maindialog_Load);
            this.tabs.ResumeLayout(false);
            this.tabConfig.ResumeLayout(false);
            this.tabConfig.PerformLayout();
            this.tabSlice.ResumeLayout(false);
            this.tabSlice.PerformLayout();
            this.tabLoad.ResumeLayout(false);
            this.tabLoad.PerformLayout();
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.TabControl tabs;
        private System.Windows.Forms.TabPage tabConfig;
        private System.Windows.Forms.TabPage tabSlice;
        private System.Windows.Forms.Button configFileCheck;
        private System.Windows.Forms.Button configFileButton;
        private System.Windows.Forms.TextBox configFileTextBox;
        private System.Windows.Forms.Label sliceStepLabel;
        private System.Windows.Forms.TextBox stlFileTextBox;
        private System.Windows.Forms.Button stlFileButton;
        private System.Windows.Forms.TabPage tabLoad;
        private System.Windows.Forms.CheckBox sliceGetOnlyToolpaths;
        private System.Windows.Forms.TextBox paramTextBox;
        private System.Windows.Forms.Label paramLabel;
        private System.Windows.Forms.CheckBox useMultislicing;
        private System.Windows.Forms.Button sliceAddslices;
        private System.Windows.Forms.TextBox sliceStepTextBox;
        private System.Windows.Forms.Button loadAddSlices;
        private System.Windows.Forms.CheckBox loadGetOnlyToolpaths;
        private System.Windows.Forms.TextBox pathsFileTextBox;
        private System.Windows.Forms.Button pathsFileButton;
        private System.Windows.Forms.OpenFileDialog ofdialog;
        private System.Windows.Forms.Button helpButton;
        private System.Windows.Forms.TextBox ntoolTextBox;
        private System.Windows.Forms.Label labelOnlyNtool;
    }
}