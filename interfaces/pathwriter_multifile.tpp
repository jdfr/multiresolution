#include "pathwriter_multifile.hpp"

//this contains the implementation of the PathWriterMultiFile template, and is intended to be included where necessary

template<typename T> void PathWriterMultiFile<T>::init(std::string file, const char * _extension, double _epsilon, bool _generic_for_type, bool _generic_for_ntool, bool _generic_for_z) {
    epsilon = _epsilon;
    generic_for_ntool = _generic_for_ntool;
    generic_for_z = _generic_for_z;
    generic_for_type = _generic_for_type;
    generic_all = this->generic_for_ntool && this->generic_for_z && this->generic_for_type;
    delegateWork = !this->generic_all;
    filename = std::move(file);
    extension = _extension;
    isopen = false;
}

template<typename T> bool PathWriterMultiFile<T>::matchZNtool(int _type, int _ntool, double _z) {
    return generic_all || ((generic_for_ntool || (ntool == _ntool)) &&
        (generic_for_z || (std::fabs(z - _z) < epsilon)) &&
        (generic_for_type || (_type == type)));
}

template<typename T> int PathWriterMultiFile<T>::findOrCreateSubwriter(int _type, double _radius, int _ntool, double _z) {
    if (subwriters.size()>0) {
        if (subwriters[currentSubwriter]->matchZNtool(_type, _ntool, _z)) {
            return currentSubwriter;
        }
        for (auto w = subwriters.begin(); w != subwriters.end(); ++w) {
            if ((*w)->matchZNtool(_type, _ntool, _z)) {
                currentSubwriter = (int)(w - subwriters.begin());
                return currentSubwriter;
            }
        }
    }

    std::string N, Z;
    const char * Type = "";
    if (!generic_for_type)  {
        switch (_type) {
        case PATHTYPE_RAW_CONTOUR:        Type = ".raw";       break;
        case PATHTYPE_PROCESSED_CONTOUR:  Type = ".contour";   break;
        case PATHTYPE_TOOLPATH_PERIMETER: Type = ".perimeter"; break;
        case PATHTYPE_TOOLPATH_SURFACE:   Type = ".surface";   break;
        case PATHTYPE_TOOLPATH_INFILLING: Type = ".infilling"; break;
        default:                          Type = ".unknown";
        }
    }
    if (!generic_for_ntool) N = str(".N", _ntool);
    if (!generic_for_z)     Z = str(".Z", _z);
    std::string newfilename = str(filename, Type, N, Z, ".dxf");
    subwriters.push_back(static_cast<T*>(this)->createSubWriter(newfilename, epsilon, generic_for_type, generic_for_ntool, generic_for_z));
    subwriters.back()->type = _type;
    subwriters.back()->radius = _radius;
    subwriters.back()->ntool = _ntool;
    subwriters.back()->z = _z;
    subwriters.back()->delegateWork = false;
    return currentSubwriter = ((int)subwriters.size() - 1);
}

template<typename T> bool PathWriterMultiFile<T>::start() {
    if (!this->isopen) {
        addExtension(filename, extension);
        if (!static_cast<T*>(this)->startWriter()) return false;
    }
    return true;
}

template<typename T> bool PathWriterMultiFile<T>::writePaths(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    if (delegateWork) {
        int idx = findOrCreateSubwriter(type, radius, ntool, z);
        bool ret = subwriters[idx]->writePaths(paths, type, radius, ntool, z, scaling, isClosed);
        if (!ret) err = subwriters[idx]->err;
        return ret;
    }
    if (!isopen) {
        addExtension(filename, extension);
        if (!static_cast<T*>(this)->startWriter()) return false;
    }
    return static_cast<T*>(this)->writePathsSpecific(paths, type, radius, ntool, z, scaling, isClosed);
}

template<typename T> bool PathWriterMultiFile<T>::writeToAll(clp::Paths &paths, int type, double radius, int ntool, double z, double scaling, bool isClosed) {
    bool ret = true;
    if (delegateWork) {
        bool ret = true;
        for (auto &w : subwriters) {
            bool newret = w->writePaths(paths, type, radius, ntool, z, scaling, isClosed);
            if (!newret) err = w->err;
            ret = ret && newret;
        }
    }
    if (isopen) {
        bool newret = static_cast<T*>(this)->writePathsSpecific(paths, type, radius, ntool, z, scaling, isClosed);
        ret = ret && newret;
    }
    return ret;
}


template<typename T> bool PathWriterMultiFile<T>::close() {
    bool ok = true;
    if (!subwriters.empty()) {
        for (auto &w : subwriters) {
            if (!w->close()) {
                err = w->err;
                ok = false;
            }
        }
        subwriters.clear();
    }
    if (isopen) {
        bool newok;
        newok = static_cast<T*>(this)->endWriter();
        ok = ok && newok;
        newok = static_cast<T*>(this)->specificClose();
        ok = ok && newok;
        isopen = false;
    }
    return ok;
}
