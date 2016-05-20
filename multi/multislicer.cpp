#include "multislicer.hpp"
#include "orientPaths.hpp"
#include "medialaxis.hpp"
#include "showcontours.hpp"
#include <algorithm>

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
        return ((std::abs(minx) >= this->limitX) ||
                (std::abs(maxx) >= this->limitX) ||
                (std::abs(miny) >= this->limitY) ||
                (std::abs(maxy) >= this->limitY));
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


/////////////////////////////////////////////////
/*ClippingResources STATELESS, LOW LEVEL HELPER TEMPLATES*/
/////////////////////////////////////////////////

template<typename T, typename INFLATEDACCUM> void ClippingResources::operateInflatedLinesAndContoursInClipper(clp::ClipType mode, T &res, clp::Paths &lines, double radius, clp::Paths *aux, INFLATEDACCUM* inflated_acumulator) {
    for (clp::Paths::iterator line = lines.begin(); line != lines.end(); ++line) {
        aux->clear();
        offsetDo(*aux, radius, *line, clp::jtRound, clp::etOpenRound);
        clipper.AddPaths(*aux, clp::ptClip, true);
        if (inflated_acumulator != NULL) {
            MOVETO(*aux, *inflated_acumulator);
        }
    }
    clipper.Execute(mode, res, clp::pftEvenOdd, clp::pftNonZero);
    clipper.Clear();
    if (!std::is_same<T, clp::PolyTree>::value) ClipperEndOperation(clipper);
}

template<typename T, typename INFLATEDACCUM> void ClippingResources::operateInflatedLinesAndContours(clp::ClipType mode, T &res, clp::Paths &contours, clp::Paths &lines, double radius, clp::Paths *aux, INFLATEDACCUM* inflated_acumulator) {
    clipper.AddPaths(contours, clp::ptSubject, true);
    operateInflatedLinesAndContoursInClipper(mode, res, lines, radius, aux, inflated_acumulator);
}

template<typename Output, typename... Inputs> inline void ClippingResources::unitePaths(Output &output, Inputs&... inputs) {
    char dummy[sizeof...(Inputs)] = { (AddPaths(inputs, clp::ptSubject, true), (char)0)... };
    //maybe clp::pftPositive is better?
    clipper.Execute(clp::ctUnion, output, clp::pftNonZero, clp::pftNonZero);
    clipper.Clear();
    if (!std::is_same<Output, clp::PolyTree>::value) ClipperEndOperation(clipper);
}

template<typename Output, typename Input> void ClippingResources::offsetDo2(Output &output, double delta1, double delta2, Input &input, clp::Paths &aux, clp::JoinType jointype, clp::EndType endtype) {
    AddPaths(input, jointype, endtype);
    offset.Execute(aux, delta1);
    offset.Clear();
    ClipperEndOperation(offset);
    offset.AddPaths(aux, jointype, endtype);
    offset.Execute(output, delta2);
    offset.Clear();
    if (!std::is_same<Output, clp::PolyTree>::value) ClipperEndOperation(offset);
}


/////////////////////////////////////////////////
/*ClippingResources STATELESS, HIGH LEVEL HELPER METHODS

auxiliar variables are passed around in order to avoid recurring std::vector growing costs*/
/////////////////////////////////////////////////

void ClippingResources::removeHighResDetails(size_t k, clp::Paths &contours, clp::Paths &lowres, clp::Paths &opened, clp::Paths &aux1) {
    auto &ppspec     = spec->pp[k];

    //lowres is used at the end as output, but before that is used as temporary variable

    //remove high-resolution positive details from paths
    //opened <- opening(paths, radius)
    offset.ArcTolerance = (double)ppspec.arctolG;
    offsetDo2(opened, (double)-ppspec.radius, (double)ppspec.radius, contours, lowres, clp::jtRound, clp::etClosedPolygon);

    if (!ppspec.applysnap) {
        //if we are not snapping to grid, operations below do not apply
        lowres = std::move(opened); //RESULT IS RETURNED IN lowres
        return;
    }

    //add high-resolution negative space
    //aux1 <- closing(opened, substep)
    //offset.ArcTolerance = (double)ppspec.arctolG;
    offsetDo2(aux1, (double)ppspec.substep, (double)-ppspec.substep * 1.0, opened, lowres, clp::jtRound, clp::etClosedPolygon);

    //separate negative space
    //lowres <- aux1 - opened
    clipperDo(lowres, clp::ctDifference, aux1, opened, clp::pftEvenOdd, clp::pftEvenOdd);

    /*we may expect lowres to be completely like paths in low resolution areas.
    However, it seems that lowres has long, narrow strips around paths in some
    areas, hinting that it is somehow slightly more extended than it should
    be. This would lead to unnecessary removal of material to accomodate
    non-existent high-resolution negative space. To avoid this problem, we
    apply here an opening with a very small radius.
    TODO: We are taking smallRadius, but we should determine an optimal radius*/
    auto &nextppspec = spec->pp[k + 1];
    offset.ArcTolerance = (double)nextppspec.arctolR; //arctolsmall//arctolgridstep
    offsetDo2(lowres, (double)-nextppspec.radius, (double)nextppspec.radius, lowres, aux1, clp::jtRound, clp::etClosedPolygon);

    //dilate separated negative space
    //aux1 <- dilate(lowres, dilatestep)
    offset.ArcTolerance = (double)ppspec.arctolG;
    offsetDo(aux1, (double)ppspec.dilatestep, lowres, clp::jtRound, clp::etClosedPolygon);

    //remove high-resolution negative details from paths
    //lowres <- opened - aux1
    clipperDo(lowres, clp::ctDifference, opened, aux1, clp::pftEvenOdd, clp::pftEvenOdd);

    //RESULT IS RETURNED IN lowres
}

void ClippingResources::overwriteHighResDetails(size_t k, clp::Paths &contours, clp::Paths &lowres, clp::Paths &aux1, clp::Paths &aux2) {
    auto &ppspec    = spec->pp[k];
    auto &fattening = spec->global.addsub.fattening;

    //remove high-res negative details.
    offset.ArcTolerance = (double)ppspec.arctolG;
    double negativeHighResFactor = (double)ppspec.substep * 1.1; //minimal factor
    if (fattening.eraseHighResNegDetails && (((double)fattening.eraseHighResNegDetails_radius) > negativeHighResFactor)) {
        negativeHighResFactor = (double)fattening.eraseHighResNegDetails_radius;
    }
    offsetDo2(lowres, negativeHighResFactor, -negativeHighResFactor, contours, aux1, clp::jtRound, clp::etClosedPolygon);

    if (fattening.useGradualFattening) {
        clp::Paths fat, thin, next;
        double r = (double)ppspec.radius;
        if (ppspec.addInternalClearance) {
            r *= 2;
        }
        //compute the low-res contours with a naive toolpath
        offsetDo2(fat, -r, r, lowres, aux1, clp::jtRound, clp::etClosedPolygon);
        //get the areas that are high-res
        clipperDo(thin, clp::ctDifference, lowres, fat, clp::pftEvenOdd, clp::pftEvenOdd);
        //TODO: expose smallNeg as a configurable parameter!!!
        double smallNeg = r*0.01; //remove spurious ultrathin components
        offsetDo2(thin, -smallNeg, smallNeg, thin, aux1, clp::jtRound, clp::etClosedPolygon);
        //SHOWCONTOURS(*spec->global.config, "initial fat and thin", &lowres, &fat, &thin);

        //if thin is empty, we have nothing more to do, since we have no high-res areas!
        if (thin.empty()) return;

        //accumulate the low-res parts
        clipper2.AddPaths(fat, clp::ptSubject, true);

        //in the sequence of gradual steps:
        //    -radiusFactor should be strictly decreasing in the range (1, 0]
        //    -inflateFactor should be strictly increasing in the range [0,1]
        //    -SUCH THAT, in each step, (radiusFactor+inflateFactor)>=1
        auto endi = fattening.gradual.end();
        for (auto step = fattening.gradual.begin(); step != endi; ++step) {
            double gradradius  = r*step->radiusFactor;
            double gradinflate = r*step->inflateFactor;
            if (gradradius != 0.0) {
                const double gradradius_from_fat = 1.1*gradradius;
                //get the portion of "thin" which is at least as wide as gradrad
                offsetDo2(next, -gradradius, gradradius, thin, aux1, clp::jtRound, clp::etClosedPolygon);
                //some parts of "thin" are not as wide as gradrad, but are adjacent to "fat", so we inflate "fat" to encompass them
                offsetDo(aux1, gradradius_from_fat, fat, clp::jtSquare, clp::etClosedPolygon);
                unitePaths(aux2, next, aux1);
                //get the portion of "thin" which is either near enough of "fat", or at least as wide as gradrad
                clipperDo(aux1, clp::ctIntersection, thin, aux2, clp::pftEvenOdd, clp::pftEvenOdd);
                if (aux1.empty()) continue;
                clp::Paths *subresult;
                if (gradinflate != 0.0) {
                    //inflate the previously computed protion of "thin" by the required amount
                    offsetDo(aux2, gradinflate, aux1, clp::jtRound, clp::etClosedPolygon);
                    subresult = &aux2;
                } else {
                    subresult = &aux1;
                }
                //SHOWCONTOURS(*spec->global.config, str("fat, thin, next, subresult ", step-fattening.gradual.begin()), &fat, &thin, &next, subresult);
                //accumulate the result
                clipper2.AddPaths(*subresult, clp::ptSubject, true);
                //if we are not in the last iteration, prepare the vars for the next one!
                if ((endi - step) > 1) {
                    //get the new "thin", as the non-fattened portion of "thin"
                    clipperDo(thin, clp::ctDifference, thin, aux1, clp::pftEvenOdd, clp::pftEvenOdd);
                    //set the new fat
                    fat = std::move(aux1);
                }
            } else {
                if (gradinflate != 0.0) {
                    offsetDo(aux2, gradinflate, thin, clp::jtRound, clp::etClosedPolygon);
                    clipper2.AddPaths(aux2, clp::ptSubject, true);
                } else {
                    clipper2.AddPaths(thin, clp::ptSubject, true);
                }
                break;
            }
        }

        //clp::Paths old_lowres = lowres;
        clipper2.Execute(clp::ctUnion, lowres, clp::pftNonZero, clp::pftNonZero);
        clipper2.Clear();
        ClipperEndOperation(clipper2);
        //SHOWCONTOURS(*spec->global.config, "contour before and after overwriting", &old_lowres, &lowres);
    }

    //RESULT IS RETURNED IN lowres
}

void ClippingResources::doDiscardCommonToolPaths(size_t k, clp::Paths &toolpaths, clp::Paths &contours_alreadyfilled, clp::Paths &aux1) {
    auto &ppspec = spec->pp[k];

    //admitedly, this way to remove common arcs betwen adjacent paths is expensive and overkill
    //maybe there is way to directly extract the common arcs from the guts of CliperLib::Clipper.
    //but answering that question would require a very intimate knowledge of the internals of that library. too little time to do it right now.

    //we will not overwrite aux2 because its contents will be needed later in the loop!

    //now, offset all previous contours, and accumulate them
    clp::Paths previousContours;
    clipper.AddPaths(toolpaths, clp::ptSubject, false);
    double globalRadius = (double)ppspec.radius + (double)ppspec.radiusRemoveCommon;
    offset.ArcTolerance = (double)ppspec.arctolG;
    offsetDo(aux1, globalRadius, contours_alreadyfilled, clp::jtRound, clp::etClosedPolygon);
    clipper.AddPaths(aux1, clp::ptClip, true);
    //execute the difference. NOTE: for intersected paths, the result can be either an open path or a pair of open paths for each path sharing a common arc with lower resolution contours.
    //the latter (two paths) happens if the endpoint is not in the common arc. unintersected paths should not be affected by the operation
    clp::PolyTree pt;
    clipper.Execute(clp::ctDifference, pt, clp::pftEvenOdd, clp::pftEvenOdd);
    clipper.Clear();
    clp::PolyTreeToPaths(pt, toolpaths); //copies both closed and open paths
    ClipperEndOperation(clipper, &pt);
}

bool ClippingResources::generateToolPath(size_t k, bool nextProcessSameKind, clp::Paths &contour, clp::Paths &toolpaths, clp::Paths &temp_toolpath, clp::Paths &aux1) {
    auto &ppspec = spec->pp[k];

    /*erode to get the toolpath
    use gridstep's arcTolerance because we have details at that scale,
    after removing negative details).*/
    //toolpath <- erode(contour, radius)
    offset.ArcTolerance = (double)ppspec.arctolG;
    offsetDo(temp_toolpath, (double)-ppspec.radius, contour, clp::jtRound, clp::etClosedPolygon);
    //clp::Paths beforesnap = temp_toolpath;

    //IT IS RECOMMENDED TO DO SNAPPING FOR ALL PATHS, BECAUSE IT SEEMS TO REMOVE SLIGHT IMPERFECTIONS WHICH CAN CAUSE PROBLEMS LATER

    //We ALSO add a SNAPTOGRID operation to get a finely tuned
    //big-radius toolpath.
    if (ppspec.applysnap) {
        //toolpath <- opening(toolpath, safestep)
        if (nextProcessSameKind) {
            offsetDo2(temp_toolpath, -ppspec.safestep, ppspec.safestep, temp_toolpath, aux1, clp::jtRound, clp::etClosedPolygon);
        } else {
            ppspec.snapspec.mode = SnapDilate;
        }
        //aux1 <- snapToGrid(toolpath, gridstep, doErosion)
        bool ok = snapClipperPathsToGrid(*spec->global.config, aux1, temp_toolpath, ppspec.snapspec, *err);
        if (!ok) return false;
        std::swap(aux1, temp_toolpath);
    } else {
        if (ppspec.addInternalClearance) {
            offsetDo2(temp_toolpath, (double)-ppspec.radius, (double)ppspec.radius, temp_toolpath, aux1, clp::jtRound, clp::etClosedPolygon);
        } else {
            if (ppspec.burrLength>0) {
                //toolpath <- opening(toolpath, safestep)
                double oldmiter = offset.MiterLimit;
                offset.MiterLimit = 10;
                offsetDo2(temp_toolpath, (double)-ppspec.burrLength, (double)ppspec.burrLength, temp_toolpath, aux1, clp::jtSquare, clp::etClosedPolygon);
                offset.MiterLimit = oldmiter; //should not be necessary, but just in case...
            }
        }
    }

    //SHOWCONTOURS(*spec->global.config, "contour, toolpath before and after snap", &contour, &beforesnap, &temp_toolpath);
    //add to the end the initial point of each contour in the toolpath (this is necessary to correctly operate with the toolpaths as a set of open paths in clipperlib, but also for proper toolpath output)
    applyToPaths<copyOpenToClosedPath, ReserveCapacity>(temp_toolpath, toolpaths);
    return true;
}


////    MEDIAL AXIS HELPER    ////
/*HIGH LEVEL DESCRIPTION OF THIS VERSION:
    CONVERT shapes TO HOLEDPOLYGONS
    FOR EACH FACTOR:
        FOR EACH HOLEDPOLYGON:
            ERODE HOLEDPOLYGON
            APPLY MEDIAL AXIS
            REMOVE ALL MEDIAL AXES FROM HOLEDPOLYGON
    CONVERT HOLEDPOLYGONS TO shapes
*/
bool ClippingResources::applyMedialAxisNotAggregated(size_t k, std::vector<double> &medialAxisFactors, std::vector<clp::Paths> &accumContours, clp::Paths &shapes, clp::Paths &medialaxis_accumulator) {
    bool linesHaveBeenComputed = false;
    if (medialAxisFactors.size() == 0) return linesHaveBeenComputed;
    auto &ppspec = spec->pp[k];

    //convert the eroded remaining contours to HoledPolygons, treat each one separately
    HoledPolygons hps1, hps2, *hps = &hps1, *newhps = &hps2;
    HoledPolygons offsetedhps;
    clp::Paths accum_medialaxis;
    clp::Paths medialaxis;
    std::vector<clp::Paths> *inflated_acumulator = &accumContours;
    AddPathsToHPs(clipper, shapes, *hps);
    double minidelta = 0.01 * ppspec.radius * *std::min_element(medialAxisFactors.begin(), medialAxisFactors.end());
#ifdef TRY_TO_AVOID_EXTENDING_BIFURCATIONS
    //relative tolerance adjusted to be 100 for a toolpath with a 10um radius
    //clp::cInt TOLERANCE = (clp::cInt)(scaled / 10000.0);
    //absolute tolerance, seems more sensible because this is to be applied to the output of
    //the Voronoi algorithm, which should not be affected by the resolution of the toolpath
    const clp::cInt TOLERANCE = 0;
#endif
    for (auto medialAxisFactor = medialAxisFactors.begin(); medialAxisFactor != medialAxisFactors.end(); ++medialAxisFactor) {
        double factor = (double)ppspec.radius * (*medialAxisFactor);
        double minwidth = factor / 2.0;
        double maxwidth = factor * 2.0;
        newhps->clear();
        //newhps->reserve(hps->size());
        clp::Paths aux;
        for (HoledPolygons::iterator hp = hps->begin(); hp != hps->end(); ++hp) {
            accum_medialaxis.clear();
            if (minidelta > 0) {
                //offset by a slightly bigger amount in order to avoid features that may be pathologically thin, with the potential to crash the medial axis algorithm
                hp->offset2(offset, -factor - minidelta, minidelta, offsetedhps);
            } else {
                hp->offset(offset, -factor, offsetedhps);
            }
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
            operateInflatedLinesAndContoursInClipper(clp::ctDifference, pt, accum_medialaxis, (double)ppspec.radius, &aux, inflated_acumulator);
            AddPolyTreeToHPs(pt, *newhps);
            ClipperEndOperation(clipper, &pt);
            linesHaveBeenComputed = linesHaveBeenComputed || (!accum_medialaxis.empty());
            MOVETO(accum_medialaxis, medialaxis_accumulator);
        }
        std::swap(hps, newhps);
    }
    //do not update the shapes
    /*shapes.clear();
    AddHPsToPaths(*hps, shapes);*/
}


/////////////////////////////////////////////////
/*Infiller METHODS (REQUIRE STATE, SO THEY ARE
ENCAPSULATED IN AN OBJECT*/
/////////////////////////////////////////////////

bool Infiller::processInfillings(size_t k, std::vector<clp::Paths> *_infillingsIndependentContours, clp::Paths &infillingAreas, clp::Paths &accumInfillingsHolder, clp::Paths &contoursToBeInfilled) {
    auto &ppspec = res->spec->pp[k];
    infillingsIndependentContours = _infillingsIndependentContours;
    infillingRadius = (double)ppspec.radius;
    erodedInfillingRadius = std::abs(infillingRadius*(1 - ppspec.infillingLineOverlap));
    infillingUseClearance = ppspec.addInternalClearance;
    accumInfillingsHolder.clear();
    accumInfillings = &accumInfillingsHolder;
    infillingRecursive = ppspec.infillingRecursive || (!ppspec.medialAxisFactorsForInfillings.empty());
    switch (ppspec.infillingMode) {
    case InfillingConcentric: {
        HoledPolygons hps;
        AddPathsToHPs(res->clipper, infillingAreas, hps);
        numconcentric = ppspec.useMaxConcentricRecursive ? ppspec.maxConcentricRecursive : std::numeric_limits<int>::max();
        //use the stepping grid for this process, but switch to snapSimple
        applySnapConcentricInfilling = ppspec.applysnap;
        if (applySnapConcentricInfilling) {
            concentricInfillingSnapSpec = ppspec.snapspec;
            concentricInfillingSnapSpec.mode = SnapSimple;
        }
        for (auto hp = hps.begin(); hp != hps.end(); ++hp) {
            if (!processInfillingsConcentricRecursive(*hp)) return false;
        }
        break;
    } case InfillingRectilinearV:
      case InfillingRectilinearH:
        bool horizontal = ppspec.infillingMode == InfillingRectilinearH;
        if (ppspec.infillingStatic || ppspec.infillingWhole) {
            BBox bb = getBB(infillingAreas);
            globalShift = 0; //promote to command line parameter if necessary
            useGlobalShift = ppspec.infillingStatic;
            processInfillingsRectilinear(ppspec, infillingAreas, bb, horizontal);
        } else {
            HoledPolygons hps;
            AddPathsToHPs(res->clipper, infillingAreas, hps);
            clp::Paths subinfillings;
            useGlobalShift = false;
            for (auto hp = hps.begin(); hp != hps.end(); ++hp) {
                subinfillings.clear();
                BBox bb = getBB(*hp);
                hp->moveToPaths(subinfillings);
                processInfillingsRectilinear(ppspec, subinfillings, bb, horizontal);
            }
        }
        break;
    }
    return true;
}

void Infiller::processInfillingsRectilinear(PerProcessSpec &ppspec, clp::Paths &infillingAreas, BBox &bb, bool horizontal) {
    double epsilon_start = 0;// infillingRadius * 0.01; //do not start the lines exactly on the boundary, but a little bit past it
    double epsilon_erode = infillingRadius * 0.01; //do not erode a full radius, but keep a small offset
    double erode_value = (infillingUseClearance) ? epsilon_erode - infillingRadius : 0.0;
    clp::cInt minLineSize = (clp::cInt)(infillingRadius*1.0); //do not allow ridiculously small lines
    clp::cInt delta = (clp::cInt)(2 * erodedInfillingRadius);
    clp::cInt relevantMin = (horizontal ? bb.miny : bb.minx) + (clp::cInt)epsilon_start;
    clp::cInt relevantMax =  horizontal ? bb.maxy : bb.maxx;
    clp::cInt shift       = useGlobalShift ? globalShift : relevantMin;
    clp::cInt start = (clp::cInt)std::round(((double)relevantMin - shift) / delta) - 1;
    clp::cInt end   = (clp::cInt)std::round(((double)relevantMax - shift) / delta) + 1;
    clp::cInt numlines = end - start + 1;
    clp::cInt accum = start;
    clp::Paths lines(numlines, clp::Path(2));
    for (auto &line : lines) {
        if (horizontal) {
            line.front().X = bb.minx;
            line.back().X  = bb.maxx;
            line.back().Y  =
            line.front().Y = (accum++) * delta + shift;
        } else { //vertical
            line.front().Y = bb.miny;
            line.back().Y  = bb.maxy;
            line.back().X  =
            line.front().X = (accum++) * delta + shift;
        }
    }
    if (erode_value != 0.0) {
        clp::Paths aux;
        res->offsetDo(aux, erode_value, infillingAreas, clp::jtRound, clp::etClosedPolygon);
        res->clipper.AddPaths(aux, clp::ptClip, true);
    } else {
        res->clipper.AddPaths(infillingAreas, clp::ptClip, true);
    }
    res->clipper.AddPaths(lines, clp::ptSubject, false);
    {
        clp::PolyTree pt;
        res->clipper.Execute(clp::ctIntersection, pt, clp::pftEvenOdd, clp::pftEvenOdd);
        clp::PolyTreeToPaths(pt, lines);
        ClipperEndOperation(res->clipper, &pt);
    }
    res->clipper.Clear();
    if (ppspec.applysnap) {
        verySimpleSnapPathsToGrid(lines, ppspec.snapspec);
    }
    //IMPORTANT: these lambdas test if the line distance is below the threshold. If diagonal lines are possible, a new lambda must be added to handle them!
    if (horizontal) {
        auto test = [minLineSize](clp::Path &line) {return std::abs(line.front().X - line.back().X) < minLineSize; };
        lines.erase(std::remove_if(lines.begin(), lines.end(), test), lines.end());
    } else { //vertical
        auto test = [minLineSize](clp::Path &line) {return std::abs(line.front().Y - line.back().Y) < minLineSize; };
        lines.erase(std::remove_if(lines.begin(), lines.end(), test), lines.end());
    }
    COPYTO(lines, *accumInfillings);
    if (infillingRecursive) {
        res->offsetDo(lines, infillingRadius, lines, clp::jtRound, clp::etOpenRound);
        MOVETO(lines, *infillingsIndependentContours);
    }
}

/* quite hard to get right: plenty of cases were it seems like the medial axis algorithm should be
   filling voids, but it doesn't. It may be because of two reasons: shapes are not elongated enough
   (likely in some cases, but conspicuosly narrow voids are also generated), and/or they are
   too small (remember the min_width and  max_width constraints, it is difficult to fiddle with them
   in a productive way).
*/
bool Infiller::processInfillingsConcentricRecursive(HoledPolygon &hp) {
    clp::Paths current, next, smoothed;
    hp.moveToPaths(current);
    if (infillingUseClearance) {
        //make the paths as non-intersecting as possible
        res->offsetDo(smoothed, -2 * erodedInfillingRadius, current,  clp::jtRound, clp::etClosedPolygon);
        res->offsetDo(next,          erodedInfillingRadius, smoothed, clp::jtRound, clp::etClosedPolygon);
    } else {
        //freely intersecting paths, as long as they are separated by the adequate distance
        res->offsetDo(next,         -erodedInfillingRadius, current,  clp::jtRound, clp::etClosedPolygon);
    }
    if (false){//applySnapConcentricInfilling) {
        bool ok = snapClipperPathsToGrid(*res->spec->global.config, smoothed, next, concentricInfillingSnapSpec, *res->err);
        if (!ok) return false;
        MOVETO(smoothed, next);
    }
    COPYTO(next, *accumInfillings);
    if (infillingRecursive) {
        res->offsetDo(smoothed, erodedInfillingRadius, next, clp::jtRound, clp::etOpenRound);
        MOVETO(smoothed, *infillingsIndependentContours);
    }
    res->offsetDo(current, -erodedInfillingRadius, next, clp::jtRound, clp::etClosedPolygon);
    HoledPolygons subhps;
    AddPathsToHPs(res->clipper, current, subhps);
    smoothed = current = next = clp::Paths();
    for (auto subhp = subhps.begin(); subhp != subhps.end(); ++subhp) {
        --numconcentric;
        if ((numconcentric > 0) && !processInfillingsConcentricRecursive(*subhp)) return false;
        ++numconcentric;
    }
    return true;
}


/////////////////////////////////////////////////
//MULTISLICING LOGIC
/////////////////////////////////////////////////

void setupAddsubFlags(GlobalSpec &global, int k, bool &nextProcessSameKind, bool &previousProcessSameKind) {
    if (k == 0) {
        nextProcessSameKind = !global.addsub.addsubWorkflowMode;
        previousProcessSameKind = true;
    } else {
        nextProcessSameKind = true;
        previousProcessSameKind = !global.addsub.addsubWorkflowMode;
    }
}

//applyProcess first phase: determine perimeters; start computing perimeter toolpaths (if specified)
bool Multislicer::applyProcessPhase1(SingleProcessOutput &output, clp::Paths &contours_tofill, int k) {
    //start boilerplate

    auto spec    = res->spec.get();
    auto &global = spec->global;
    auto &ppspec = spec->pp[k];
    res->err = &output.err;
    this->clear();

    //INTERIM HACK FOR add/sub
    bool nextProcessSameKind, previousProcessSameKind;
    setupAddsubFlags(global, k, nextProcessSameKind, previousProcessSameKind);
    
    clp::Paths &lowres = AUX2;
    clp::Paths *contourToProcess = &lowres;

    bool notthelast = (k + 1) < spec->numspecs;
    
    output.perimeterMedialAxesHaveBeenAdded      =
    output.infillingMedialAxesHaveBeenAdded      =
    output.contours_withexternal_medialaxis_used =
    output.phase1complete                        =
    output.phase2complete                        = false;

    //this only makes sense if there is a tool with higher resolution down the line
    if (ppspec.doPreprocessing) {
        if (nextProcessSameKind) {
            if (notthelast) {
                res->removeHighResDetails(k, contours_tofill, lowres, AUX3, AUX4);
            } else {
                if ((spec->numspecs > 1) && spec->pp[k - 1].applysnap) {
                    //if we applied snapping previously, we need to remove the small linings
                    res->offset.ArcTolerance = (double)ppspec.arctolG;
                    res->offsetDo2(lowres, -(double)ppspec.dilatestep, (double)ppspec.dilatestep, contours_tofill, AUX3, clp::jtRound, clp::etClosedPolygon);
                } else {
                    //lowres = contours_tofill; //this was needed only for doing a SHOWCONTOURS with &lowres after the big if statement
                    contourToProcess = &contours_tofill;
                }
            }
        } else {
            //with the current setup, this is triggered only in add/sub mode for the first process
            res->overwriteHighResDetails(k, contours_tofill, lowres, AUX3, AUX4);
        }
    } else {
        if (ppspec.noPreprocessingOffset == 0.0) {
            contourToProcess = &contours_tofill;
        } else {
            res->offset.ArcTolerance = (double)ppspec.arctolG;
            res->offsetDo2(lowres, -ppspec.noPreprocessingOffset, ppspec.noPreprocessingOffset, contours_tofill, AUX3, clp::jtRound, clp::etClosedPolygon);
        }
    }
    
    if (contourToProcess == &lowres) {
        output.contours = std::move(*contourToProcess);
    } else {
        output.contours = *contourToProcess;
    }
    
    if (ppspec.computeToolpaths) {
        if (!res->generateToolPath(k, nextProcessSameKind, output.contours, output.ptoolpaths, output.unprocessedToolPaths, AUX4)) {
            this->clear();
            return false;
        }

        //SHOWCONTOURS(*spec->global.config, "just_after_generating_toolpath", &contours_tofill, &unprocessedToolPaths);

        //compute the contours from the toolpath (sadly, it cannot be optimized away, in any of the code paths)
        res->offsetDo(output.contours, (double)ppspec.radius, output.unprocessedToolPaths, clp::jtRound, clp::etClosedPolygon);
    }

    if (!ppspec.medialAxisFactors.empty()) {

        //clp::Paths *intermediate_paths = nextProcessSameKind ? &contours_tofill : &AUX3;
        clp::Paths *intermediate_paths = &AUX3;

        res->clipperDo(*intermediate_paths, clp::ctDifference, contours_tofill, output.contours, clp::pftEvenOdd, clp::pftEvenOdd);
        //SHOWCONTOURS(*spec->global.config, "just_before_applying_medialaxis", &contours_tofill, &unprocessedToolPaths, &output.contours, intermediate_paths);

        //but now, apply the medial axis algorithm!!!!
        output.perimeterMedialAxesHaveBeenAdded = res->applyMedialAxisNotAggregated(k, ppspec.medialAxisFactors, output.medialAxisIndependentContours, *intermediate_paths, output.medialAxis_toolpaths);
        if (!ppspec.computeToolpaths) output.medialAxis_toolpaths = clp::Paths();
        
        if (output.perimeterMedialAxesHaveBeenAdded) {
            //this will be needed only to be used by the 3D scheduling code of a (not-yet implemented) option is specified
            output.contours_withexternal_medialaxis_used = true;
            res->unitePaths(output.contours_withexternal_medialaxis, output.medialAxisIndependentContours, output.contours);
        }
    }
  
    this->clear();
    output.phase1complete = true;
    return true;
}

//applyProcess second phase: finish computing perimter toolpaths and compute infilling toolpaths,
//apply motion planning, and arrange results in the output struct in the format expected by callers
bool Multislicer::applyProcessPhase2(SingleProcessOutput &output, clp::Paths &contours_alreadyfilled, int k) {
    //start boilerplate

    auto spec    = res->spec.get();
    auto &global = spec->global;
    auto &ppspec = spec->pp[k];
    res->err = &output.err;
    this->clear();

    //INTERIM HACK FOR add/sub
    bool nextProcessSameKind, previousProcessSameKind;
    setupAddsubFlags(global, k, nextProcessSameKind, previousProcessSameKind);

    bool CUSTOMINFILLINGS = (ppspec.infillingMode == InfillingConcentric)   ||
                            (ppspec.infillingMode == InfillingRectilinearH) ||
                            (ppspec.infillingMode == InfillingRectilinearV);

    clp::Paths *infillingAreas = CUSTOMINFILLINGS ? &AUX1 : &output.infillingAreas;
    infillingAreas->clear();
    clp::Paths *intermediate_medialaxis_infilling;
    std::vector<clp::Paths> infillings_medialAxisIndependentContours;

    if (!ppspec.computeToolpaths) {
        AUX4.clear();
        intermediate_medialaxis_infilling = &AUX4; //this may be needed to avoid errors when computing the medial axis
    } else {
        //if required, discard common toolpaths.
        bool discardCommonToolpaths = spec->useContoursAlreadyFilled(k);

        if (discardCommonToolpaths) {
            res->doDiscardCommonToolPaths(k, output.ptoolpaths, contours_alreadyfilled, AUX4);
        }
        if (nextProcessSameKind && CUSTOMINFILLINGS && ppspec.infillingRecursive) {
            output.infillingsIndependentContours.resize(1);
            res->offsetDo(output.infillingsIndependentContours[0], (double)ppspec.radius, output.ptoolpaths, clp::jtRound, clp::etOpenRound);
        }

        //generate the infilling contour only if necessary
        output.alsoInfillingAreas = (ppspec.infillingMode == InfillingJustContours);
        //the generation of the infilling regions depends on the flag discardCommonToolpaths
        if (ppspec.infillingMode != InfillingNone) {
            if (discardCommonToolpaths) {
                //this is more expensive than the alternative, but necessary for possibly open toolpaths
                if (output.infillingsIndependentContours.empty()) {
                    res->operateInflatedLinesAndContours(clp::ctDifference, *infillingAreas, output.contours, output.ptoolpaths, (double)ppspec.radius, &AUX4, (clp::Paths*)NULL);
                } else {
                    res->clipperDo(*infillingAreas, clp::ctDifference, output.contours, output.infillingsIndependentContours[0], clp::pftNonZero, clp::pftNonZero);
                }
            } else {
                //0.99: cannot be 1.0, clipping / round-off errors crop up
                double shrinkFactor = (ppspec.addInternalClearance) ? 0.99 : (1-ppspec.infillingPerimeterOverlap);
                res->offsetDo(*infillingAreas, -(double)ppspec.radius * shrinkFactor, output.unprocessedToolPaths, clp::jtRound, clp::etClosedPolygon);
            }
        }

        if (CUSTOMINFILLINGS) {
            AUX2.clear();
            //NOTE: using output.unprocessedToolPaths here is semantically dubious. REVISIT LATER!
            if (!infiller.processInfillings(k, &output.infillingsIndependentContours, *infillingAreas, accumInfillingsHolder, output.unprocessedToolPaths)) {
                this->clear();
                return false;
            }
        }

        output.unprocessedToolPaths = clp::Paths();
        AUX4.clear();
        intermediate_medialaxis_infilling = &AUX4;

        //if ((!output.infillingsIndependentContours.empty()) && (!ppspec.medialAxisFactorsForInfillings.empty())) {
        if (nextProcessSameKind && CUSTOMINFILLINGS && (!ppspec.medialAxisFactorsForInfillings.empty())) {
            AUX2.clear();
            clp::Paths &accumNonCoveredByInfillings = AUX2;
            //showContours(output.infillingsIndependentContours, ShowContoursInfo(*spec->global.config, "see infilling contours"));
            res->clipperDo(accumNonCoveredByInfillings, clp::ctDifference, *infillingAreas, output.infillingsIndependentContours, clp::pftNonZero, clp::pftNonZero);
            //elsewhere in the code we use !infillingsIndependentContours.empty() as a test to see if we are doing recursive infillings, so we clear it to make sure we do not break that logic
            if (!ppspec.infillingRecursive) output.infillingsIndependentContours.clear();
            //SHOWCONTOURS(*spec->global.config, "accumNonConveredByInfillings", &accumNonCoveredByInfillings);
            //the output in infillings_medialAxisIndependentContours should really be in the end be placed in output.medialAxisIndependentContours
            //however, we put it in a temporal variable infillings_medialAxisIndependentContours in order to be able to handle some special cases
            output.infillingMedialAxesHaveBeenAdded = res->applyMedialAxisNotAggregated(k, ppspec.medialAxisFactorsForInfillings, infillings_medialAxisIndependentContours, accumNonCoveredByInfillings, *intermediate_medialaxis_infilling);
        }

    }

    AUX2.clear();

    if (output.infillingsIndependentContours.empty() && nextProcessSameKind) {
        //in this case, we are not interested in having the medial axis contours as a separated set of contours
        if (output.contours_withexternal_medialaxis_used) {
            //in this case, part of the work was already done in the first phase
            res->unitePaths(output.contours, infillings_medialAxisIndependentContours, output.contours_withexternal_medialaxis);
        } else {
            //in this case, all of the work must be done now
            res->unitePaths(output.contours, infillings_medialAxisIndependentContours, output.medialAxisIndependentContours, output.contours);
        }
        if (global.alsoContours) {
            COPYTO(output.contours, output.contoursToShow);
        }
        output.medialAxisIndependentContours.clear();
        infillings_medialAxisIndependentContours.clear();
    } else {
        /*in this case, artificial holes may be generated as artifacts
        (either from overprints if !nextProcessSameKind, or from non-filled
        interiors if !output.infillingsIndependentContours.empty()). The
        3D profile of these holes cannot be approximated by eroding them
        as for holes in contours.*/
        res->unitePaths(output.contours, output.contours);
        if (global.alsoContours) {
            /*COPYTO(output.contours,                          output.contoursToShow);
            COPYTO(infillings_medialAxisIndependentContours, output.contoursToShow);
            COPYTO(output.medialAxisIndependentContours,     output.contoursToShow);
            COPYTO(output.infillingsIndependentContours,     output.contoursToShow);*/
            if (output.contours_withexternal_medialaxis_used) {
                //in this case, part of the work was already done in the first phase
                res->unitePaths(output.contoursToShow, output.infillingsIndependentContours, infillings_medialAxisIndependentContours, output.contours_withexternal_medialaxis);
            } else {
                //in this case, all of the work must be done now
                res->unitePaths(output.contoursToShow, output.infillingsIndependentContours, infillings_medialAxisIndependentContours, output.contours, output.medialAxisIndependentContours);
            }
        }
        //put the contents of infillings_medialAxisIndependentContours where they are required by the 3d scheduling code: output.medialAxisIndependentContours
        if (!infillings_medialAxisIndependentContours.empty()) {
            MOVETO(infillings_medialAxisIndependentContours, output.medialAxisIndependentContours);
        }
    }

    if (ppspec.computeToolpaths) {
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
        */
        if (!output.medialAxis_toolpaths.empty()) {
            MOVETO(output.medialAxis_toolpaths, output.ptoolpaths);
            output.medialAxis_toolpaths.clear();
        }

        bool dolump = ppspec.lumpToolpathsTogether;

        if (!accumInfillingsHolder.empty()) {
            if (dolump) {
                MOVETO(accumInfillingsHolder, output.ptoolpaths);
            } else {
                MOVETO(accumInfillingsHolder, output.itoolpaths);
            }
            accumInfillingsHolder.clear();
        }
        if (!intermediate_medialaxis_infilling->empty()) {
            if (dolump) {
                MOVETO(*intermediate_medialaxis_infilling, output.ptoolpaths);
            } else {
                MOVETO(*intermediate_medialaxis_infilling, output.itoolpaths);
            }
            intermediate_medialaxis_infilling->clear();
        }
        if (dolump) {
            output.itoolpaths.clear();
        }

        if (global.applyMotionPlanner) {
            //IMPORTANT: this must be the LAST step in the processing of the toolpaths and contours. Any furhter processing will ruin this
            if (!output.ptoolpaths.empty()) verySimpleMotionPlanner(spec->startState, PathOpen, output.ptoolpaths);
            if (!output.itoolpaths.empty()) verySimpleMotionPlanner(spec->startState, PathOpen, output.itoolpaths);
        }
    }

    this->clear();
    output.phase2complete = true;
    return true;
}

bool Multislicer::applyProcess(SingleProcessOutput &output, clp::Paths &contours_tofill, clp::Paths &contours_alreadyfilled, int k) {
    if (!applyProcessPhase1(output, contours_tofill, k)) return false;
    
    return applyProcessPhase2(output, contours_alreadyfilled, k);
}


int Multislicer::applyProcesses(std::vector<SingleProcessOutput*> &outputs, clp::Paths &contours_tofill, clp::Paths &contours_alreadyfilled, int kinit, int kend) {
    auto &spec   = res->spec;
    auto &global = spec->global;
    if (global.substractiveOuter) {
        addOuter(contours_tofill, global.limitX, global.limitY);
    }
    if (global.correct || global.substractiveOuter) {
        orientPaths(contours_tofill);
    }
    if (kinit < 0) kinit = 0;
    if (kend < 0) kend = (int)spec->numspecs - 1;
    ++kend;
    int k;
    for (k = kinit; k < kend; ++k) {
        if (spec->useContoursAlreadyFilled(k)) {
            res->clipper.AddPaths(contours_alreadyfilled, clp::ptSubject, true);
            if (k > kinit) {
                if (outputs[k - 1]->infillingsIndependentContours.empty()) {
                    res->clipper.AddPaths(outputs[k - 1]->contours, clp::ptSubject, true);
                } else {
                    res->AddPaths(outputs[k - 1]->infillingsIndependentContours, clp::ptSubject, true);
                }
                res->AddPaths(outputs[k - 1]->medialAxisIndependentContours, clp::ptSubject, true);
            }
            res->clipper.Execute(clp::ctUnion, contours_alreadyfilled, clp::pftNonZero, clp::pftNonZero);
            res->clipper.Clear();
            ClipperEndOperation(res->clipper);
        }
        //SHOWCONTOURS(*spec->global.config, "before_applying_process_1", &contours_tofill);
        //SHOWCONTOURS(*spec->global.config, "before_applying_process_2", &contours_alreadyfilled);
        bool ok = applyProcess(*(outputs[k]), contours_tofill, contours_alreadyfilled, k);
        //SHOWCONTOURS(*spec->global.config, str("after_applying_process ", k), &contours_tofill, &(outputs[k]->contours));
        if (!ok) break;
        if (global.substractiveOuter) {
            removeOuter(outputs[k]->ptoolpaths,         global.outerLimitX, global.outerLimitY);
            removeOuter(outputs[k]->itoolpaths,         global.outerLimitX, global.outerLimitY);
            if (outputs[k]->alsoInfillingAreas) {
                removeOuter(outputs[k]->infillingAreas, global.outerLimitX, global.outerLimitY);
            }
        }
        bool nextProcessSameKind = (!spec->global.addsub.addsubWorkflowMode) || (k > 0);
        if (nextProcessSameKind) {
            if (outputs[k]->infillingsIndependentContours.empty()) {
                res->clipperDo(contours_tofill, clp::ctDifference, contours_tofill, outputs[k]->contours, clp::pftNonZero, clp::pftNonZero);
            } else {
                res->AddPaths(contours_tofill, clp::ptSubject, true);
                res->AddPaths(outputs[k]->infillingsIndependentContours, clp::ptClip, true);
                res->AddPaths(outputs[k]->medialAxisIndependentContours, clp::ptClip, true);
                res->clipper.Execute(clp::ctDifference, contours_tofill, clp::pftNonZero, clp::pftNonZero);
                res->clipper.Clear();
                ClipperEndOperation(res->clipper);
                //SHOWCONTOURS(*spec->global.config, "after_applying_infillings_1", &(contours_tofill));
            }
        } else {
            //clp::Paths MYAUX = contours_tofill;
            res->AddPaths(outputs[k]->contours, clp::ptSubject, true);
            res->AddPaths(outputs[k]->medialAxisIndependentContours, clp::ptSubject, true);
            res->AddPaths(contours_tofill, clp::ptClip, true);
            res->clipper.Execute(clp::ctDifference, contours_tofill, clp::pftNonZero, clp::pftNonZero);
            res->clipper.Clear();
            ClipperEndOperation(res->clipper);
            //SHOWCONTOURS(*spec->global.config, "output contours", &MYAUX, &outputs[k]->contours, &contours_tofill);
            //clipperDo(clipper, contours_tofill, clp::ctDifference, outputs[k]->contours, contours_tofill, clp::pftEvenOdd, clp::pftEvenOdd);
            //SHOWCONTOURS(*spec->global.config, "after_addsub_switch", &MYAUX, &outputs[k]->contours, &contours_tofill);// , &contours_alreadyfilled);
        }
    }
    return k;
}

