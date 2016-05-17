#include "pathsfile.hpp"
#include "simpleparsing.hpp"

//some examples of transformations for debugging:
//just translation: 1 0 0 5 0 1 0 -10 0 0 1 3
//rotation over Z axis: 0.469426929951 -0.882971346378 0.000000000000 0.000000000000 0.882971346378 0.469426929951 0.000000000000 0.000000000000 0.000000000000 0.000000000000 1.000000000000 0.000000000000 0.000000000000 0.000000000000 0.000000000000 1.000000000000
//full 3d rotation: 0.982641577721 0.099782958627 0.156393378973 -6.835826396942 -0.152828425169 0.913296103477 0.377536356449 -0.000000019690 -0.105161771178 -0.394884258509 0.912692427635 0.000000004553 0.000000000000 0.000000000000 0.000000000000 1.000000000000
//0.952031 0.180332 0.247219 -0.000000 -0.301287 0.693675 0.654249 -0.000000 -0.053507 -0.697349 0.714732 0.000000 0.000000 0.000000 0.000000 1.000000


std::string transformAndSave(IOPaths &iop, TransformationMatrix matrix, bool is2DCompatible, bool identityInZ, bool identityInXY, SliceHeader &sliceheader, DPaths &paths) {
    if (is2DCompatible) {
        if (!identityInZ) {
            sliceheader.z = applyTransform2DCompatibleZ(sliceheader.z, matrix);
        }
        if (!identityInXY) {
            for (auto path = paths.begin(); path != paths.end(); ++path) {
                for (auto point = path->begin(); point != path->end(); ++point) {
                    applyTransform2DCompatibleXY(*point, matrix);
                }
            }
        }
        std::string err = sliceheader.writeToFile(iop.f);
        if (!err.empty()) { return err; }
        if (!iop.writeDoublePaths(paths, PathOpen)) {
            return std::string("Error while writing double clipperpaths!!!");
        }
    } else {
        Paths3D paths3;
        paths3.reserve(paths.size());
        for (auto path = paths.begin(); path != paths.end(); ++path) {
            paths3.push_back(Path3D());
            paths3.back().reserve(path->size());
            for (auto point = path->begin(); point != path->end(); ++point) {
                paths3.back().push_back(applyTransformFull3D(*point, sliceheader.z, matrix));
            }
        }

        sliceheader.saveFormat = PATHFORMAT_DOUBLE_3D;
        sliceheader.totalSize = getPathsSerializedSize(paths3, PathOpen) + sliceheader.headerSize;
        sliceheader.setBuffer();
        std::string err = sliceheader.writeToFile(iop.f);
        if (!err.empty()) { return err; }
        if (!write3DPaths(iop, paths3, PathOpen)) {
            return std::string("Error while writing 3d clipperpaths!!!");
        }
    }
    return std::string();
}

std::string transformAndSave(IOPaths &iop, TransformationMatrix matrix, SliceHeader &sliceheader, Paths3D &paths) {
    for (auto path = paths.begin(); path != paths.end(); ++path) {
        for (auto point = path->begin(); point != path->end(); ++point) {
            applyTransformFull3D(*point, matrix);
        }
    }
    std::string err = sliceheader.writeToFile(iop.f);
    if (!err.empty()) { return err; }
    if (!write3DPaths(iop, paths, PathOpen)) {
        return std::string("Error writing 3D clipperpaths!!!");
    }
    return std::string();
}

std::string transformPaths(const char * filename, const char *outputname, TransformationMatrix matrix) {
    FILE * f = fopen(filename, "rb");
    if (f == NULL) { return str("Could not open input file ", filename); }
    IOPaths iop_f(f);

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", filename, ": ", err); }

    FILE * o = fopen(outputname, "wb");
    if (o == NULL) { return str("Could not open output file ", outputname); }
    IOPaths iop_o(o);

    fileheader.writeToFile(o, true);

    bool is2DCompatible = transformationIs2DCOmpatible(matrix);
    bool identityInZ, identityInXY;
    if (is2DCompatible) {
        identityInZ  = transform2DIsIdentityInZ (matrix);
        identityInXY = transform2DIsIdentityInXY(matrix);
    }

    SliceHeader sliceheader;
    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string err = sliceheader.readFromFile(f);
        if (!err.empty()) { return str("Error reading ", currentRecord, "-th slice header: ", err); }
        if (sliceheader.alldata.size() < 7) { return str("Error reading ", currentRecord, "-th slice header: header is too short!"); }
        if (sliceheader.saveFormat == PATHFORMAT_INT64) {
            DPaths paths;
            {
                clp::Paths paths_input;
                if (!iop_f.readClipperPaths(paths_input)) {
                    return str("Error reading ", currentRecord, "-th integer clipperpaths: header is too short!");
                }
                paths.resize(paths_input.size());
                for (int k = 0; k < paths.size(); ++k) {
                    paths[k].reserve(paths_input[k].size());
                    auto endp = paths_input[k].end();
                    for (auto point = paths_input[k].begin(); point != endp; ++point) {
                        paths[k].push_back(clp::DoublePoint(point->X * sliceheader.scaling, point->Y * sliceheader.scaling));
                    }
                }
            }
            sliceheader.saveFormat = PATHFORMAT_DOUBLE;
            sliceheader.setBuffer();
            err = transformAndSave(iop_o, matrix, is2DCompatible, identityInZ, identityInXY, sliceheader, paths);
            if (!err.empty()) return err;
        } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE) {
            DPaths paths;
            if (!iop_f.readDoublePaths(paths)) {
                return str("Error reading ", currentRecord, "-th double clipperpaths: header is too short!");
            }
            err = transformAndSave(iop_o, matrix, is2DCompatible, identityInZ, identityInXY, sliceheader, paths);
            if (!err.empty()) return err;
        } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE_3D) {
            Paths3D paths;
            if (read3DPaths(iop_f, paths)) {
                return str("Error reading ", currentRecord, "-th 3d clipperpaths: header is too short!");
            }
            err = transformAndSave(iop_o, matrix, sliceheader, paths);
            if (!err.empty()) return err;
        } else {
            fclose(f);
            fclose(o);
            return str("error: ", currentRecord, "-th path in ", filename, " has an unknown save format type: ", sliceheader.saveFormat);
        }
    }

    fclose(f);
    fclose(o);

    return std::string();
}

const char *ERR =
"\nArguments: PATHSFILENAME_INPUT PATHSFILENAME_OUTPUT TRANSFORMATION_MATRIX\n\n"
"    -PATHSFILENAME_INPUT and PATHSFILENAME_OUTPUT are required (input/output paths file names).\n\n"
"    -TRANSFORMATION_MATRIX is a sequence of 16 values specifying a row-wise transformation matrix. If the transformation rotates just over the Z axis, the resulting paths are 2D. Otherwise, 3D paths are generated.\n\n";

void printError(ParamReader &rd) {
    rd.fmt << ERR;
    std::string err = rd.fmt.str();
    fprintf(stderr, err.c_str());
}

int main(int argc, const char** argv) {
    ParamReader rd = ParamReader::getParamReaderWithOptionalResponseFile(argc, argv);

    if (!rd.err.empty()) {
        fprintf(stderr, "ParamReader error: %s\n", rd.err.c_str());
        return -1;
    }

    const char *pathsfilename_input, *pathsfilename_output;
    
    if (!rd.readParam(pathsfilename_input,    "PATHSFILENAME_INPUT"))           { printError(rd); return -1; }
    if (!rd.readParam(pathsfilename_output,   "PATHSFILENAME_OUTPUT"))          { printError(rd); return -1; }

    TransformationMatrix matrix;

    for (int i = 0; i < TransformationMatrixLength; ++i) {
        if (!rd.readParam(matrix[i], i, "-th coefficient of the transformation matrix")) { printError(rd); return -1; }
    }

    if (transformationSurelyIsAffine(matrix)) {
        fprintf(stderr, "The specified transformation matrix is not rigid!!!!");
        return -1;
    }

    std::string err = transformPaths(pathsfilename_input, pathsfilename_output, matrix);

    if (!err.empty()) {
        fprintf(stderr, "Error while trying to get a set of paths according to the specification: %s", err.c_str());
        return -1;
    }

    return 0;
}


