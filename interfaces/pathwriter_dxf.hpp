#ifndef PATHWRITER_DXF_HEADER
#define PATHWRITER_DXF_HEADER

#include "pathwriter_multifile.hpp"

enum DXFWMode { DXFAscii, DXFBinary };

//write to DXF file (either ascii or binary)
//TODO: currently, this only works on little-endian machines such as x86. If necessary, modify so it also work in different XXX-endians
template<DXFWMode mode> class DXFPathWriter : public PathWriterMultiFile<DXFPathWriter<mode>> {
public:
    DXFPathWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z);
    std::shared_ptr<DXFPathWriter<mode>> createSubWriter(std::string file, double epsilon, bool generic_type, bool _generic_ntool, bool _generic_z);
    bool startWriter();
    bool endWriter();
    bool writePathsSpecific(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    bool specificClose();
};

typedef DXFPathWriter<DXFAscii>  DXFAsciiPathWriter;
typedef DXFPathWriter<DXFBinary> DXFBinaryPathWriter;

#endif