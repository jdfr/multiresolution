#ifndef SHOWCONTOURS_HEADER
#define SHOWCONTOURS_HEADER

//this is strictly for debugging!

#include "common.hpp"
#include "config.hpp"

typedef struct ShowContoursInfo {
    bool usePipe;
    std::string windowname;
    std::string filesep;
    std::string pythonExecutablePath;
    std::string showContoursDir;
    std::string showContoursScriptPath;
    template<typename STR> ShowContoursInfo(Configuration &config, STR _windowname) {
        windowname = _windowname;
        usePipe = true;
        //usePipe = false;
        filesep = config.getValue("FILESEP");
        showContoursDir = config.getValue("SHOWCONTOURS_SCRIPTPATH");
        //showContoursScriptPath = showContoursDir + filesep + config.getValue("SHOWCONTOURS_SCRIPTNAME");
        showContoursScriptPath = config.getValue("SHOWCONTOURS_SCRIPTNAME");
        pythonExecutablePath = config.getValue("PYPATH");
    }
} ShowContoursInfo;

//hack to send contours to python in order to plot them with matplotlib. Several calling schemes are provided, though in practice only one is used for now...
void showContours(std::vector<clp::Paths> &contours, ShowContoursInfo &info);
void showContours(std::vector<clp::Paths*> &contours, ShowContoursInfo &info);

//this is a hack to write the list of contours to show as a list of pointers of variable length, automagically generating required calling boilerplate
template<typename STR, typename... Args> void SHOWCONTOURS(Configuration &config, STR windowname, Args... args) {
    std::vector<clp::Paths*> toshowv = { args... };
    ShowContoursInfo info(config, windowname);
    showContours(toshowv, info);
}

#endif