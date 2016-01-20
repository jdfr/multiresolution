#include "multislicer.hpp"
#include "orientPaths.hpp"
#include "medialaxis.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <math.h>
#include <algorithm>
#include <sstream>

#include "showcontours.hpp"

/////////////////////////////////////////////////
/*MACHINERY FOR THE QUICK HACK TO ADAPT THE CODE
TO SUBSTRACTIVE WHEN THE OUTER LIMITS ARE NOT
EXPLICIT (THIS IS INEFFICIENT, COMPARED TO A
FULLY-FLEDGED SUBSTRACTIVE IMPLEMENTATION, BUT EASIER)*/
/////////////////////////////////////////////////

typedef struct InOuter {
    clp::cInt limitX, limitY;
    
    InOuter(clp::cInt _limitX, clp::cInt _limitY) : limitX(_limitX), limitY(_limitY) {}
    
    bool operator()(clp::Path &path) const {
        clp::cInt minx, maxx, miny, maxy;
        if (path.size()==0) return false;
        minx = maxx = path[0].X;
        miny = maxy = path[0].X;
        for (clp::Path::iterator point = path.begin()+1; point!=path.end(); ++point) {
            minx = std::min(minx, point->X);
            maxx = std::max(maxx, point->X);
            miny = std::min(miny, point->Y);
            maxy = std::max(maxy, point->Y);
        }
        return ((abs(minx)>=this->limitX) ||
                (abs(maxx)>=this->limitX) ||
                (abs(miny)>=this->limitY) ||
                (abs(maxy)>=this->limitY));
    }
} InOuter;

void addOuter(clp::Paths &paths, clp::cInt limitX, clp::cInt limitY) {
    paths.push_back(clp::Path());
    clp::Paths::reverse_iterator path = paths.rbegin();
    path->push_back(clp::IntPoint( limitX,  limitY));
    path->push_back(clp::IntPoint(-limitX,  limitY));
    path->push_back(clp::IntPoint(-limitX, -limitY));
    path->push_back(clp::IntPoint( limitX, -limitY));
}

void removeOuter(clp::Paths &paths, clp::cInt limitX, clp::cInt limitY) {
    paths.erase( std::remove_if(paths.begin(), paths.end(), InOuter(limitX, limitY)), paths.end() );
}


//auxiliar variables are passed around in order to avoid recurring std::vector growing costs

void Multislicer::removeHighResDetails(size_t k, clp::Paths &contours, clp::Paths &lowres, clp::Paths &opened, clp::Paths &aux1) {
    //lowres is used at the end as output, but before that is used as temporary variable

    //remove high-resolution positive details from paths
    //opened <- opening(paths, radius)
    offset.ArcTolerance = (double)spec.pp[k].arctolG;
    offsetDo2(offset, opened, (double)-spec.pp[k].radius, (double)spec.pp[k].radius, contours, lowres, clp::jtRound, clp::etClosedPolygon);

    //add high-resolution negative space
    //aux1 <- closing(opened, substep)
    //offset.ArcTolerance = (double)spec.pp[k].arctolG;
    offsetDo2(offset, aux1, (double)spec.pp[k].substep, (double)-spec.pp[k].substep * 1.0, opened, lowres, clp::jtRound, clp::etClosedPolygon);

    //separate negative space
    //lowres <- aux1 - opened
    clipperDo(clipper, lowres, clp::ctDifference, aux1, opened, clp::pftEvenOdd, clp::pftEvenOdd);

    /*we may expect lowres to be completely like paths in low resolution areas.
    However, it seems that lowres has long, narrow strips around paths in some
    areas, hinting that it is somehow slightly more extended than it should
    be. This would lead to unnecessary removal of material to accomodate
    non-existent high-resolution negative space. To avoid this problem, we
    apply here an opening with a very small radius.
    TODO: We are taking smallRadius, but we should determine an optimal radius*/
    offset.ArcTolerance = (double)spec.pp[k + 1].arctolR; //arctolsmall//arctolgridstep
    offsetDo2(offset, lowres, (double)-spec.pp[k + 1].radius, (double)spec.pp[k + 1].radius, lowres, aux1, clp::jtRound, clp::etClosedPolygon);

    //dilate separated negative space
    //aux1 <- dilate(lowres, dilatestep)
    offset.ArcTolerance = (double)spec.pp[k].arctolG;
    offsetDo(offset, aux1, (double)spec.pp[k].dilatestep, lowres, clp::jtRound, clp::etClosedPolygon);

    //remove high-resolution negative details from paths
    //lowres <- opened - aux1
    clipperDo(clipper, lowres, clp::ctDifference, opened, aux1, clp::pftEvenOdd, clp::pftEvenOdd);

    //RESULT IS RETURNED IN lowres
}

//in a first implementation, we suppose that no clearance is required
void Multislicer::overwriteHighResDetails(size_t k, clp::Paths &contours, clp::Paths &lowres, clp::Paths &aux1) {

    //remove high-res negative details.
    offset.ArcTolerance = (double)spec.pp[k].arctolG;
    double negativeHighResFactor = (double)spec.pp[k].substep * 1.1;
    offsetDo2(offset, lowres, negativeHighResFactor, -negativeHighResFactor, contours, aux1, clp::jtRound, clp::etClosedPolygon);

}

void Multislicer::doDiscardCommonToolPaths(size_t k, clp::Paths &toolpaths, clp::Paths &contours_alreadyfilled, clp::Paths &aux1) {
    //admitedly, this way to remove common arcs betwen adjacent paths is expensive and overkill
    //maybe there is way to directly extract the common arcs from the guts of CliperLib::Clipper.
    //but answering that question would require a very intimate knowledge of the internals of that library. too little time to do it right now.

    //we will not overwrite aux2 because its contents will be needed later in the loop!

    //now, offset all previous contours, and accumulate them
    clp::Paths previousContours;
    clipper.AddPaths(toolpaths, clp::ptSubject, false);
    double globalRadius = (double)spec.pp[k].radius + (double)spec.pp[k].radiusRemoveCommon;
    offset.ArcTolerance = (double)spec.pp[k].arctolG;
    offsetDo(offset, aux1, globalRadius, contours_alreadyfilled, clp::jtRound, clp::etClosedPolygon);
    clipper.AddPaths(aux1, clp::ptClip, true);
    //execute the difference. NOTE: for intersected paths, the result can be either an open path or a pair of open paths for each path sharing a common arc with lower resolution contours.
    //the latter (two paths) happens if the endpoint is not in the common arc. unintersected paths should not be affected by the operation
    clp::PolyTree polytree; //polytree required, otherwise ClipperLib call will fail!
    clipper.Execute(clp::ctDifference, polytree, clp::pftEvenOdd, clp::pftEvenOdd);
    clipper.Clear();
    clp::PolyTreeToPaths(polytree, toolpaths); //copies both closed and open paths
}

bool Multislicer::generateToolPath(size_t k, bool nextProcessSameKind, clp::Paths &contour, clp::Paths &toolpaths, clp::Paths &temp_toolpath, clp::Paths &aux1) {
    /*erode to get the toolpath
    use gridstep's arcTolerance because we have details at that scale,
    after removing negative details).*/
    //toolpath <- erode(contour, radius)
    offset.ArcTolerance = (double)spec.pp[k].arctolG;
    offsetDo(offset, temp_toolpath, (double)-spec.pp[k].radius, contour, clp::jtRound, clp::etClosedPolygon);

    //IT IS RECOMMENDED TO DO SNAPPING FOR ALL PATHS, BECAUSE IT SEEMS TO REMOVE SLIGHT IMPERFECTIONS WHICH CAN CAUSE PROBLEMS LATER

    //We ALSO add a SNAPTOGRID operation to get a finely tuned
    //big-radius toolpath.
    if (spec.pp[k].applysnap) {
        //toolpath <- opening(toolpath, safestep)
        if (nextProcessSameKind) {
            offsetDo2(offset, temp_toolpath, -spec.pp[k].safestep, spec.pp[k].safestep, temp_toolpath, aux1, clp::jtRound, clp::etClosedPolygon);
        } else {
            spec.pp[k].snapspec.mode = SnapDilate;
        }
        //aux1 <- snapToGrid(toolpath, gridstep, doErosion)
        bool ok = snapClipperPathsToGrid(aux1, temp_toolpath, spec.pp[k].snapspec, *err);
        if (!ok) return false;
        std::swap(aux1, temp_toolpath);
    } else {
        if (spec.pp[k].addInternalClearance) {
            offsetDo2(offset, temp_toolpath, (double)-spec.pp[k].radius, (double)spec.pp[k].radius, temp_toolpath, aux1, clp::jtRound, clp::etClosedPolygon);
        } else {
            if (spec.pp[k].burrLength>0) {
                //toolpath <- opening(toolpath, safestep)
                double oldmiter = offset.MiterLimit;
                offset.MiterLimit = 10;
                offsetDo2(offset, temp_toolpath, (double)-spec.pp[k].burrLength, (double)spec.pp[k].burrLength, temp_toolpath, aux1, clp::jtSquare, clp::etClosedPolygon);
                offset.MiterLimit = oldmiter; //should not be necessary, but just in case...
            }
        }
    }

    //add to the end the initial point of each contour in the toolpath (this is unconditionally necessary to make the interface with the company's c# app to work seamlessly for open and closed paths, but it is also necessary to correctly operate with the toolpath as a set of open paths in clipperlib)
    applyToPaths<copyOpenToClosedPath, ReserveCapacity>(temp_toolpath, toolpaths);
    return true;
}

template<typename T, typename INFLATEDACCUM> void Multislicer::operateInflatedLinesAndContoursInClipper(clp::ClipType mode, T &res, clp::Paths &lines, double radius, clp::Paths *aux, INFLATEDACCUM* inflated_acumulator) {
    for (clp::Paths::iterator line = lines.begin(); line != lines.end(); ++line) {
        aux->clear();
        offsetDo(offset, *aux, radius, *line, clp::jtRound, clp::etOpenRound);
        /*offset.AddPath(*line, clp::jtRound, clp::etOpenRound);
        aux->clear();
        offset.Execute(*aux, radius);
        offset.Clear();*/
        clipper.AddPaths(*aux, clp::ptClip, true);
        if (inflated_acumulator != NULL) {
            MOVETO(*aux, *inflated_acumulator);
        }
    }
    clipper.Execute(mode, res, clp::pftEvenOdd, clp::pftNonZero);
    clipper.Clear();

}

template<typename T, typename INFLATEDACCUM> void Multislicer::operateInflatedLinesAndContours(clp::ClipType mode, T &res, clp::Paths &contours, clp::Paths &lines, double radius, clp::Paths *aux, INFLATEDACCUM* inflated_acumulator) {
    clipper.AddPaths(contours, clp::ptSubject, true);
    operateInflatedLinesAndContoursInClipper(mode, res, lines, radius, aux, inflated_acumulator);
}

/////////////////////////////////////////////////
//INFILL HELPERS
/////////////////////////////////////////////////

void Multislicer::processInfillingsRectilinear(size_t k, clp::Paths &infillingAreas, BBox bb, bool horizontal) {
    //SEGUIR POR AQUI
    double epsilon_start = 0;// infillingRadius * 0.01; //do not start the lines exactly on the boundary, but a little bit past it
    double epsilon_erode = infillingRadius * 0.01; //do not erode a full radius, but keep a small offset
    double erode_value = (infillingUseClearance) ? epsilon_erode - infillingRadius : 0.0;
    clp::cInt minLineSize = (clp::cInt)(infillingRadius*1.0); //do not allow ridiculously small lines
    clp::cInt start = (horizontal ? bb.miny : bb.minx) + (clp::cInt)epsilon_start;
    clp::cInt delta = (clp::cInt)(2*infillingRadius*0.999); //space the lines a little bit closer than the line width
    clp::cInt numlines = ((horizontal ? bb.maxy : bb.maxx) - start) / delta + 1; //add one line to be sure
    clp::cInt accum = start;
    clp::Paths lines(numlines, clp::Path(2));
    for (auto &line : lines) {
        if (horizontal) {
            line.front().X = bb.minx;
            line.back().X  = bb.maxx;
            line.back().Y  = line.front().Y = accum;
        } else { //vertical
            line.front().Y = bb.miny;
            line.back().Y  = bb.maxy;
            line.back().X  = line.front().X = accum;
        }
        accum             += delta;
    }
    if (erode_value != 0.0) {
        clp::Paths aux;
        offsetDo(offset, aux, erode_value, infillingAreas, clp::jtRound, clp::etClosedPolygon);
        clipper.AddPaths(aux, clp::ptClip, true);
    } else {
        clipper.AddPaths(infillingAreas, clp::ptClip, true);
    }
    clipper.AddPaths(lines, clp::ptSubject, false);
    {
        clp::PolyTree pt;
        clipper.Execute(clp::ctIntersection, pt, clp::pftEvenOdd, clp::pftEvenOdd);
        clp::PolyTreeToPaths(pt, lines);
    }
    clipper.Clear();
    if (spec.pp[k].applysnap) {
        verySimpleSnapPathsToGrid(lines, spec.pp[k].snapspec);
    }
    //IMPORTANT: these lambdas test if the line distance is below the threshold. If diagonal lines are possible, a new lambda must be added to handle them!
    if (horizontal) {
        auto test = [minLineSize](clp::Path &line) {return abs(line.front().X - line.back().X) < minLineSize; };
        lines.erase(std::remove_if(lines.begin(), lines.end(), test), lines.end());
    } else { //vertical
        auto test = [minLineSize](clp::Path &line) {return abs(line.front().Y - line.back().Y) < minLineSize; };
        lines.erase(std::remove_if(lines.begin(), lines.end(), test), lines.end());
    }
    COPYTO(lines, *accumInfillings);
    if (infillingRecursive) {
        offsetDo(offset, lines, infillingRadius, lines, clp::jtRound, clp::etOpenRound);
        MOVETO(lines, *infillingsIndependentContours);
    }
}


/* quite hard to get right: plenty of cases were it seems like the medial axis algorithm should be
   filling voids, but it doesn't. It may be because of two reasons: shapes are not elongated enough
   (likely in some cases, but conspicuosly narrow voids are also generated), and/or they are
   too small (remember the min_width and  max_width constraints, it is difficult to fiddle with them
   in a productive way).
*/
bool Multislicer::processInfillingsConcentricRecursive(HoledPolygon &hp) {
    clp::Paths current, next, smoothed;
    hp.moveToPaths(current);
    if (infillingUseClearance) {
        //make the paths as non-intersecting as possible
        offsetDo(offset, smoothed, -2 * infillingRadius, current, clp::jtRound, clp::etClosedPolygon);
        offsetDo(offset, next,          infillingRadius, smoothed, clp::jtRound, clp::etClosedPolygon);
    } else {
        //freely intersecting paths, as long as they are separated by the adequate distance
        offsetDo(offset, next, -infillingRadius, current, clp::jtRound, clp::etClosedPolygon);
    }
    if (false){//applySnapConcentricInfilling) {
        bool ok = snapClipperPathsToGrid(smoothed, next, concentricInfillingSnapSpec, *err);
        if (!ok) return false;
        MOVETO(smoothed, next);
    }
    COPYTO(next, *accumInfillings);
    if (infillingRecursive) {
        offsetDo(offset, smoothed, infillingRadius, next, clp::jtRound, clp::etOpenRound);
        MOVETO(smoothed, *infillingsIndependentContours);
    }
    offsetDo(offset, current, -infillingRadius, next, clp::jtRound, clp::etClosedPolygon);
    HoledPolygons subhps;
    AddPathsToHPs(current, subhps);
    smoothed = current = next = clp::Paths();
    for (auto subhp = subhps.begin(); subhp != subhps.end(); ++subhp) {
        if (!processInfillingsConcentricRecursive(*subhp)) return false;
    }
    return true;
}

bool Multislicer::processInfillings(size_t k, clp::Paths &infillingAreas, clp::Paths &contoursToBeInfilled) {
    infillingRadius = (double)spec.pp[k].radius;
    infillingUseClearance = spec.pp[k].addInternalClearance;
    accumInfillingsHolder.clear();
    accumInfillings = &accumInfillingsHolder;//&result.toolpaths[k]; //this gets inserted in real time
    infillingRecursive = spec.pp[k].infillingRecursive || (!spec.pp[k].medialAxisFactorsForInfillings.empty());
    switch (spec.pp[k].infillingMode) {
    case InfillingConcentric: {
        HoledPolygons hps;
        AddPathsToHPs(infillingAreas, hps);
        //use the stepping grid for this process, but switch to snapSimple
        applySnapConcentricInfilling = spec.pp[k].applysnap;
        if (applySnapConcentricInfilling) {
            concentricInfillingSnapSpec = spec.pp[k].snapspec;
            concentricInfillingSnapSpec.mode = SnapSimple;
        }
        for (auto hp = hps.begin(); hp != hps.end(); ++hp) {
            if (!processInfillingsConcentricRecursive(*hp)) return false;
        }
        break;
    } case InfillingRectilinearV:
      case InfillingRectilinearH:
        if (spec.pp[k].infillingWhole) {
            processInfillingsRectilinear(k, infillingAreas, getBB(infillingAreas), spec.pp[k].infillingMode == InfillingRectilinearH);
        } else {
            HoledPolygons hps;
            AddPathsToHPs(infillingAreas, hps);
            clp::Paths subinfillings;
            for (auto hp = hps.begin(); hp != hps.end(); ++hp) {
                subinfillings.clear();
                BBox bb = getBB(*hp);
                hp->moveToPaths(subinfillings);
                processInfillingsRectilinear(k, subinfillings, bb, spec.pp[k].infillingMode == InfillingRectilinearH);
            }
        }
        break;
    }
    return true;
}


/////////////////////////////////////////////////
//MEDIAL AXIS HELPERS
/////////////////////////////////////////////////

/*HIGH LEVEL DESCRIPTION OF THIS VERSION:
    CONVERT shapes TO HOLEDPOLYGONS
    FOR EACH FACTOR:
        FOR EACH HOLEDPOLYGON:
            ERODE HOLEDPOLYGON
            APPLY MEDIAL AXIS
            REMOVE ALL MEDIAL AXES FROM HOLEDPOLYGON
    CONVERT HOLEDPOLYGONS TO shapes
*/
void Multislicer::applyMedialAxisNotAggregated(size_t k, std::vector<double> &medialAxisFactors, std::vector<clp::Paths> &accumContours, clp::Paths &shapes, clp::Paths &medialaxis_accumulator) {
    if (medialAxisFactors.size() == 0) return;
    //convert the eroded remaining contours to HoledPolygons, treat each one separately
    HoledPolygons hps1, hps2, *hps = &hps1, *newhps = &hps2;
    HoledPolygons offsetedhps;
    clp::Paths accum_medialaxis;
    clp::Paths medialaxis;
    std::vector<clp::Paths> *inflated_acumulator = &accumContours;
    AddPathsToHPs(shapes, *hps);
#ifdef TRY_TO_AVOID_EXTENDING_BIFURCATIONS
    //relative tolerance adjusted to be 100 for a toolpath with a 10um radius
    //clp::cInt TOLERANCE = (clp::cInt)(scaled / 10000.0);
    //absolute tolerance, seems more sensible because this is to be applied to the output of
    //the Voronoi algorithm, which should not be affected by the resolution of the toolpath
    const clp::cInt TOLERANCE = 0;
#endif
    for (auto medialAxisFactor = medialAxisFactors.begin(); medialAxisFactor != medialAxisFactors.end(); ++medialAxisFactor) {
        double factor = (double)spec.pp[k].radius * (*medialAxisFactor);
        double minwidth = factor / 2.0;
        double maxwidth = factor * 2.0;
        newhps->clear();
        //newhps->reserve(hps->size());
        clp::Paths aux;
        for (HoledPolygons::iterator hp = hps->begin(); hp != hps->end(); ++hp) {
            accum_medialaxis.clear();
            hp->offset(offset, -factor, offsetedhps);
            for (HoledPolygons::iterator ohp = offsetedhps.begin(); ohp != offsetedhps.end(); ++ohp) {
                medialaxis.clear();
                //TODO: tweak min_width and max_width
                prunedMedialAxis(*ohp, clipper, medialaxis, minwidth, maxwidth
#ifdef TRY_TO_AVOID_EXTENDING_BIFURCATIONS
                    , TOLERANCE
#endif
                    );
                MOVETO(medialaxis, accum_medialaxis);
            }
            offsetedhps.clear();
            //offset the medial axis paths and substract the result from the remaining contours
            clipper.AddPath(hp->contour, clp::ptSubject, true);
            clipper.AddPaths(hp->holes, clp::ptSubject, true);
            clp::PolyTree pt;
            operateInflatedLinesAndContoursInClipper(clp::ctDifference, pt, accum_medialaxis, (double)spec.pp[k].radius, &aux, inflated_acumulator);
            AddPolyTreeToHPs(pt, *newhps);
            MOVETO(accum_medialaxis, medialaxis_accumulator);
        }
        std::swap(hps, newhps);
    }
    //do not update the shapes
    /*shapes.clear();
    AddHPsToPaths(*hps, shapes);*/
}



/////////////////////////////////////////////////
//MULTISLICING LOGIC
/////////////////////////////////////////////////

//this is a flag for the logic of motion planning
const bool LUMP_CONTOURS_AND_INFILLINGS_TOGETHER = true;

bool Multislicer::applyProcess(SingleProcessOutput &output, clp::Paths &contours_tofill, clp::Paths &contours_alreadyfilled, int k) {

    //start boilerplate

    err = &output.err;
    this->clear();
    infillingsIndependentContours = &output.infillingsIndependentContours;

    //INTERIM HACK FOR add/sub
    bool nextProcessSameKind, previousProcessSameKind;
    if (k == 0) {
        nextProcessSameKind = !spec.global.addsubWorkflowMode;
        previousProcessSameKind = true;
    } else {
        nextProcessSameKind = true;
        previousProcessSameKind = !spec.global.addsubWorkflowMode;
    }

    bool CUSTOMINFILLINGS = (spec.pp[k].infillingMode == InfillingConcentric)   ||
                            (spec.pp[k].infillingMode == InfillingRectilinearH) ||
                            (spec.pp[k].infillingMode == InfillingRectilinearV);

    clp::Paths *infillingAreas = CUSTOMINFILLINGS ? &AUX1 : &output.infillingAreas;
    infillingAreas->clear();

    clp::Paths &lowres = AUX2;
    clp::Paths *contourToProcess;

    bool notthelast = (k + 1) < spec.numspecs;

    //this only makes sense if there is a tool with higher resolution down the line
    if (spec.pp[k].doPreprocessing && notthelast) {
        if (nextProcessSameKind) {
            removeHighResDetails(k, contours_tofill, lowres, AUX3, AUX4);
        } else {
            overwriteHighResDetails(k, contours_tofill, lowres, AUX3);
        }
        contourToProcess = &lowres;
    } else {
        contourToProcess = &contours_tofill;
    }

    clp::Paths &unprocessedToolPaths = AUX3;
    if (!generateToolPath(k, nextProcessSameKind, *contourToProcess, output.toolpaths, unprocessedToolPaths, AUX4)) {
        this->clear();
        return false;
    }

    //SHOWCONTOURS(spec.global.config, "just_after_generating_toolpath", &contours_tofill, &lowres, &unprocessedToolPaths);

    //compute the contours from the toolpath (sadly, it cannot be optimized away, in any of the code paths
    offsetDo(offset, output.contours, (double)spec.pp[k].radius, unprocessedToolPaths, clp::jtRound, clp::etClosedPolygon);

    //if required, discard common toolpaths.
    bool discardCommonToolpaths = spec.useContoursAlreadyFilled(k);

    if (discardCommonToolpaths) {
        doDiscardCommonToolPaths(k, output.toolpaths, contours_alreadyfilled, AUX4);
    }
    if (nextProcessSameKind && CUSTOMINFILLINGS && spec.pp[k].infillingRecursive) {
        output.infillingsIndependentContours.resize(1);
        offsetDo(offset, output.infillingsIndependentContours[0], (double)spec.pp[k].radius, output.toolpaths, clp::jtRound, clp::etOpenRound);
    }

    //generate the infilling contour only if necessary
    output.alsoInfillingAreas = (spec.pp[k].infillingMode==InfillingJustContours);
    //the generation of the infilling regions depends on the flag discardCommonToolpaths
    if (spec.pp[k].infillingMode != InfillingNone) {
        if (discardCommonToolpaths) {
            //this is more expensive than the alternative, but necessary for possibly open toolpaths
            if (output.infillingsIndependentContours.empty()) {
                operateInflatedLinesAndContours(clp::ctDifference, *infillingAreas, output.contours, output.toolpaths, (double)spec.pp[k].radius, &AUX4, (clp::Paths*)NULL);
            } else {
                clipperDo(clipper, *infillingAreas, clp::ctDifference, output.contours, output.infillingsIndependentContours[0], clp::pftNonZero, clp::pftNonZero);
            }
        } else {
            //TODO: change the constant 0.3 by a parameter (that is, a parameter to tune the shrink factor for the the infilling, if no clearance is needed)
            //ALTERNATIVE TODO: make the shrink factor a non-conditional parameter, but the user should then remember to set it appropriately if clearance is required
            //0.99: cannot be 1.0, clipping / round-off errors crop up
            double shrinkFactor = (spec.pp[k].addInternalClearance) ? 0.99 : 0.3;
            offsetDo(offset, *infillingAreas, -(double)spec.pp[k].radius * shrinkFactor, unprocessedToolPaths, clp::jtRound, clp::etClosedPolygon);
        }
    }

    if (CUSTOMINFILLINGS) {
        AUX2.clear();
        //NOTE: using unprocessedToolPaths here is semantically dubious. REVISIT LATER!
        if (!processInfillings(k, *infillingAreas, unprocessedToolPaths)) {
            this->clear();
            return false;
        }
    }

    AUX4.clear();
    clp::Paths &intermediate_medialaxis = AUX4;

    //if ((!output.infillingsIndependentContours.empty()) && (!spec.pp[k].medialAxisFactorsForInfillings.empty())) {
    if (nextProcessSameKind && CUSTOMINFILLINGS && (!spec.pp[k].medialAxisFactorsForInfillings.empty())) {
        //showContours(output.infillingsIndependentContours, ShowContoursInfo(spec.global.config, "see infilling contours"));
        clipperDo(clipper, accumNonCoveredByInfillings, clp::ctDifference, *infillingAreas, output.infillingsIndependentContours, clp::pftNonZero, clp::pftNonZero);
        //elsewhere in the code we use !infillingsIndependentContours.empty() as a test to see if we are doing recursive infillings, so we clear it to make sure we do not break that logic
        if (!spec.pp[k].infillingRecursive) output.infillingsIndependentContours.clear();
        //SHOWCONTOURS(spec.global.config, "accumNonConveredByInfillings", &accumNonCoveredByInfillings);
        applyMedialAxisNotAggregated(k, spec.pp[k].medialAxisFactorsForInfillings, output.medialAxisIndependentContours, accumNonCoveredByInfillings, intermediate_medialaxis);
    }

    if (!spec.pp[k].medialAxisFactors.empty()) {
      
        //clp::Paths *intermediate_paths = nextProcessSameKind ? &contours_tofill : &AUX3;
        clp::Paths *intermediate_paths = &AUX3;

        clipperDo(clipper, *intermediate_paths, clp::ctDifference, contours_tofill, output.contours, clp::pftEvenOdd, clp::pftEvenOdd);
        //SHOWCONTOURS(spec.global.config, "just_before_applying_medialaxis", &contours_tofill, &unprocessedToolPaths, &output.contours, intermediate_paths);

        //but now, apply the medial axis algorithm!!!!
        applyMedialAxisNotAggregated(k, spec.pp[k].medialAxisFactors, output.medialAxisIndependentContours, *intermediate_paths, intermediate_medialaxis);
    }

    if (output.infillingsIndependentContours.empty() && nextProcessSameKind) {
        //in this case, we are not interested in having the medial axis contours as a separated set of contours
        unitePaths(clipper, output.contours, output.medialAxisIndependentContours, output.contours);
        if (spec.global.alsoContours) {
            COPYTO(output.contours, output.contoursToShow);
        }
        output.medialAxisIndependentContours.clear();
    } else {
        /*in this case, artificial holes may be generated as artifacts
        (either from overprints if !nextProcessSameKind, or from non-filled
        interiors if !output.infillingsIndependentContours.empty()). The
        3D profile of these holes cannot be approximated by eroding them
        as for holes in contours.*/
        unitePaths(clipper, output.contours, output.contours);
        if (spec.global.alsoContours) {
            COPYTO(output.contours, output.contoursToShow);
            COPYTO(output.medialAxisIndependentContours, output.contoursToShow);
            COPYTO(output.infillingsIndependentContours, output.contoursToShow);
        }
    }

    /*TODO: right now, the motion planning makes little sense. it would be better to do each island separately:
        -do one HoledPolygon
        -do its outer medial axes (possibly merge with the next step)
        -do its infilling and inner medial axes
        -go to nearest HoledPolygons and repeat
    this would require to use HoledPolygons as the native datatype and keep the infillings and medial axes
    classified by HoledPolygon (and innerness in the case of the medial axes), modifying the above algorithms
    accordingly.
    it would also require to modify the motion planner to use the combination of HoledPolygons and infillings
    separated by HoledPolygon. Right now this is the workflow:
        
    IF LUMP_CONTOURS_AND_INFILLINGS_TOGETHER:
        -all is lumped together, then motion planning is done
    ELSE IF THERE ARE NOT CUSTOM INFILLINGS:
        -contours and medial axes are lumped together, then motion planning is done
    ELSE
        -contours are planned, then custom infillings and (if applicable) medial axes are lumped together and then motion planning is done for them
    Cyclomatic complexity is quite big, redundant code is minimized
        
    */
    bool use_infillings = CUSTOMINFILLINGS && !accumInfillingsHolder.empty();
    bool use_medialaxis = !spec.pp[k].medialAxisFactors.empty() && !intermediate_medialaxis.empty();
    bool add_medialaxis_now = (LUMP_CONTOURS_AND_INFILLINGS_TOGETHER || !spec.global.applyMotionPlanner || !use_infillings) && use_medialaxis;
    bool add_infillings_now = (LUMP_CONTOURS_AND_INFILLINGS_TOGETHER || !spec.global.applyMotionPlanner) && use_infillings;
    if (add_medialaxis_now) {
        MOVETO(intermediate_medialaxis, output.toolpaths);
        intermediate_medialaxis.clear();
    }
    if (add_infillings_now) {
        MOVETO(accumInfillingsHolder, output.toolpaths);
        accumInfillingsHolder.clear();
    }

    if (spec.global.applyMotionPlanner) {
        //IMPORTANT: this must be the LAST step in the processing of the toolpaths and contours. Any furhter processing will ruin this (and probably be ruined as well)

        //trying to do the thing below does not feel very good
        verySimpleMotionPlanner(spec.startState, PathOpen, output.toolpaths); //these are open toolpaths (contains a mix of open and actually closed ones)

        /*the order IS important (start_near is a side-effected configuration value),
        and should be the same as the output order in the gcode pipeline (alternatively,
        if the gcode pipeline can reorder arbitrarily both kinds of objects
        (currently the company's AutoCAD app does not), this should be completely redesigned
        (and probably deferred until actual gcode creation)*/
        if (!LUMP_CONTOURS_AND_INFILLINGS_TOGETHER) {
            if (use_infillings) {
                //INVARIANT: add_medialaxis_now is false
                if (!spec.pp[k].medialAxisFactors.empty()) {
                    MOVETO(intermediate_medialaxis, accumInfillingsHolder);
                    intermediate_medialaxis.clear();
                }
                verySimpleMotionPlanner(spec.startState, PathOpen, accumInfillingsHolder);
                MOVETO(accumInfillingsHolder, output.toolpaths);
                accumInfillingsHolder.clear();
            }
        }
    }

    this->clear();
    return true;
}


int Multislicer::applyProcesses(std::vector<SingleProcessOutput*> &outputs, clp::Paths &contours_tofill, clp::Paths &contours_alreadyfilled, int kinit, int kend) {
    if (spec.global.substractiveOuter) {
        addOuter(contours_tofill, spec.global.limitX, spec.global.limitY);
    }
    if (spec.global.correct || spec.global.substractiveOuter) {
        orientPaths(contours_tofill);
    }
    if (kinit < 0) kinit = 0;
    if (kend < 0) kend = (int)spec.numspecs - 1;
    ++kend;
    int k;
    for (k = kinit; k < kend; ++k) {
        if (spec.useContoursAlreadyFilled(k)) {
            clipper.AddPaths(contours_alreadyfilled, clp::ptSubject, true);
            if (k > kinit) {
                if (outputs[k - 1]->infillingsIndependentContours.empty()) {
                    clipper.AddPaths(outputs[k - 1]->contours, clp::ptSubject, true);
                } else {
                    AddPaths(clipper, outputs[k - 1]->infillingsIndependentContours, clp::ptSubject, true);
                }
                AddPaths(clipper, outputs[k - 1]->medialAxisIndependentContours, clp::ptSubject, true);
            }
            clipper.Execute(clp::ctUnion, contours_alreadyfilled, clp::pftNonZero, clp::pftNonZero);
            clipper.Clear();
        }
        //clp::Paths MYAUX = contours_tofill;
        //SHOWCONTOURS(spec.global.config, "before_applying_process_1", &contours_tofill);
        //SHOWCONTOURS(spec.global.config, "before_applying_process_2", &contours_alreadyfilled);
        bool ok = applyProcess(*(outputs[k]), contours_tofill, contours_alreadyfilled, k);
        //SHOWCONTOURS(spec.global.config, "after_applying_process", &contours_tofill, &(outputs[k]->contours));
        if (!ok) break;
        if (spec.global.substractiveOuter) {
            removeOuter(outputs[k]->toolpaths,          spec.global.outerLimitX, spec.global.outerLimitY);
            if (outputs[k]->alsoInfillingAreas) {
                removeOuter(outputs[k]->infillingAreas, spec.global.outerLimitX, spec.global.outerLimitY);
            }
        }
        bool nextProcessSameKind = (!spec.global.addsubWorkflowMode) || (k > 0);
        if (nextProcessSameKind) {
            if (outputs[k]->infillingsIndependentContours.empty()) {
                clipperDo(clipper, contours_tofill, clp::ctDifference, contours_tofill, outputs[k]->contours, clp::pftNonZero, clp::pftNonZero);
            } else {
                AddPaths(clipper, contours_tofill, clp::ptSubject, true);
                AddPaths(clipper, outputs[k]->infillingsIndependentContours, clp::ptClip, true);
                AddPaths(clipper, outputs[k]->medialAxisIndependentContours, clp::ptClip, true);
                clipper.Execute(clp::ctDifference, contours_tofill, clp::pftNonZero, clp::pftNonZero);
                clipper.Clear();
                //SHOWCONTOURS(spec.global.config, "after_applying_infillings_1", &(contours_tofill));
            }
        } else {
            //clp::Paths MYAUX = contours_tofill;
            AddPaths(clipper, outputs[k]->contours, clp::ptSubject, true);
            AddPaths(clipper, outputs[k]->medialAxisIndependentContours, clp::ptSubject, true);
            AddPaths(clipper, contours_tofill, clp::ptClip, true);
            clipper.Execute(clp::ctDifference, contours_tofill, clp::pftNonZero, clp::pftNonZero);
            clipper.Clear();
            //SHOWCONTOURS(spec.global.config, "output contours", &MYAUX, &outputs[k]->contours, &contours_tofill);
            //clipperDo(clipper, contours_tofill, clp::ctDifference, outputs[k]->contours, contours_tofill, clp::pftEvenOdd, clp::pftEvenOdd);
            //SHOWCONTOURS(spec.global.config, "after_addsub_switch", &MYAUX, &outputs[k]->contours, &contours_tofill);// , &contours_alreadyfilled);
        }
    }
    return k;
}

