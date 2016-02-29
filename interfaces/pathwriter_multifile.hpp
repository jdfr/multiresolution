#ifndef PATHWRITER_MULTIFILE_HEADER
#define PATHWRITER_MULTIFILE_HEADER

#include "pathwriter.hpp"

//this class implements the common functionality for all PathWriters which can write to several files at once.
//We use the CRTP idiom to get compile-time dispatch where we need it
template<typename T> class PathWriterMultiFile : public PathWriter {
public:
    virtual bool start();
    virtual bool writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    virtual bool close();
    virtual ~PathWriterMultiFile() { close(); }
    bool writeToAll(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed);
    bool generic_for_ntool, generic_for_z, generic_for_type, generic_all, delegateWork;
    std::vector<std::shared_ptr<T>> subwriters;
protected:
    void init(std::string file, const char * extension, double epsilon, bool generic_for_type, bool generic_for_ntool, bool generic_for_z);
    bool matchZNtool(int type, int ntool, double z);
    int findOrCreateSubwriter(int type, double radius, int ntool, double z);
    //derived classes must implement a constructor without parameters, specificClose(), startWriter(), writePathsSpecific(), and endWriter()
    double z, epsilon;
    double radius;
    int type;
    int ntool;
    bool isopen;
    int currentSubwriter;
    //this has to be a vector of pointers because if the vector gets bigger, it will destroy and recreate the objects, interfering with the life cycle we have designed
    FILE * f;
    const char * extension;
};

#endif