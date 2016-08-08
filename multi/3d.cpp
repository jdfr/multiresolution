#include "3d.hpp"
#include "orientPaths.hpp"
#include "pathsfile.hpp"
#include "slicermanager.hpp"
#include "showcontours.hpp"
#include <numeric>

//if this is too heavy (I doubt it), it can be merged into loops where it makes sense
void ToolpathManager::removeUsedSlicesPastZ(double z, std::vector<OutputSliceData> &output) {
    //TODO: decide how to remove additive contours if they are unrequired because feedback has been given with takeAdditionalAdditiveContours()
    bool sliceUpwards = spec->global.sliceUpwards;
    for (auto slices = slicess.begin(); slices != slicess.end(); ++slices) {
        slices->erase(std::remove_if(slices->begin(), slices->end(),
            [&output, z, sliceUpwards](std::shared_ptr<ResultSingleTool> &sz) {
                bool toremove = sz->used && (output[sz->idx].numSlicesRequiringThisOne == 0) && (sliceUpwards ? (sz->z < z) : (sz->z > z));
                if (toremove) {
                    output[sz->idx].result = NULL;
                }
                return toremove;
            }
        ), slices->end());
    }
}

void ToolpathManager::removeAdditionalContoursPastZ(double z) {
    bool sliceUpwards = spec->global.sliceUpwards;
    for (auto additional = additionalAdditiveContours.begin(); additional != additionalAdditiveContours.end();) {
        bool erase = sliceUpwards ? (additional->first < z) : (additional->first > z);
        if (erase) {
            additionalAdditiveContours.erase(additional++);
        } else {
            ++additional;
        }
    }
}

void ToolpathManager::applyContours(std::vector<clp::Paths> &contourss, int k, bool processIsAdditive, bool computeContoursAlreadyFilled, double diffwidth) {
    for (auto contours = contourss.begin(); contours != contourss.end(); ++contours) {
        if (!contours->empty()) applyContours(*contours, k, processIsAdditive, computeContoursAlreadyFilled, diffwidth);
    }
}

//this function is the body of the inner loop in updateInputWithProfilesFromPreviousSlices(), parametrized in the contour
void ToolpathManager::applyContours(clp::Paths &contours, int ntool_contour, bool processToComputeIsAdditive, bool computeContoursAlreadyFilled, double diffwidth) {

    if (diffwidth == 0.0) {
        auxUpdate.clear();
        COPYTO(contours, auxUpdate);
    } else {
        res->offsetDo(auxUpdate, -diffwidth, contours, clp::jtRound, clp::etClosedPolygon);
    }
    if (auxUpdate.empty()) return;
    if (spec->global.addsub.addsubWorkflowMode) {
        //here, computeContoursAlreadyFilled will always be false
        if (ntool_contour == 0) {
            //contour auxUpdate is additive
            res->clipper.AddPaths(auxUpdate, processToComputeIsAdditive ? clp::ptClip : clp::ptSubject, true);
        } else {
            //contour auxUpdate is subtractive
            if (!processToComputeIsAdditive) res->clipper.AddPaths(auxUpdate, clp::ptClip, true);
            // processToComputeIsAdditive should never be true here, because of the current way we structure add/sub processes (first 'add', all subsequent 'sub')
        }
    } else {
        //contour auxUpdate is additive
        if (processToComputeIsAdditive) res->clipper.AddPaths(auxUpdate, clp::ptClip, true);
        // processToComputeIsAdditive should always be true here
        if (computeContoursAlreadyFilled) res->clipper2.AddPaths(auxUpdate, clp::ptSubject, true);
    }
    //SHOWCONTOURS(*spec->global.config, "deflating_contour", &contours, &auxUpdate);
}

//remove from the input contours the parts that are already there from previous slices
void ToolpathManager::updateInputWithProfilesFromPreviousSlices(clp::Paths &initialContour, clp::Paths &contours_alreadyfilled, clp::Paths &rawSlice, double z, int ntool) {

    bool processToComputeIsAdditive = !spec->global.addsub.addsubWorkflowMode || ntool == 0;
    bool computeContoursAlreadyFilled = spec->useContoursAlreadyFilled(ntool);

    /*the following logic for getting parts to add and parts to substract hinges on
    the multislicer using always additive processes as default (and substractive for k>0
    if the flag spec->global.addsubWorkflowMode is set). Of course, it should be fine to
    pretend that the software is valid also if we invert all additive/subtractive processes*/
    //for subtractive process: initialContour <- previously_computed_additive_contours - rawSlice - previously_computed_subtractive_contours
    //for additive process:    initialContour <- rawSlice - previously_computed_additive_contours

    bool doNotUseStoredAdditiveContours = false;
    bool appliedAdditional = false;

    //process additional additive contours: for applyContours() logic to take them as additive, we earmark them as coming from ntool=0
    //OF COURSE, THIS WORKS ONLY AS LONG AS WE HAVE THE CONVENTION THAT add/sub PROCESSES ARE ADDITIVE FOR ntool=0 AND SUBTRACTIVE FOR ntool>0
    for (auto & additional : additionalAdditiveContours) {
        if (std::fabs(additional.first - z) < spec->global.z_epsilon) {
            if (appliedAdditional) {
                printf("WARNING: tried to apply more than one instance of additional additive contours for raw slice at z=%f, for tool=%d!\n", z, ntool);
            } else {
                applyContours(additional.second, 0, processToComputeIsAdditive, computeContoursAlreadyFilled, 0.0);
                //we have received feedback for additional contours.
                //If flag ignoreRedundantAdditiveContours is true, ignore stored additive contours
                doNotUseStoredAdditiveContours = spec->global.addsub.ignoreRedundantAdditiveContours;
                appliedAdditional = true;
            }
        }
    }
    if ((!additionalAdditiveContours.empty()) && (!appliedAdditional)) {
        printf("WARNING: there are additional additive contours, but none was applied for raw slice at z=%f, for tool=%d!\n", z, ntool);
    }

    //process previously computed contours
    for (int ntool_contour = 0; ntool_contour < spec->numspecs; ++ntool_contour) {
        
        //do not use any stored additive contour if we have received feedback
        if (doNotUseStoredAdditiveContours) {
            bool contourIsAdditive = !spec->global.addsub.addsubWorkflowMode || ntool_contour == 0;
            if (contourIsAdditive) continue;
        }

        res->offset.ArcTolerance = (double)spec->pp[ntool_contour].arctolG;
        for (auto slice = slicess[ntool_contour].begin(); slice != slicess[ntool_contour].end(); ++slice) {
            if (!(*slice)->contours.empty()) { 
                double currentWidth = spec->pp[ntool_contour].profile->getWidth(z - (*slice)->z);
                if (currentWidth > 0) {
                    double diffwidth = spec->pp[ntool_contour].radius - currentWidth;
                    if ((*slice)->infillingsIndependentContours.empty()) {
                        //if infilling contours were not generated, we make do with the contours, which are actually cheaper to handle!
                        auto &contours = (*slice)->contours;
                        if (!contours.empty()) applyContours(contours, ntool_contour, processToComputeIsAdditive, computeContoursAlreadyFilled, diffwidth);
                    } else {
                        //OK, this previous slice was meant to have recursive infilling, so we have to handle the infillings!
                        applyContours((*slice)->infillingsIndependentContours, ntool_contour, processToComputeIsAdditive, computeContoursAlreadyFilled, diffwidth);
                    }
                    applyContours((*slice)->medialAxisIndependentContours, ntool_contour, processToComputeIsAdditive, computeContoursAlreadyFilled, diffwidth);
                }
            }
        }
    }
    
    bool ensureAttachment = spec->pp[ntool].ensureAttachmentOffset != 0;
    
    if (ensureAttachment) {
        res->clipper.Execute(clp::ctUnion, auxUpdate, clp::pftNonZero, clp::pftNonZero);
        ensureAttachment = !auxUpdate.empty();
    }

    //process the raw slice
    res->clipper.AddPaths(rawSlice, processToComputeIsAdditive ? clp::ptSubject : clp::ptClip, true);
    
    //apply operations
    res->clipper.Execute(clp::ctDifference, initialContour, clp::pftNonZero, clp::pftNonZero); //clp::pftEvenOdd, clp::pftEvenOdd);
    res->clipper.Clear();
    ClipperEndOperation(res->clipper);
    if (computeContoursAlreadyFilled) {
        res->clipper2.Execute(clp::ctUnion, contours_alreadyfilled, clp::pftNonZero, clp::pftNonZero); //clp::pftEvenOdd, clp::pftEvenOdd);
        res->clipper2.Clear();
        ClipperEndOperation(res->clipper2);
    }
    //SHOWCONTOURS(*spec->global.config, "after_updating_initial_contour", &rawSlice, &initialContour);
    
    //apply --ensure-attachment-offset
    if (ensureAttachment && !initialContour.empty()) {
        clp::cInt attachmentOffset = spec->pp[ntool].ensureAttachmentOffset;
        double offset;
        //TODO: the following logic/values are extracted from ClippingResources::generateToolPath(); extract them to a submethod or field in spec->pp[ntool] and use them from both here and that method
        if (spec->pp[ntool].computeToolpaths) {
            if (spec->pp[ntool].applysnap) {
                offset = (double)spec->pp[ntool].safestep;
            } else if (spec->pp[ntool].addInternalClearance) {
                offset = (double)spec->pp[ntool].radius;
            } else {
                offset = (double)spec->pp[ntool].burrLength;
            }
            //offset += (double)spec->pp[ntool].radius;
            res->offsetDo(auxEnsure, -offset, initialContour, clp::jtRound, clp::etClosedPolygon);
            //do not go on if all contours will vanish later, because this treatment will make them larger, so they will not
            if (!auxEnsure.empty()) {
                if (spec->pp[ntool].ensureAttachmentUseMinimalOffset) {
                    double moffset = spec->pp[ntool].ensureAttachmentMinimalOffset;
                    //this is to remove long and narrow artifacts
                    res->offsetDo(auxEnsure,     -moffset/100, initialContour, clp::jtRound, clp::etClosedPolygon);
                    res->offsetDo(initialContour, moffset/100, auxEnsure,      clp::jtRound, clp::etClosedPolygon);
                    //remove small artifacts from the contour (but uses only dissapearance under negative offset)
                    HoledPolygons hps, result;
                    AddPathsToHPs(res->clipper, initialContour, hps);
                    auto &offseter = res->offset;
                    erase_remove_idiom(hps, [&result, &offseter, moffset](HoledPolygon &hp){
                        hp.offset(offseter, -moffset, result);
                        return result.empty();
                    });
                    initialContour.clear();
                    AddHPsToPaths(hps, initialContour);
                }
                
                if (!initialContour.empty()) {
                    //inflate the initial contour to overlap the already present contours
                    res->offsetDo(auxEnsure, (double)attachmentOffset, initialContour, clp::jtRound, clp::etClosedPolygon);
                    //intersect with the already present contours to compute the overlapping
                    res->clipperDo(auxEnsure, clp::ctIntersection, auxEnsure, auxUpdate, clp::pftNonZero, clp::pftNonZero);
                    //inflate slightly the overlapping to avoid it being disconnected from the initial contour
                    res->offsetDo(auxUpdate, (double)attachmentOffset / 100, auxEnsure, clp::jtMiter, clp::etClosedPolygon);
                    //fuse the overlapping with the initial contour
                    res->clipperDo(initialContour, clp::ctUnion, initialContour, auxUpdate, clp::pftNonZero, clp::pftNonZero);
                }
            }
        }
        auxUpdate.clear();
        auxEnsure.clear();
    }
}

void ToolpathManager::removeFromContourSegmentsWithoutSupport(clp::Paths &contour, ResultSingleTool &output, std::vector<ResultSingleTool*> &requiredContours) {
    computeContoursAboveAndBelow(output, requiredContours, false);
    bool sliceUpwards = spec->global.sliceUpwards;
    clp::Paths *support = sliceUpwards ? &output.contoursBelow : &output.contoursAbove;
    clp::Paths localSupport;
    if (support->empty()) {
        contour.clear();
    } else {
        bool doOffset = spec->pp[output.ntool].supportOffset!=0;
        double supportOffset = (double)spec->pp[output.ntool].supportOffset;
        clp::Paths segment;
        if (doOffset) {
            res->offsetDo(localSupport, supportOffset, *support, clp::jtRound, clp::etClosedPolygon);
            support = &localSupport;
        }
        HoledPolygons hps;
        int initialSize = (int)hps.size();
        AddPathsToHPs(res->clipper, contour, hps);
        erase_remove_idiom(hps, [this, &segment, support, doOffset, supportOffset](HoledPolygon &hp){
                segment.clear();
                if (doOffset) {
                    hp.offset(res->offset, supportOffset, segment);
                } else {
                    hp.copyToPaths(segment);
                }
                res->clipperDo(segment, clp::ctIntersection, *support, segment, clp::pftNonZero, clp::pftNonZero);
                return segment.empty();
            });
        if (hps.size()!=initialSize)  {
            contour.clear();
            AddHPsToPaths(hps, contour);
        }
    }
}


/*IMPORTANT: right now, the following method should work right only if the slices are weakly ordered. The calling order should be:
   -while there are slices to be computed:
      -ask for a slice (implicitly, it should contain all processes)
      -ask for all slices within the range of the lowest process for that slice
      -do that recursively for each process, from lowest to highest
*/
bool ToolpathManager::processSlicePhase1(std::vector<ResultSingleTool*> &requiredContours, clp::Paths &rawSlice, double z, int ntool, int output_index, ResultSingleTool *&result) {
    slicess[ntool].push_back(std::make_shared<ResultSingleTool>(z, ntool, output_index));
    ResultSingleTool &output = *(slicess[ntool].back());

    updateInputWithProfilesFromPreviousSlices(auxInitial, output.contours_alreadyfilled, rawSlice, z, ntool);
    
    if (!requiredContours.empty()) {
        removeFromContourSegmentsWithoutSupport(auxInitial, output, requiredContours);
    }

    //TODO: intilialize properly the StartPosition of the MultiSpec

    bool ret = multi.applyProcessPhase1(output, auxInitial, ntool);
    auxInitial.clear();
    output.has_err = !output.err.empty();
    if (!ret) {
        err = str("error in processSlicePhase1() at height z= ", z, ", process ", ntool, ": ", output.err, "output_idx=", output_index, "\n");
        slicess.pop_back();
        return ret;
    }
    result = &output;
    return ret;
}

/* Compute contours above and below, *only* if not computed yet.
 * This method is intended to be used two times, re-evaluating above/below ONLY if necessary.
 * However, for this memoization to be the CORRECT behavior, the way to use it is:
 *        - first call:  requiredContours must contain ONLY contours either above or below. Let this set of contours be F
 *        - second call: requiredContours must contain contours BOTH above and below. Let this set of contours be S,
 *                       with {Sa, Sb} being a partition of S such that Sa only contains the contours above, and Sb the contours below.
 *                       Then, for the calls to be correct, the following must be true: (F==Sa) || (F==Sb)
*/
bool ToolpathManager::computeContoursAboveAndBelow(ResultSingleTool &output, std::vector<ResultSingleTool*> &requiredContours, bool onlyIfBothAboveAndBelow) {
    bool hasAbove = false, hasBelow = false;
    for (auto required : requiredContours) {
        if (required->z < output.z) {
            hasBelow = true; if (hasAbove) break;
        } else {
            hasAbove = true; if (hasBelow) break;
        }
    }
    if (onlyIfBothAboveAndBelow && !(hasAbove && hasBelow)) {
        return false;
    }
    if ((output.contoursBelowAlreadyComputed || (!hasBelow)) && (output.contoursAboveAlreadyComputed || (!hasAbove))) {
        return true;
    }
    for (auto required : requiredContours) {
        clp::Clipper *clipper;
        if (required->z < output.z) {
            if (output.contoursBelowAlreadyComputed) continue;
            clipper = &res->clipper;
        } else {
            if (output.contoursAboveAlreadyComputed) continue;
            clipper = &res->clipper2;
        }
        clipper->AddPaths(required->contours_withexternal_medialaxis_used ? required->contours_withexternal_medialaxis : required->contours, clp::ptSubject, true);
    }
    if (hasBelow && !output.contoursBelowAlreadyComputed) {
        output.contoursBelowAlreadyComputed = true;
        res->clipper.Execute(clp::ctUnion, output.contoursBelow, clp::pftNonZero, clp::pftNonZero);
    }
    res->clipper.Clear();
    ClipperEndOperation(res->clipper);
    if (hasAbove && !output.contoursAboveAlreadyComputed) {
        output.contoursAboveAlreadyComputed = true;
        res->clipper2.Execute(clp::ctUnion, output.contoursAbove, clp::pftNonZero, clp::pftNonZero);
    }
    res->clipper2.Clear();
    ClipperEndOperation(res->clipper2);
    return true;
}

void clearContoursAboveBelow(ResultSingleTool &output) {
    //this resets the memoization logic in computeContoursAboveAndBelow()
    output.contoursAboveAlreadyComputed = false;
    output.contoursBelowAlreadyComputed = false;
    output.contoursAbove.clear();
    output.contoursBelow.clear();
}

bool ToolpathManager::processSlicePhase2(ResultSingleTool &output, std::vector<ResultSingleTool*> requiredContoursOverhang, std::vector<ResultSingleTool*> requiredContoursSurface, bool recomputeRequiredAfterOverhang) {
    clp::Paths *internalParts = NULL;
    clp::Paths *support       = NULL;
    if (!requiredContoursOverhang.empty()) {
        computeContoursAboveAndBelow(output, requiredContoursOverhang, false);
        support = spec->global.sliceUpwards ? &output.contoursBelow : &output.contoursAbove;
        double overhangOffset = (double)spec->pp[output.ntool].overhangOffset;
        if (overhangOffset!=0.0) {
            res->offsetDo(auxEnsure, overhangOffset, *support, clp::jtRound, clp::etClosedPolygon);
            support = &auxEnsure;
        }
        
    }
    if (recomputeRequiredAfterOverhang) {
        if ((support!=NULL) && (support!=&auxEnsure)) {
            auxEnsure = std::move(*support);
            support   = &auxEnsure;
        }
        clearContoursAboveBelow(output);
    }
    if (!requiredContoursSurface.empty()) {
        bool haveBeenComputed = computeContoursAboveAndBelow(output, requiredContoursSurface, true);
        if (haveBeenComputed) {
            res->clipperDo(auxInitial, clp::ctIntersection, output.contoursAbove, output.contoursBelow, clp::pftNonZero, clp::pftNonZero);
            //output.contoursAbove = clp::Paths();
            //output.contoursBelow = clp::Paths();
        } else {
            auxInitial.clear();
        }
        internalParts = &auxInitial;
    }
    bool ret = multi.applyProcessPhase2(output, internalParts, support, output.contours_alreadyfilled, output.ntool);
    clearContoursAboveBelow(output);
    auxInitial.clear();
    auxEnsure.clear();
    output.contours_alreadyfilled = clp::Paths();
    output.has_err = !output.err.empty();
    if (!ret) {
        err = str("error in processSlicePhase2() at height z= ", output.z, ", process ", output.ntool, ": ", output.err, "output_idx=", output.idx, "\n");
        slicess.pop_back();
        return ret;
    }
    if (spec->global.substractiveOuter) {
        removeOuter(output.ptoolpaths,         spec->global.outerLimitX, spec->global.outerLimitY);
        removeOuter(output.stoolpaths,         spec->global.outerLimitX, spec->global.outerLimitY);
        removeOuter(output.itoolpaths,         spec->global.outerLimitX, spec->global.outerLimitY);
        if (output.alsoInfillingAreas) {
            removeOuter(output.infillingAreas, spec->global.outerLimitX, spec->global.outerLimitY);
        }
    }
    return ret;
}

void ToolpathManager::serialize_custom(FILE *f) {
    int numspecs = (int)spec->numspecs;
    serialize(f, numspecs);
    for (int k=0; k<numspecs; ++k) {
        serialize(f, spec->pp[k].internalInfilling.infillingAlternate);
        serialize(f, spec->pp[k].surfaceInfilling .infillingAlternate);
    }
}
void ToolpathManager::deserialize_custom(FILE *f) {
    int numspecs;
    deserialize(f, numspecs);
    if (numspecs != spec->numspecs) {
        throw std::runtime_error(str("number of tools from configuration (", spec->numspecs, ") does not match number of tools from the serialized state(", numspecs, ")!!!"));
    }
    for (int k=0; k<numspecs; ++k) {
        deserialize(f, spec->pp[k].internalInfilling.infillingAlternate);
        deserialize(f, spec->pp[k].surfaceInfilling .infillingAlternate);
    }
}


void RawSlicesManager::removeUsedRawSlices() {
    for (int k = 0; k < raw.size(); ++k) {
        if (raw[k].inUse) {
            if (raw[k].numRemainingUses < 0) {
                printf("WARNING: raw slice %d at z=%g has been apparently used more times than it was expected\n", k, raw[k].z);
                continue;
            }
            if (raw[k].numRemainingUses == 0) {
                raw[k].slice = clp::Paths(); //completely free memory (clear won't cut it!)
                raw[k].inUse = false;
            }
        }
    }
}


void SimpleSlicingScheduler::createSlicingSchedule(double minz, double maxz, double epsilon, SchedulingMode mode) {
    clear();
    zmin = minz;
    zmax = maxz;
    switch (mode) {
    case ScheduleLaserSimple:
        throw std::runtime_error("option ScheduleLaserSimple not implemented!!!!");
        break;
    case ScheduleTwoPhotonSimple:
        if (tm.spec->global.schedMode==ManualScheduling) {
            input.reserve(tm.spec->global.schedSpec.size());
            for (auto pair = tm.spec->global.schedSpec.begin(); pair != tm.spec->global.schedSpec.end(); ++pair) {
                input.push_back(InputSliceData(pair->z, pair->ntool));
            }
        } else {
            bool sliceUpwards = tm.spec->global.sliceUpwards;
            double extent = maxz - minz;
            double base;
            if (tm.spec->global.use_z_base) {
                base = tm.spec->global.z_base;
            } else {
                base = sliceUpwards ? minz : maxz;
            }
            int num = 0;
            for (int k = 0; k < tm.spec->numspecs; ++k) num += (int)(extent / tm.spec->pp[k].profile->sliceHeight) + 3;
            input.reserve(num);
            std::vector<double> zbase(tm.spec->numspecs);
            if (sliceUpwards) {
                for (int i = 0; i < tm.spec->numspecs; ++i) zbase[i] = base + tm.spec->pp[i].profile->applicationPoint;
            } else {
                for (int i = 0; i < tm.spec->numspecs; ++i) zbase[i] = base - tm.spec->pp[i].profile->remainder;
            }
            recursiveSimpleInputScheduler(0, zbase, sliceUpwards ? maxz : minz);
        }
        if (!input.empty()) {
            computeSimpleOutputOrderForInputSlices();
            pruneInputZsAndCreateRawZs(epsilon);
        }
    }
}

bool testSliceNotNearEnd(double z, double zend, int process, ToolpathManager &tm) {
    double zspan = (tm.spec->global.sliceUpwards) ?
        (zend - z - tm.spec->pp[process].profile->remainder) :
        (z - zend - tm.spec->pp[process].profile->applicationPoint);
    //-0.2 to give some slack and not discard slices that protude slightly
    return (zspan >= 0) || (zspan >= tm.spec->pp[process].profile->sliceHeight*-0.2);
}

/*this is adequate for additive processes, as it assumes that voxels are symmetric over the Z axis.
It defines an ordering for the of Z-slices such as the use of large-voxel processes is maximized,
in the following way: first, Z-slices at Z-distances that enable the use of large voxels are processed,
then are processed Z-slices in their neighbourhood, which will not be able to accomodate large voxels.*/
//THIS VERSION GENERATES A MODIFIED IN-PLACE VERSION WHERE AT LEAST TWO HIGHER ORDER SECTIONS ARE COMPUTED *BEFORE* THE NEXT ORDER

void SimpleSlicingScheduler::recursiveSimpleInputScheduler(int process_spec, std::vector<double> &zbase, double zend) {
    //ATTENTION: THIS RECURSIVE PROCEDURE CAN GENERATE SLICES THAT ARE AT IDENTICAL OR NEAR IDENTICAL Zs
    //TO AVOID UNNECESARY REPETITIVE SLICING, WE FIX IT IN THE ORDERING STAGE. OF COURSE, THAT DEPENDS CRUCIALLY ON THE ORDERING!

    int process;
    bool nextpok;
    int nextp          = process_spec + 1;
    bool sliceUpwards  = tm.spec->global.sliceUpwards;
    if (tm.spec->global.schedTools.empty()) {
        process        = process_spec;
        nextpok        = nextp < tm.spec->numspecs;
    } else {
        process        = tm.spec->global.schedTools[process_spec];
        nextpok        = nextp < tm.spec->global.schedTools.size();
    }

    double sliceHeight = tm.spec->pp[process].profile->sliceHeight;
    if (!sliceUpwards) {
           sliceHeight = -sliceHeight;
    }
    double z           = zbase[process];

    if (testSliceNotNearEnd(z, zend, process, tm)) {
        input.push_back(InputSliceData(z, process));
        zbase[process] += sliceHeight;
    }

    while (testSliceNotNearEnd(z += sliceHeight, zend, process, tm)) {
        input.push_back(InputSliceData(z, process));
        if (nextpok) {
            double next_zend = sliceUpwards ?
                std::min(zbase[process], zend) :
                std::max(zbase[process], zend);
            recursiveSimpleInputScheduler(nextp, zbase, next_zend);
        }
        zbase[process] += sliceHeight;
    }

    //add possible high res slices higher than the place where low res slices cannot be placed
    if (nextpok) {
        recursiveSimpleInputScheduler(nextp, zbase, zend);
    }

}

void SimpleSlicingScheduler::pruneInputZsAndCreateRawZs(double epsilon) {
    int numraw = (int)input.size();
    //we generate RawSliceInfos from InputSliceInfos, but some input slices are redundant, so we will use only some of them
    std::vector<bool> useinraw(numraw, true);
    //it is easier to compute the values of raw.numRemainingUses in output order
    std::vector<int> remainingUsesRaw_zs_output_order;
    remainingUsesRaw_zs_output_order.reserve(numraw); //reserve in excess, to avoid reallocations
    remainingUsesRaw_zs_output_order.push_back(1);
    //this is a map from input slices to values in remainingUsesRaw_zs_output_order
    std::vector<int> initialMapInputToRaw(numraw);
    //WARNING: this code relies on output to be sorted by z!!!!!
    //TODO: right now, epsilon is absolute. Does it make sense to use a value relative to the heights
    //of the slices? May be useful for multislicings with great differences in voxel size, but maybe
    //confusing to implement correctly...
    initialMapInputToRaw[output[0].mapOutputToInput] = (int)remainingUsesRaw_zs_output_order.size() - 1;
    for (int k = 1; k < output.size(); ++k) {
        double df = std::abs(output[k].z - output[k - 1].z);
        if (df <= epsilon) {
            input[output[k].mapOutputToInput].z = output[k].z = output[k - 1].z;
            useinraw[output[k].mapOutputToInput] = false;
            --numraw;
            ++remainingUsesRaw_zs_output_order.back();
        } else {
            remainingUsesRaw_zs_output_order.push_back(1);
        }
        initialMapInputToRaw[output[k].mapOutputToInput] = (int)remainingUsesRaw_zs_output_order.size() - 1;
    }
    //now we fill raw data in the same order as input data (only that some input data is repeated, so it is not used)
    rm.raw.resize(numraw);
    rm.rawZs.resize(numraw);
    //raw_idx_in_zs_output_order are the indexes of raw slices in the same order as remainingUsesRaw_zs_output_order
    std::vector<int> raw_idx_in_zs_output_order(numraw);
    int kraw = 0;
    //first pass: slices with flag useinraw set to true
    for (int k = 0; k < input.size(); ++k) {
        if (useinraw[k]) {
            rm.raw[kraw].z = rm.rawZs[kraw] = input[k].z;
            //if tm.spec->global.avoidVerticalOverwriting is set, numRemainingUses has to be computed in conjunction with filling requiredRawSlices
            rm.raw[kraw].numRemainingUses = (tm.spec->global.avoidVerticalOverwriting) ? 0 : remainingUsesRaw_zs_output_order[initialMapInputToRaw[k]];
            rm.raw[kraw].inUse = false;
            rm.raw[kraw].wasUsed = false;
            rm.raw[kraw].slice.clear();
            rm.raw[kraw].mapRawToInput.clear();
            rm.raw[kraw].mapRawToInput.reserve(remainingUsesRaw_zs_output_order[initialMapInputToRaw[k]]);
            rm.raw[kraw].mapRawToInput.push_back(k);
            input[k].mapInputToRaw = kraw;
            raw_idx_in_zs_output_order[initialMapInputToRaw[k]] = kraw;
            ++kraw;
        }
    }
    //second pass: slices with flag useinraw set to false (cannot do in the previous loop to avoid trying to use values in raw_idx_in_zs_output_order before they are set
    for (int k = 0; k < input.size(); ++k) {
        if (!useinraw[k]) {
            kraw = raw_idx_in_zs_output_order[initialMapInputToRaw[k]];
            input[k].mapInputToRaw = kraw;
            rm.raw[kraw].mapRawToInput.push_back(k);
        }
    }

    if (tm.spec->global.avoidVerticalOverwriting) {
        //TODO: reserve space for each input.requiredRawSlices, to avoid memory fragmentation!!!!
        for (int k = 0; k < input.size(); ++k) {
            int ntool = input[k].ntool;
            double inputz = input[k].z;
            double minz = inputz - tm.spec->pp[ntool].profile->applicationPoint;
            double maxz = inputz + tm.spec->pp[ntool].profile->remainder;
            for (int m = 0; m < rm.raw.size(); ++m) {
                double z = rm.raw[m].z;
                if ((z >= minz) && z<=maxz) {
                    //input[k].requiredRawSlices.push_back(m);
                    /*we add the m-th raw slice to the set of required raw slices of the k-th input slice IF:
                            -EITHER THE m-th RAW SLICE IS THE ONE ASSOCIATED TO input[k]
                            -OR THE RAW SLICE IS ASSOCIATED TO ANOTHER INPUT SLICE WITH A HIGHER RES TOOL
                    otherwise, the requirement would be commutative for all nearby slices,
                    even if they will clearly not mean an influence for the k-th input slice*/
                    auto &inputs = rm.raw[m].mapRawToInput;
                    if ((input[k].mapInputToRaw == m) || std::any_of(inputs.begin(), inputs.end(), [ntool, this](int input_idx) {return this->input[input_idx].ntool > ntool; })) {
                        input[k].requiredRawSlices.push_back(m);
                        ++rm.raw[m].numRemainingUses;
                    }
                }
            }
        }
    }
}


//creates a very simple output order: each Z slice in order, and inside the same slice, low res processes first.
void SimpleSlicingScheduler::computeSimpleOutputOrderForInputSlices() {
    //get indexes for sorting inputs by Z
    output_idx = 0;
    num_output_by_tool.resize(tm.spec->numspecs, 0);
    output.resize(input.size());
    std::vector<int> order_to_output(input.size());
    std::iota(order_to_output.begin(), order_to_output.end(), 0);
    auto comparator = [this](int a, int b) { double df = input[a].z - input[b].z; return (df < 0.0) || ((df == 0) && (input[a].ntool < input[b].ntool)); };
    bool sliceUpwards = tm.spec->global.sliceUpwards;
    if (sliceUpwards) {
        std::sort(order_to_output. begin(), order_to_output. end(), comparator);
    } else {
        std::sort(order_to_output.rbegin(), order_to_output.rend(), comparator);
    }
    for (int i = 0; i < order_to_output.size(); ++i) {
        int ii = order_to_output[i];
        output[i].computed = false;
        output[i].mapOutputToInput = ii;
        output[i].z = input[ii].z;
        output[i].ntool = input[ii].ntool;
        output[i].numSlicesRequiringThisOne = 0;
        output[i].recomputeRequiredAfterSupport  = false;
        output[i].recomputeRequiredAfterOverhang = false;
        input[ii].mapInputToOutput = i;
        ++num_output_by_tool[output[i].ntool];
    }
    if (tm.spec->global.anyDifferentiateSurfaceInfillings || tm.spec->global.anyAlwaysSupported || tm.spec->global.anyOverhangAlwaysSupported) {
        typedef struct MinMax { double min, max, extent; MinMax(double mn, double mx) : min(mn), max(mx), extent(mx-mn) {} } MinMax;
        std::vector<MinMax> minmaxzs;
        size_t numouts = output.size();
        minmaxzs.reserve(numouts);
        for (auto &out : output) {
            int ntool   = out.ntool;
            double minz = out.z - tm.spec->pp[ntool].profile->applicationPoint;
            double maxz = out.z + tm.spec->pp[ntool].profile->remainder;
            minmaxzs.emplace_back(minz, maxz);
        }
        //helpers to parametrize subloop direction (either downwards or upwards)
        int  for_inits[]      = {-1, +1};
        bool loop_is_upwards;
        auto loop_termination = [&loop_is_upwards, numouts](int  idx) { return loop_is_upwards ? idx < numouts : idx >= 0; };
        auto loop_next        = [&loop_is_upwards]         (int &idx) { if    (loop_is_upwards) { ++idx; } else { --idx; } };
        //main business logic common to boths subloops
        bool recordForSurfaces;
        bool recordForAlwaysSupported;
        bool recordForOverhangAlwaysSupported;
        bool record_for_reasons_1_and_2;
        bool record_for_reasons_2_and_3;
        bool record_for_reasons_1_and_3_but_not_2;
        bool recordForAnyReason;
        bool computeDifferentiationOnlyWithContoursFromSameTool;
        bool considerOverhangOnlyWithContoursFromSameTool;
        auto recordSlices = [this, &recordForSurfaces, &recordForAlwaysSupported, &recordForOverhangAlwaysSupported, &record_for_reasons_1_and_2, &record_for_reasons_2_and_3, &record_for_reasons_1_and_3_but_not_2, &computeDifferentiationOnlyWithContoursFromSameTool, &considerOverhangOnlyWithContoursFromSameTool]
                            (bool okSurface, bool okSupport, bool okOverhang, bool different_ntool, int k, int kk) {
            /* BUSINESS LOGIC: record slice kk as prerequisite for slice k in any combination of three possible prerequisites
             * 
             * ACTUAL COMPUTATION ORDER: 
             *     1. prerequisite requiredContoursForSupport  (either above or below, with same option as 2)
             *     2. prerequisite requiredContoursForOverhang (either above or below, with same option as 1)
             *     3. prerequisite requiredContoursForSurface  (both above and below)
             * So, recomputation flags are set up if there are discrepancies after 1 or 2.
             */
            bool recordSupport      = recordForAlwaysSupported         && okSupport  && !(                                                      different_ntool);
            bool recordOverhang     = recordForOverhangAlwaysSupported && okOverhang && !(      considerOverhangOnlyWithContoursFromSameTool && different_ntool);
            bool recordSurface      = recordForSurfaces                && okSurface  && !(computeDifferentiationOnlyWithContoursFromSameTool && different_ntool);
            
            //record if necessary for each prerequisite
            if (recordSurface) {
                output[k].requiredContoursForSurface.push_back(kk);
                ++output[kk].numSlicesRequiringThisOne;
            }
            if (recordOverhang) {
                output[k].requiredContoursForOverhang.push_back(kk);
                ++output[kk].numSlicesRequiringThisOne;
            }
            if (recordSupport) {
                output[k].requiredContoursForSupport.push_back(kk);
                ++output[kk].numSlicesRequiringThisOne;
            }
            
            //recomputation is required in cases where (a) more than one prerequisite is being supported and (b) requirements are different for each prerequisite
            if (!output[k].recomputeRequiredAfterSupport) {
                if (record_for_reasons_1_and_2) /*(recordForAlwaysSupported && recordForOverhangAlwaysSupported)*/ {
                    output[k].recomputeRequiredAfterSupport  = recordSupport != recordOverhang;
                }
            }
            if (!output[k].recomputeRequiredAfterOverhang) {
                if (record_for_reasons_2_and_3) /*(recordForOverhangAlwaysSupported && recordForSurfaces)*/ {
                    output[k].recomputeRequiredAfterOverhang = recordOverhang != recordSurface;
                }
                //the case where all reasons are simulateneously true is already covered by if statements for record_for_reasons_1_and_2 and record_for_reasons_2_and_3 
                if (record_for_reasons_1_and_3_but_not_2) /*(recordForAlwaysSupported && (!recordForOverhangAlwaysSupported) && recordForSurfaces)*/ {
                    output[k].recomputeRequiredAfterOverhang = recordSupport != recordSurface;
                }
            }
        };
        //main loop
        for (int k = 0; k < numouts; ++k) {
            int ntool = output[k].ntool;
            auto &ppspec = tm.spec->pp[ntool];
            computeDifferentiationOnlyWithContoursFromSameTool = ppspec.computeDifferentiationOnlyWithContoursFromSameTool;
            considerOverhangOnlyWithContoursFromSameTool       = ppspec.considerOverhangOnlyWithContoursFromSameTool;
            
            if (ppspec.differentiateSurfaceInfillings || ppspec.alwaysSupported || ppspec.overhangAlwaysSupported) {
                //add the slices above and below this slice: first slices below, then slices above
                //the code structure is almost identical for both cases (below and above), but it it has enough differences to make it quite
                //confusing if we want to refactor them as a single loop parameterized on some initial values and conditions. Instead, we use lambda recordSlices() for the common logic

                //this heuristic is good enough for now: we loop until we have covered a Z extent at least a little bigger than the required to consider slices of same tool just above/below.
                //TODO: take into account special cases when the heuristic may not be right
                double maxFac = 1.0;
                if (recordForSurfaces)                maxFac = std::max(maxFac, ppspec.differentiateSurfaceExtentFactor);
                if (recordForAlwaysSupported)         maxFac = std::max(maxFac, ppspec.alwaysSupportExtentFactor);
                if (recordForOverhangAlwaysSupported) maxFac = std::max(maxFac, ppspec.considerOverhangExtentFactor);
                maxFac += 0.1;
                
                /*ATTENTION: computeContoursAboveAndBelow() can be called up to 3 times. Its memoization logic works in the following way:
                               * if required, compute contour from set of slices in requiredContoursForSupport, and save the result
                               * if the sets requiredContoursForOverhang and requiredContoursForSupport are the same, reuse the result, otherwise recompute the contour and save it
                               * the set requiredContoursForSurface is partitioned in two subsets, one for slices below and one for slices above. If one of these coincides with recomputeRequiredAfterOverhang, it can be reused, otherwise all is recomputed
                             Flags recomputeRequired* are used to signal that recomputation is required*/
                
                //try to add slices below. When sliceUpwards is true, the subloop is downwards, and vice versa
                recordForSurfaces                    = ppspec.differentiateSurfaceInfillings;  //phase 2
                recordForAlwaysSupported             = sliceUpwards && ppspec.alwaysSupported; //phase 1
                recordForOverhangAlwaysSupported     = sliceUpwards && ppspec.overhangAlwaysSupported; //phase 2
                record_for_reasons_1_and_2           = recordForAlwaysSupported         & recordForOverhangAlwaysSupported;
                record_for_reasons_2_and_3           = recordForOverhangAlwaysSupported & recordForSurfaces;
                record_for_reasons_1_and_3_but_not_2 = recordForAlwaysSupported        && (!recordForOverhangAlwaysSupported) && recordForSurfaces;
                recordForAnyReason                   = recordForAlwaysSupported         | recordForOverhangAlwaysSupported     | recordForSurfaces;
                if (recordForAnyReason) {
                    double floorSurface  = minmaxzs[k].min - minmaxzs[k].extent * ppspec.differentiateSurfaceExtentFactor;
                    double floorSupport  = minmaxzs[k].min - minmaxzs[k].extent * ppspec.alwaysSupportExtentFactor;
                    double floorOverhang = minmaxzs[k].min - minmaxzs[k].extent * ppspec.considerOverhangExtentFactor;
                    double floorAbsolute = minmaxzs[k].min - minmaxzs[k].extent * maxFac;
                    loop_is_upwards     = !sliceUpwards;
                    for (int kk = k+for_inits[loop_is_upwards]; loop_termination(kk); loop_next(kk)) {
                        bool different_ntool = ntool!=output[kk].ntool;
                        //if such slices can be used, make sure that their Z extent does not overlap too much with our Z extent
                        if (different_ntool && ((minmaxzs[kk].max - minmaxzs[k].min) > minmaxzs[k].extent)) continue;
                        //keep adding slices while they are not too below us
                        bool okSurface  = minmaxzs[kk].max > floorSurface;
                        bool okSupport  = minmaxzs[kk].max > floorSupport;
                        bool okOverhang = minmaxzs[kk].max > floorOverhang;
                        recordSlices(okSurface, okSupport, okOverhang, different_ntool, k, kk);
                        if (!(minmaxzs[kk].max > floorAbsolute)) {
                            break;
                        }
                    }
                }
                
                //try to add slices above. When sliceUpwards is true, the subloop is upwards, and vice versa
                recordForSurfaces                    = ppspec.differentiateSurfaceInfillings;  //phase 2
                recordForAlwaysSupported             = (!sliceUpwards) && ppspec.alwaysSupported; //phase 1
                recordForOverhangAlwaysSupported     = (!sliceUpwards) && ppspec.overhangAlwaysSupported; //phase 2
                record_for_reasons_1_and_2           = recordForAlwaysSupported         & recordForOverhangAlwaysSupported;
                record_for_reasons_2_and_3           = recordForOverhangAlwaysSupported & recordForSurfaces;
                record_for_reasons_1_and_3_but_not_2 = recordForAlwaysSupported        && (!recordForOverhangAlwaysSupported) && recordForSurfaces;
                recordForAnyReason                   = recordForAlwaysSupported         | recordForOverhangAlwaysSupported     | recordForSurfaces;
                if (recordForAnyReason) {
                    double ceilSurface  = minmaxzs[k].max + minmaxzs[k].extent * ppspec.differentiateSurfaceExtentFactor;
                    double ceilSupport  = minmaxzs[k].max + minmaxzs[k].extent * ppspec.alwaysSupportExtentFactor;
                    double ceilOverhang = minmaxzs[k].max + minmaxzs[k].extent * ppspec.considerOverhangExtentFactor;
                    double ceilAbsolute = minmaxzs[k].min + minmaxzs[k].extent * maxFac;
                    loop_is_upwards = sliceUpwards;
                    for (int kk = k+for_inits[loop_is_upwards]; loop_termination(kk); loop_next(kk)) {
                        bool different_ntool = ntool!=output[kk].ntool;
                        //if such slices can be used, make sure that their Z extent does not overlap too much with our Z extent
                        if (different_ntool && ((minmaxzs[k].max - minmaxzs[kk].min) > minmaxzs[k].extent)) continue;
                        //keep adding slices while they are not too above us
                        bool okSurface  = minmaxzs[kk].min < ceilSurface;
                        bool okSupport  = minmaxzs[kk].min < ceilSupport;
                        bool okOverhang = minmaxzs[kk].min < ceilOverhang;
                        recordSlices(okSurface, okSupport, okOverhang, different_ntool, k, kk);
                        if (!(minmaxzs[kk].min < ceilAbsolute)) {
                            break;
                        }
                    }
                }
                
            }
        }
    }
}

//this method has to trust that the input slice will be according to the list of Z input values
void RawSlicesManager::receiveNextRawSlice(clp::Paths &input) {
    raw[raw_idx].wasUsed = raw[raw_idx].inUse = true;
    raw[raw_idx].slice = std::move(input);
    ++raw_idx;
    if (sched->tm.spec->global.substractiveOuter) {
        addOuter(raw[raw_idx].slice, sched->tm.spec->global.limitX, sched->tm.spec->global.limitY);
    }
    if (sched->tm.spec->global.correct || sched->tm.spec->global.substractiveOuter) {
        orientPaths(raw[raw_idx].slice);
    }
}

bool RawSlicesManager::singleRawSliceReady(int raw_idx, int input_idx) {
    //catch errors before the scheduler's state gets mangled beyond debuggability
    if ((!raw[raw_idx].inUse) && raw[raw_idx].wasUsed) {
        sched->has_err = true;
        sched->err     = str("error: a raw slice at idx_raw=", raw_idx, " was previously freed but it is required NOW at input_idx=", input_idx);
        return false;
    }
    return raw[raw_idx].inUse;
}

bool RawSlicesManager::rawReady(int input_idx) {
    if (sched->tm.spec->global.avoidVerticalOverwriting) {
        std::vector<int> &raw_idxs = sched->input[input_idx].requiredRawSlices;
        if (raw_idxs.empty()) { //catch this error condition before it propagates!
            sched->has_err = true;
            sched->err     = str("error: avoidVerticalOverwriting is set, but at input_idx=", input_idx, " no raw slices were required!!!!");
            return false;
        }
        for (auto raw_idx = raw_idxs.begin(); raw_idx != raw_idxs.end(); ++raw_idx) {
            if (!singleRawSliceReady(*raw_idx, input_idx)) return false;
        }
        return true;
    } else {
        int idx_raw = sched->input[input_idx].mapInputToRaw;
        return singleRawSliceReady(idx_raw, input_idx);
    }
}

clp::Paths *RawSlicesManager::getRawContour(int idx_raw, int input_idx) {
    //here, we trust that rawReady() has returned TRUE PREVIOUSLY, Otherwise... CLUSTERFUCK!!!!
    if (sched->tm.spec->global.avoidVerticalOverwriting) {
        std::vector<int> &raw_idxs = sched->input[input_idx].requiredRawSlices;
        if (raw_idxs.size() == 1) {
            //trivial case
            --raw[raw_idxs[0]].numRemainingUses;
            return &(raw[raw_idxs[0]].slice);
        } else {
            //NAIVE METHOD: JUST INTERSECT ALL CONTOURS.
            //this is naive because it shrinks the shape more than it is scritctly necessary
            /*bool first = true;
            //NOTE: THIS WILL NOT WORK UNLESS CLIPPINGS ARE DONE ONE BY ONE (AS FOUND WHILE DEBUGGING THE NON-COMMENTED VERSION OF THE CODE)
            clp::Clipper &clipper = sched->tm.multi.clipper;
            for (auto raw_idx = raw_idxs.begin(); raw_idx != raw_idxs.end(); ++raw_idx) {
                clipper.AddPaths(raw[*raw_idx].slice, first ? clp::ptClip : clp::ptSubject, true);
                first = false;
            }
            clipper.Execute(clp::ctIntersection, auxRawSlice, clp::pftNonZero, clp::pftNonZero);
            clipper.Clear();
            ClipperEndOperation(clipper);
            return &auxRawSlice;*/

            //ADVANCED METHOD: OFFSET CONTOURS TO TAKE INTO ACCOUNT THE PROFILE OF THE VOXEL
            double inputz = sched->input[input_idx].z;
            int ntool = sched->input[input_idx].ntool;
            bool firstTime = true;
            clp::Paths *next = NULL;
            for (auto raw_idx = raw_idxs.begin(); raw_idx != raw_idxs.end(); ++raw_idx) {
                --raw[*raw_idx].numRemainingUses;
                double rawz = raw[*raw_idx].z;
                if (inputz == rawz) {
                    //in this codepath, this assignment should be always executed exactly once
                    next = &raw[*raw_idx].slice;
                } else {
                    double width_at_raw = sched->tm.spec->pp[ntool].profile->getWidth(rawz - inputz);
                    //this edge case happens from time to time due to misconfigurations, just let's handle it gracefully instead of erroring out
                    if (width_at_raw == 0) continue;
                    if (width_at_raw < 0) {
                        sched->has_err = true;
                        sched->err     = str("error for input slice with input_idx=", input_idx, " at z=", inputz, ", a raw slice with raw_idx=", *raw_idx, " at z=", rawz, " was required, but the voxel's width at the raw slice Z was illegal: ", width_at_raw);
                        return NULL;
                    }
                    double diffwidth = sched->tm.spec->pp[ntool].radius - width_at_raw;
                    if (diffwidth < 0) {
                        sched->has_err = true;
                        sched->err     = str("error for input slice with input_idx=", input_idx, " at z=", inputz, ", a raw slice with raw_idx=", *raw_idx, " at z=", rawz, " was required, but the diffwidth is below 0: ", diffwidth);
                        return NULL;
                    }
                    //we use jtSquare here because it is way faster than jtRound and we do not strictly need the extra shape precision provided by jtRound
                    sched->tm.res->offsetDo(auxaux, diffwidth, raw[*raw_idx].slice, clp::jtSquare, clp::etClosedPolygon);
                    next = &auxaux;
                }
                if (firstTime) {
                    if (next == &auxaux) {
                        auxRawSlice = std::move(*next);
                    } else {
                        auxRawSlice = *next;
                    }
                    firstTime = false;
                } else {
                    sched->tm.res->clipperDo(auxRawSlice, clp::ctIntersection, auxRawSlice, *next, clp::pftNonZero, clp::pftNonZero);
                }
            }
            auxaux.clear();

            return &auxRawSlice;
        }
    } else {
        --raw[idx_raw].numRemainingUses;
        return &(raw[idx_raw].slice);
    }
}

//reconstruct cross-references from OutputSliceData to ResultSingleTool
void SimpleSlicingScheduler::post_deserialize_reconstruct() {
    if (output.empty()) return;
    for (auto &slices : tm.slicess) {
        for (auto &slice : slices) {
            output[slice->idx].result = slice.get();
        }
    }
}

//helper method for processReadyRawSlices()
void SimpleSlicingScheduler::removeUnrequiredData(double z) {
    //TODO: we have all the information needed to decided what raw/previous/additional slices are required to compute each slice, so we could write a rule engine to schedule the removal of slices exactly when we know they are not needed again!

    //delete slices ONLY if they have been used AND are way below the current input slice
    //WARNING: THIS RELIES ON A SPECIFIC CALL PATTERN TO receiveNextInputSlice() and giveNextOutputSlice() to work correctly!!!!!
    /*         the call pattern is a loop doing:
          while (not_all_input_slices_have_been_sent) {
              receiveNextInputSlice();
              while (there_are_available_output_slices) {
                  giveNextOutputSlice();
              }
          }
    */
    if (removeUnused && (tm.spec->global.schedMode!=ManualScheduling) ){//&& (input[input_idx].ntool == 0)) {
        bool sliceUpwards = tm.spec->global.sliceUpwards;
        /*why use factor 2.1: sometimes, a slice of ntool>0 will be scheduled *after*
        the immediately higher slice of ntool==0. As a result, it is important to avoid
        discarding slices up to two previous slices of ntool==0. Using 2.1 is to be on
        the safe side regarding to round-off errors.. But we use 4.1:
                -add 1 because weird things may happen if applicationPoint is not in the
                 middle for some tool
                -add 1 because computing surface toolpaths in phase 2 require feedback
                 from previously computed contours in phase 1*/
        double zlimit = z - tm.spec->pp[0].profile->sliceHeight * (sliceUpwards ? 4.1 : -4.1);
        rm.removeUsedRawSlices();
        tm.removeAdditionalContoursPastZ(zlimit);
        tm.removeUsedSlicesPastZ(zlimit, output);
      }
}



//method to consume all pending raw slices, doing phase 1 slicing computations (and phase 2 if possible)
bool SimpleSlicingScheduler::processReadyRawSlices() {
    bool ok = true;
    while (true) {
        if (rm.rawReady((int)input_idx)) {
            int this_output_idx = input[input_idx].mapInputToOutput;
            auto &this_output = output[this_output_idx];
            std::vector<ResultSingleTool*> recalleds;
            
            if (!this_output.requiredContoursForSupport.empty()) {
               recalleds = getRequiredContours(this_output.requiredContoursForSupport);
                has_err = recalleds.empty();
                if (has_err) {
                    err = str("Error: slice with input_idx=", input_idx, " should be ready for phase 1 of processing, but it requires some other slices to have already passed phase 1 themselves, but not all of them already have. Currently, the scheduler is not designed to re-order the slices in the face of this eventuality, so the process just ends with this error. This is probably due to some hiccup in the order of slices if using --slicing-manual");
                    ok = false;
                    break;
                }
                anotateRequiredContoursAsUsed(recalleds);
            }
                
            int idx_raw = input[input_idx].mapInputToRaw;
            clp::Paths *raw = rm.getRawContour(idx_raw, (int)input_idx);
            if (raw == NULL) break;
            double z = rm.raw[idx_raw].z;
            
            has_err = !tm.processSlicePhase1(recalleds, *raw, z, input[input_idx].ntool, this_output_idx, this_output.result);
            rm.auxRawSlice.clear();
            if (has_err) {
                err = tm.err;
                ok  = false;
                break;
            }
            if (this_output.recomputeRequiredAfterSupport) clearContoursAboveBelow(*this_output.result);
            if (this_output.requiredContoursForOverhang.empty() && this_output.requiredContoursForSurface.empty()) {
                has_err = !tm.processSlicePhase2(*this_output.result);
                if (has_err) {
                    err = tm.err;
                    ok  = false;
                    break;
                }
                this_output.computed = true;
            }
            removeUnrequiredData(input[input_idx].z);
            ++input_idx;
            if (input_idx >= input.size()) break;
        } else {
            break;
        }
    }
    return ok;
}

//precondition: !requiredContoursIdxs.empty()
//result: if all required contours have been computed, return vector of pointers to them. Otherwise, return empty vector
std::vector<ResultSingleTool*> SimpleSlicingScheduler::getRequiredContours(std::vector<int> &requireds) {
    std::vector<ResultSingleTool*> recalleds;
    recalleds.reserve(requireds.size());
    for (auto required : requireds) {
        if ((output[required].result != NULL)  && output[required].result->phase1complete) {
            recalleds.push_back(output[required].result);
        }
    }
    if (recalleds.size()!=requireds.size()) {
        recalleds.clear();
    }
    return recalleds;
}

void SimpleSlicingScheduler::anotateRequiredContoursAsUsed(std::vector<ResultSingleTool*> &recalleds) {
    for (auto recalled : recalleds) {
        --output[recalled->idx].numSlicesRequiringThisOne;
    }
}


//helper method for processReadySlicesPhase2(): do phase 2 slicing computations if the system is ready
bool SimpleSlicingScheduler::tryToComputeSlicePhase2(ResultSingleTool &result) {
    
    auto &requiredsOverhang = output[result.idx].requiredContoursForOverhang;
    std::vector<ResultSingleTool*> recalledsOverhang = getRequiredContours(requiredsOverhang);
    if (!requiredsOverhang.empty() && recalledsOverhang.empty()) return true;
                       
    auto &requiredsSurface = output[result.idx].requiredContoursForSurface;
    std::vector<ResultSingleTool*> recalledsSurface = getRequiredContours(requiredsSurface);
    if (!requiredsSurface.empty() && recalledsSurface.empty()) return true;
    
    anotateRequiredContoursAsUsed(recalledsOverhang);
    anotateRequiredContoursAsUsed(recalledsSurface);
                       
    bool ok = true;
    has_err = !tm.processSlicePhase2(result, std::move(recalledsOverhang), std::move(recalledsSurface), output[result.idx].recomputeRequiredAfterOverhang);
    if (has_err) {
        err = tm.err;
        ok = false;
    }
    output[result.idx].computed = true;
    return ok;
}

//method to do all pending phase 2 slicing computations
bool SimpleSlicingScheduler::processReadySlicesPhase2() {
    for (auto &slices : tm.slicess) {
        for (auto &slice : slices) {
            if (slice->phase1complete && !slice->phase2complete) {
                if (!tryToComputeSlicePhase2(*slice)) return false;
            }
        }
    }
    return true;
}


void SimpleSlicingScheduler::computeNextInputSlices() {
//temporal arrangement until we are ready to drop the old method implementation
    if(!processReadyRawSlices()) return;
    if (tm.spec->global.anyDifferentiateSurfaceInfillings || tm.spec->global.anyOverhangAlwaysSupported) processReadySlicesPhase2();
    return;
}

//this method will return slices in the intended ordering, if available
std::shared_ptr<ResultSingleTool> SimpleSlicingScheduler::giveNextOutputSlice() {
    if (!output[output_idx].computed) return std::shared_ptr<ResultSingleTool>();
    int ntool = output[output_idx].ntool;
    for (int k = 0; k < tm.slicess[ntool].size(); ++k) {
        if ((tm.slicess[ntool][k]->idx == output_idx) && (!tm.slicess[ntool][k]->used)) {
            tm.slicess[ntool][k]->used = true;
            output_idx++;
            return tm.slicess[ntool][k];
        }
    }
    has_err = true;
    err = "Could not find the expected output slice!!!";
    return std::shared_ptr<ResultSingleTool>();
}

std::string applyFeedbackFromFile(Configuration &config, MetricFactors &factors, SimpleSlicingScheduler &sched, std::vector<double> &zs, std::vector<double> &scaled_zs) {
    
    std::shared_ptr<SlicerManager> feedbackSlicer;
    
    bool useMesh = sched.tm.spec->global.fb.feedbackMesh;
    
    if (useMesh) {
        feedbackSlicer = getExternalSlicerManager(config, factors, config.getValue("SLICER_DEBUGFILE_FEEDBACK"), "");

        char *meshfullpath = fullPath(sched.tm.spec->global.fb.feedbackFile.c_str());
        if (meshfullpath == NULL) {
            return std::string("Error trying to resolve canonical path to the feedback mesh file");
        }

        if (!feedbackSlicer->start(meshfullpath)) {
            free(meshfullpath);
            std::string err = feedbackSlicer->getErrorMessage();
            return str("Error while trying to start the slicer manager: ", err, "!!!\n");
        }
        free(meshfullpath);

        double a, b, c, d, e, f;
        feedbackSlicer->getLimits(&a, &b, &c, &d, &e, &f);

        double slicer_to_input = 1 / factors.input_to_slicer;
        if (std::fabs(feedbackSlicer->getScalingFactor() - slicer_to_input) > (slicer_to_input*1e-3)) {
            feedbackSlicer->terminate();
            return str("Error: the scalingFactor from the slicer is ", feedbackSlicer->getScalingFactor(), " while the factor from the configuration is different: ", slicer_to_input, "!!!\n");
        }

        if (!feedbackSlicer->sendZs(std::move(zs))) {
                std::string err = feedbackSlicer->getErrorMessage();
                feedbackSlicer->terminate();
                return str("Error while trying to send Z values to the feedback slicer manager: ", err, "!!!\n");
        }
    } else {
        const bool  metadataRequired = false;
        const bool  filterByPathType = true;
        const int64 pathTypeValue    = PATHTYPE_PROCESSED_CONTOUR;
        feedbackSlicer = getRawSlicerManager(sched.tm.spec->global.z_epsilon, metadataRequired, filterByPathType, pathTypeValue);
        
        if (!feedbackSlicer->start(sched.tm.spec->global.fb.feedbackFile.c_str())) {
            std::string err = feedbackSlicer->getErrorMessage();
            return str("Error while trying to start the slicer manager: ", err, "!!!\n");
        }
    }


    clp::Paths rawslice;

    for (int k = 0; !feedbackSlicer->reachedEnd(); ++k) {
        rawslice.clear();

        if (!feedbackSlicer->readNextSlice(rawslice)) {
            std::string err = feedbackSlicer->getErrorMessage();
            feedbackSlicer->terminate();
            return str("Error while trying to read the ", k, "-th slice from the feedback slicer manager: ", err, "!!!\n");
        }

        double z = useMesh ? scaled_zs[k] : feedbackSlicer->getZForPreviousSlice() * factors.input_to_internal;
        sched.tm.takeAdditionalAdditiveContours(z, rawslice);

    }

    if (!feedbackSlicer->finalize()) {
        std::string err = feedbackSlicer->getErrorMessage();
        return str("Error while finalizing the feedback slicer manager: ", err, "!!!!");
    }

    return std::string();
}

