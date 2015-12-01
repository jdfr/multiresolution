#include "pathsfile.hpp"
#include "simpleparsing.hpp"
#include <string>
#include <fstream>
#include <limits>
#include <math.h>

#include <boost/tokenizer.hpp>
using boost::tokenizer;
using boost::escaped_list_separator;
typedef escaped_list_separator<char> sep;

//algorithm to iterate over a xyz file
template<typename Function> std::string withXYZDo(const char *inputfilename, int &nline, Function function) {
    sep pointsep("", ",; \t", "");

    std::string line;
    std::ifstream f(inputfilename);
    nline = 0;
    std::vector<double> xyz;
    xyz.reserve(3);
    while (getline(f, line)) {
        xyz.clear();

        tokenizer<sep> tok(line, pointsep);

        for (tokenizer<sep>::iterator it = tok.begin(); it != tok.end(); ++it) {
            if (!it->empty()) {
                xyz.push_back(strtod(it->c_str(), NULL));
            }
            if (xyz.size() == 3) break; //allow for point cloud files with more than 3 fields
        }

        if (xyz.empty()) continue;

        ++nline;
        if (xyz.size() != 3) {
            return str("In file ", inputfilename, ", line ", nline, " cannot be converted to three input values: ", line, "\n");
        }

        function(xyz);
    }

    if (nline == 0) {
        return str("Could not open file ", inputfilename, ", or it has no points");
    }

    return std::string();
}

typedef struct Limits {
    double minx = std::numeric_limits<double>::infinity();
    double maxx = -std::numeric_limits<double>::infinity();
    double miny = std::numeric_limits<double>::infinity();
    double maxy = -std::numeric_limits<double>::infinity();
    double minz = std::numeric_limits<double>::infinity();
    double maxz = -std::numeric_limits<double>::infinity();
} Limits;

std::string bbxyz(const char *inputfilename) {
    Limits lims;

    auto getLimits = [&lims](std::vector<double> &xyz) {
        lims.minx = fmin(lims.minx, xyz[0]);
        lims.maxx = fmax(lims.maxx, xyz[0]);
        lims.miny = fmin(lims.miny, xyz[1]);
        lims.maxy = fmax(lims.maxy, xyz[1]);
        lims.minz = fmin(lims.minz, xyz[2]);
        lims.maxz = fmax(lims.maxz, xyz[2]);
    };

    int nline;

    std::string res = withXYZDo(inputfilename, nline, getLimits);

    if (res.empty()) {
        fprintf(stdout, "number of points: %d\n", nline);
        fprintf(stdout, "bounding box:\n");
        fprintf(stdout, "X: %25.20g %25.20g\n", lims.minx, lims.maxx);
        fprintf(stdout, "Y: %25.20g %25.20g\n", lims.miny, lims.maxy);
        fprintf(stdout, "Z: %25.20g %25.20g\n", lims.minz, lims.maxz);
    }

    return res;

}

//const int mlen = 16; //for true matrix
const int mlen = 12; //do not care about the last row of the transformation matrix
//the matrix is specified row-wise
typedef double TransformationMatrix[mlen];

std::string transformAndSave(const char *input, const char *output, TransformationMatrix matrix) {
    FILE * o = fopen(output, "w");
    if (o == NULL) { return str("Could not open output file ", output); }

    auto doTransform = [matrix, o](std::vector<double> &xyz) {
        double x = (matrix[0] * xyz[0]) + (matrix[1] * xyz[1]) + (matrix[2] * xyz[2]) + matrix[3];
        double y = (matrix[4] * xyz[0]) + (matrix[5] * xyz[1]) + (matrix[6] * xyz[2]) + matrix[7];
        double z = (matrix[8] * xyz[0]) + (matrix[9] * xyz[1]) + (matrix[10] * xyz[2]) + matrix[11];

        fprintf(o, "%25.20g %25.20g %25.20g\n", x, y, z);
    };

    int nline;

    std::string res = withXYZDo(input, nline, doTransform);

    fclose(o);
    return res;
}


const char *ERR =
"\nArguments: INPUTFILENAME (bb | transform OUTPUTFILENAME TRANSFORMATION_MATRIX)\n\n"
"    -INPUTFILENAME is required (point cloud in xyz format).\n\n"
"    -if second argument is 'bb', compute the bounding box of the point cloud.\n\n"
"    -if second argument is 'transform':\n\n"
"    -OUTPUTFILENAME is the name of the transformed point cloud in xyz format.\n\n"
"    -TRANSFORMATION_MATRIX is a sequence of 16 values specifying a row-wise transformation matrix.\n\n"
"This tool handles point clouds in xyz format.\n\n";

void printError(ParamReader &rd) {
    rd.fmt << ERR;
    std::string err = rd.fmt.str();
    fprintf(stderr, err.c_str());
}


int main(int argc, const char **argv) {
    ParamReader rd = ParamReader::getParamReaderWithOptionalResponseFile(argc, argv);

    if (!rd.err.empty()) {
        fprintf(stderr, "ParamReader error: %s\n", rd.err.c_str());
        return -1;
    }

    const char *filename_input, *mode;

    if (!rd.readParam(filename_input, "INPUTFILENAME"))                                      { printError(rd); return -1; }
    if (!rd.readParam(mode, "mode (bb|transform)"))                                          { printError(rd); return -1; }

    std::string err;

    if        (strcmp(mode, "bb")==0) {
        err = bbxyz(filename_input);
    } else if (strcmp(mode, "transform") == 0) {
        const char *filename_output;
        TransformationMatrix matrix;

        if (!rd.readParam(filename_output, "OUTPUTFILENAME"))                                { printError(rd); return -1; }
        for (int i = 0; i < mlen; ++i) {
            if (!rd.readParam(matrix[i], i, "-th coefficient of the transformation matrix")) { printError(rd); return -1; }
        }

        err = transformAndSave(filename_input, filename_output, matrix);

    } else {
        fprintf(stderr, "mode parameter must be either 'bb' or 'transform', but was %s\n", mode);
        return -1;
    }

    if (err.empty()) {
        return 0;
    } else {
        fprintf(stderr, err.c_str());
        return -1;
    }

}
