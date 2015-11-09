#ifndef SLICEVIEWER_HEADER
#define SLICEVIEWER_HEADER

#include "subprocess.hpp"

class SlicesViewer : public virtual SubProcessManager {
public:
    template<typename STR> SlicesViewer(Configuration &config, STR _windowname, bool _use2d) : SubProcessManager(true, false) {
        windowname = _windowname;
        use2D = _use2d;
        filesep = config.getValue("FILESEP");
        workdir = config.getValue("SLICEVIEWER_SCRIPTPATH");
        scriptPath = workdir + filesep + config.getValue("SLICEVIEWER_SCRIPTNAME");
        execpath = config.getValue("PYPATH");
        const char *key = use2D ? "SLICEVIEWER_2D_FORMATTING" : "SLICEVIEWER_3D_FORMATTING";
        use_custom_formatting = config.hasKey(key);
        if (use_custom_formatting) {
            custom_formatting = config.getValue(key);
        }
    }
    template<typename STR> SlicesViewer(Configuration &_config, STR _windowname, bool _use2d, STR _inputfile) : SlicesViewer(_config, _windowname, _use2d) {
        pipeInput = false;
        inputfile = _inputfile;
    }
    virtual std::string start();
protected:
    std::string windowname;
    std::string inputfile;
    std::string custom_formatting;
    bool use2D;
    bool use_custom_formatting;
    std::string filesep;
    std::string scriptPath;
};

#endif
