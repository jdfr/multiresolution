//this is a simple command line application that organizes the execution of the multislicer

//if macro STANDALONE_USEPYTHON is defined, SHOWCONTOUR support is baked in
#define STANDALONE_USEPYTHON

#include "app.hpp"
#include "config.hpp"
#include "spec.hpp"
#include "auxgeom.hpp"
#include <stdio.h>
#include <sstream>
#include <string>


//const int mlen = 16; //for true matrix
const int mlen = 12; //do not care about the last row of the transformation matrix
//the matrix is specified row-wise
typedef double TransformationMatrix[mlen];

//some examples of transformations for debugging:
//just translation: 1 0 0 5 0 1 0 -10 0 0 1 3
//rotation over Z axis: 0.469426929951 -0.882971346378 0.000000000000 0.000000000000 0.882971346378 0.469426929951 0.000000000000 0.000000000000 0.000000000000 0.000000000000 1.000000000000 0.000000000000 0.000000000000 0.000000000000 0.000000000000 1.000000000000
//full 3d rotation: 0.982641577721 0.099782958627 0.156393378973 -6.835826396942 -0.152828425169 0.913296103477 0.377536356449 -0.000000019690 -0.105161771178 -0.394884258509 0.912692427635 0.000000004553 0.000000000000 0.000000000000 0.000000000000 1.000000000000
//0.952031 0.180332 0.247219 -0.000000 -0.301287 0.693675 0.654249 -0.000000 -0.053507 -0.697349 0.714732 0.000000 0.000000 0.000000 0.000000 1.000000


bool transformationIs2DCOmpatible(TransformationMatrix matrix) {
    return ((matrix[2] == 0.0) && (matrix[6] == 0.0) && (matrix[8] == 0.0) && (matrix[9] == 0.0));
}

bool transformationSurelyIsAffine(TransformationMatrix matrix) {
    return (matrix[12] != 0.0) || (matrix[13] != 0.0) || (matrix[14] != 0.0) || (matrix[15] != 1.0);
}

void transformAndSave(FILE *f, TransformationMatrix matrix, bool is2DCompatible, SliceHeader &sliceheader, DPaths &paths) {
    FILES ff(1, f);
    if (is2DCompatible) {
        sliceheader.z = sliceheader.z*matrix[10] + matrix[11];
        for (auto path = paths.begin(); path != paths.end(); ++path) {
            for (auto point = path->begin(); point != path->end(); ++point) {
                double x = (matrix[0] * point->X) + (matrix[1] * point->Y) + matrix[3];
                double y = (matrix[4] * point->X) + (matrix[5] * point->Y) + matrix[7];
                point->X = x;
                point->Y = y;
            }
        }
        sliceheader.writeToFiles(ff);
        writeDoublePaths(f, paths, PathOpen);
    } else {
        Paths3D paths3;
        paths3.reserve(paths.size());
        for (auto path = paths.begin(); path != paths.end(); ++path) {
            paths3.push_back(Path3D());
            paths3.back().reserve(path->size());
            for (auto point = path->begin(); point != path->end(); ++point) {
                double xx = point->X;
                double yy = point->Y;
                double x = (matrix[0] * point->X) + (matrix[1] * point->Y) + (matrix[2]  * sliceheader.z) + matrix[3];
                double y = (matrix[4] * point->X) + (matrix[5] * point->Y) + (matrix[6]  * sliceheader.z) + matrix[7];
                double z = (matrix[8] * point->X) + (matrix[9] * point->Y) + (matrix[10] * sliceheader.z) + matrix[11];
                paths3.back().push_back(Point3D(x, y, z));
            }
        }

        sliceheader.saveFormat = SAVEMODE_DOUBLE_3D;
        sliceheader.totalSize = getPathsSerializedSize(paths3, PathOpen);
        sliceheader.setBuffer();
        sliceheader.writeToFiles(ff);

        write3DPaths(f, paths3, PathOpen);
    }
}

void transformAndSave(FILE *f, TransformationMatrix matrix, SliceHeader &sliceheader, Paths3D &paths) {
    FILES ff(1, f);
    for (auto path = paths.begin(); path != paths.end(); ++path) {
        for (auto point = path->begin(); point != path->end(); ++point) {
            double x = (matrix[0] * point->x) + (matrix[1] * point->y) + (matrix[2] *  point->z) + matrix[3];
            double y = (matrix[4] * point->x) + (matrix[5] * point->y) + (matrix[6] *  point->z) + matrix[7];
            double z = (matrix[8] * point->x) + (matrix[9] * point->y) + (matrix[10] * point->z) + matrix[11];
            point->x = x;
            point->y = y;
            point->z = z;
        }
    }
    sliceheader.writeToFiles(ff);
    write3DPaths(f, paths, PathOpen);
}

std::string transformPaths(const char * filename, const char *outputname, TransformationMatrix matrix) {
    FILE * f = fopen(filename, "rb");
    if (f == NULL) { return str("Could not open input file ", filename); }

    FileHeader fileheader;
    std::string err = fileheader.readFromFile(f);
    if (!err.empty()) { fclose(f); return str("Error reading file header for ", filename, ": ", err); }

    FILE * o = fopen(outputname, "wb");
    if (o == NULL) { return str("Could not open output file ", outputname); }

    FILES oo(1, o);

    fileheader.writeToFiles(oo, true);

    bool is2DCompatible = transformationIs2DCOmpatible(matrix);

    SliceHeader sliceheader;
    for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
        std::string err = sliceheader.readFromFile(f);
        if (!err.empty()) { return str("Error reading ", currentRecord, "-th slice header: ", err); }
        if (sliceheader.alldata.size() < 7) { return str("Error reading ", currentRecord, "-th slice header: header is too short!"); }
        if (sliceheader.saveFormat == SAVEMODE_INT64) {
            DPaths paths;
            {
                clp::Paths paths_input;
                readClipperPaths(f, paths_input);
                paths.resize(paths_input.size());
                for (int k = 0; k < paths.size(); ++k) {
                    paths[k].reserve(paths_input[k].size());
                    auto endp = paths_input[k].end();
                    for (auto point = paths_input[k].begin(); point != endp; ++point) {
                        paths[k].push_back(clp::DoublePoint(point->X * sliceheader.scaling, point->Y * sliceheader.scaling));
                    }
                }
            }
            sliceheader.saveFormat = SAVEMODE_DOUBLE;
            sliceheader.setBuffer();
            transformAndSave(o, matrix, is2DCompatible, sliceheader, paths);
        } else if (sliceheader.saveFormat == SAVEMODE_DOUBLE) {
            DPaths paths;
            readDoublePaths(f, paths);
            transformAndSave(o, matrix, is2DCompatible, sliceheader, paths);
        } else if (sliceheader.saveFormat == SAVEMODE_DOUBLE_3D) {
            Paths3D paths;
            read3DPaths(f, paths);
            transformAndSave(o, matrix, sliceheader, paths);
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
    ParamReader rd = getParamReader(argc, argv);

    if (!rd.err.empty()) {
        fprintf(stderr, "ParamReader error: %s\n", rd.err.c_str());
        return -1;
    }

    const char *pathsfilename_input, *pathsfilename_output;
    
    if (!rd.readParam(pathsfilename_input,    "PATHSFILENAME_INPUT"))           { printError(rd); return -1; }
    if (!rd.readParam(pathsfilename_output,   "PATHSFILENAME_OUTPUT"))          { printError(rd); return -1; }

    TransformationMatrix matrix;

    for (int i = 0; i < mlen; ++i) {
        if (!rd.readParam(matrix[i], i, "-th coefficient of the transformation matrix")) { printError(rd); return -1; }
    }

    if ((mlen==16) && transformationSurelyIsAffine(matrix)) {
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


