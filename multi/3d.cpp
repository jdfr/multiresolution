#include "3d.hpp"
#include "auxgeom.hpp"
#include "orientPaths.hpp"
#include "pathsfile.hpp"
#include "slicermanager.hpp"
#include "showcontours.hpp"
#include <numeric>
#include <algorithm>

//if this is too heavy (I doubt it), it can be merged into loops where it makes sense
void ToolpathManager::removeUsedSlicesPastZ(double z) {
    //TODO: decide how to remove additive contours if they are unrequired because feedback has been given with takeAdditionalAdditiveContours()
    bool sliceUpwards = spec->global.sliceUpwards;
    for (auto slices = slicess.begin(); slices != slicess.end(); ++slices) {
        slices->erase(std::remove_if(slices->begin(), slices->end(),
            [z, sliceUpwards](std::shared_ptr<ResultSingleTool> sz) { return sz->used && (sliceUpwards ? (sz->z < z) : (sz->z > z)); }
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
void ToolpathManager::updateInputWithProfilesFromPreviousSlices(clp::Paths &initialContour, clp::Paths &rawSlice, double z, int ntool) {

    bool processToComputeIsAdditive = !spec->global.addsub.addsubWorkflowMode || ntool == 0;
    bool computeContoursAlreadyFilled = spec->useContoursAlreadyFilled(ntool);

    /*the following logic for getting parts to add and parts to substract hinges on
    the multislicer using always additive processes as default (and substractive for k>0
    if the flag spec->global.addsubWorkflowMode is set). Of course, it should be fine to
    pretend that the software is valid also if we invert all additive/subtractive processes*/
    //for subtractive process: initialContour <- previously_computed_additive_contours - rawSlice - previously_computed_subtractive_contours
    //for additive process:    initialContour <- rawSlice - previously_computed_additive_contours

    //process the raw slice
    res->clipper.AddPaths(rawSlice, processToComputeIsAdditive ? clp::ptSubject : clp::ptClip, true);

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
}


/*IMPORTANT: right now, the following method should work right only if the slices are weakly ordered. The calling order should be:
   -while there are slices to be computed:
      -ask for a slice (implicitly, it should contain all processes)
      -ask for all slices within the range of the lowest process for that slice
      -do that recursively for each process, from lowest to highest
*/
bool ToolpathManager::multislice(clp::Paths &rawSlice, double z, int ntool, int output_index) {//, ResultConsumer &consumer) {

    updateInputWithProfilesFromPreviousSlices(auxInitial, rawSlice, z, ntool);

    //TODO: intilialize properly the StartPosition of the MultiSpec

    bool ret = false;

    slicess[ntool].push_back(std::make_shared<ResultSingleTool>(z, ntool, output_index));
    ResultSingleTool &output = *(slicess[ntool].back());
    ret = multi.applyProcess(output, auxInitial, contours_alreadyfilled, ntool);
    contours_alreadyfilled.clear();
    output.has_err = !output.err.empty();
    if (!ret) {
        err = str("error in applyProcess() at height z= ", z, ", process ", ntool, ": ", output.err, "\n");
        slicess.pop_back();
        return ret;
    }
    if (spec->global.substractiveOuter) {
        removeOuter(output.ptoolpaths,         spec->global.outerLimitX, spec->global.outerLimitY);
        removeOuter(output.itoolpaths,         spec->global.outerLimitX, spec->global.outerLimitY);
        if (output.alsoInfillingAreas) {
            removeOuter(output.infillingAreas, spec->global.outerLimitX, spec->global.outerLimitY);
        }
    }

    return ret;
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
    if (tm.spec->global.sliceUpwards) {
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
        input[ii].mapInputToOutput = i;
        ++num_output_by_tool[output[i].ntool];
    }
}

//this method has to trust that the input slice will be according to the list of Z input values
void RawSlicesManager::receiveNextRawSlice(clp::Paths &input) {
    raw[raw_idx].wasUsed = raw[raw_idx].inUse = true;
    raw[raw_idx].slice = std::move(input);
    ++raw_idx;
    if (sched.tm.spec->global.substractiveOuter) {
        addOuter(raw[raw_idx].slice, sched.tm.spec->global.limitX, sched.tm.spec->global.limitY);
    }
    if (sched.tm.spec->global.correct || sched.tm.spec->global.substractiveOuter) {
        orientPaths(raw[raw_idx].slice);
    }
}

bool RawSlicesManager::singleRawSliceReady(int raw_idx, int input_idx) {
    //catch errors before the scheduler's state gets mangled beyond debuggability
    if ((!raw[raw_idx].inUse) && raw[raw_idx].wasUsed) {
        sched.has_err = true;
        sched.err     = str("error: a raw slice at idx_raw=", raw_idx, " was previously freed but it is required NOW at input_idx=", input_idx);
        return false;
    }
    return raw[raw_idx].inUse;
}

bool RawSlicesManager::rawReady(int input_idx) {
    if (sched.tm.spec->global.avoidVerticalOverwriting) {
        std::vector<int> &raw_idxs = sched.input[input_idx].requiredRawSlices;
        if (raw_idxs.empty()) { //catch this error condition before it propagates!
            sched.has_err = true;
            sched.err     = str("error: avoidVerticalOverwriting is set, but at input_idx=", input_idx, " no raw slices were required!!!!");
            return false;
        }
        for (auto raw_idx = raw_idxs.begin(); raw_idx != raw_idxs.end(); ++raw_idx) {
            if (!singleRawSliceReady(*raw_idx, input_idx)) return false;
        }
        return true;
    } else {
        int idx_raw = sched.input[input_idx].mapInputToRaw;
        return singleRawSliceReady(idx_raw, input_idx);
    }
}

clp::Paths *RawSlicesManager::getRawContour(int idx_raw, int input_idx) {
    //here, we trust that rawReady() has returned TRUE PREVIOUSLY, Otherwise... CLUSTERFUCK!!!!
    if (sched.tm.spec->global.avoidVerticalOverwriting) {
        std::vector<int> &raw_idxs = sched.input[input_idx].requiredRawSlices;
        if (raw_idxs.size() == 1) {
            //trivial case
            --raw[raw_idxs[0]].numRemainingUses;
            return &(raw[raw_idxs[0]].slice);
        } else {
            //NAIVE METHOD: JUST INTERSECT ALL CONTOURS.
            //this is naive because it shrinks the shape more than it is scritctly necessary
            /*bool first = true;
            //NOTE: THIS WILL NOT WORK UNLESS CLIPPINGS ARE DONE ONE BY ONE (AS FOUND WHILE DEBUGGING THE NON-COMMENTED VERSION OF THE CODE)
            clp::Clipper &clipper = sched.tm.multi.clipper;
            for (auto raw_idx = raw_idxs.begin(); raw_idx != raw_idxs.end(); ++raw_idx) {
                clipper.AddPaths(raw[*raw_idx].slice, first ? clp::ptClip : clp::ptSubject, true);
                first = false;
            }
            clipper.Execute(clp::ctIntersection, auxRawSlice, clp::pftNonZero, clp::pftNonZero);
            clipper.Clear();
            ClipperEndOperation(clipper);
            return &auxRawSlice;*/

            //ADVANCED METHOD: OFFSET CONTOURS TO TAKE INTO ACCOUNT THE PROFILE OF THE VOXEL
            double inputz = sched.input[input_idx].z;
            int ntool = sched.input[input_idx].ntool;
            bool firstTime = true;
            clp::Paths *next = NULL;
            for (auto raw_idx = raw_idxs.begin(); raw_idx != raw_idxs.end(); ++raw_idx) {
                --raw[*raw_idx].numRemainingUses;
                double rawz = raw[*raw_idx].z;
                if (inputz == rawz) {
                    //in this codepath, this assignment should be always executed exactly once
                    next = &raw[*raw_idx].slice;
                } else {
                    double width_at_raw = sched.tm.spec->pp[ntool].profile->getWidth(rawz - inputz);
                    //this edge case happens from time to time due to misconfigurations, just let's handle it gracefully instead of erroring out
                    if (width_at_raw == 0) continue;
                    if (width_at_raw < 0) {
                        sched.has_err = true;
                        sched.err     = str("error for input slice with input_idx=", input_idx, " at z=", inputz, ", a raw slice with raw_idx=", *raw_idx, " at z=", rawz, " was required, but the voxel's width at the raw slice Z was illegal: ", width_at_raw);
                        return NULL;
                    }
                    double diffwidth = sched.tm.spec->pp[ntool].radius - width_at_raw;
                    if (diffwidth < 0) {
                        sched.has_err = true;
                        sched.err     = str("error for input slice with input_idx=", input_idx, " at z=", inputz, ", a raw slice with raw_idx=", *raw_idx, " at z=", rawz, " was required, but the diffwidth is below 0: ", diffwidth);
                        return NULL;
                    }
                    //we use jtSquare here because it is way faster than jtRound and we do not strictly need the extra shape precision provided by jtRound
                    sched.tm.res->offsetDo(auxaux, diffwidth, raw[*raw_idx].slice, clp::jtSquare, clp::etClosedPolygon);
                    next = &auxaux;
                }
                if (firstTime) {
                    auxRawSlice = std::move(*next);
                    firstTime = false;
                } else {
                    sched.tm.res->clipperDo(auxRawSlice, clp::ctIntersection, auxRawSlice, *next, clp::pftNonZero, clp::pftNonZero);
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

void SimpleSlicingScheduler::computeNextInputSlices() {
    while (true) {
        if (rm.rawReady((int)input_idx)) {
            int idx_raw = input[input_idx].mapInputToRaw;
            clp::Paths *raw = rm.getRawContour(idx_raw, (int)input_idx);
            if (raw == NULL) break;
            double z = rm.raw[idx_raw].z;
            has_err = !tm.multislice(*raw, z, input[input_idx].ntool, input[input_idx].mapInputToOutput);
            rm.auxRawSlice.clear();
            if (has_err) {
                err = tm.err;
                break;
            }
            output[input[input_idx].mapInputToOutput].computed = true;
            //delete slices ONLY if they have been used AND are way below the current input slice
            //WARNING: THIS RELIES ON A SPECIFIC CALL PATTERN TO receiveNextInputSlice() and giveNextOutputSlice() to work correctly!!!!!
            /*         the call pattern is a loop doing:
            while (not_all_input_slices_have_been_sent) {
            receiveNextInputSlice();
            while (there_are_available_output_slices) {
            giveNextOutputSlice();
            }
            }*/
            if (removeUnused && (tm.spec->global.schedMode!=ManualScheduling) ){//&& (input[input_idx].ntool == 0)) {
                bool sliceUpwards = tm.spec->global.sliceUpwards;
                /*why use factor 3.1: sometimes, a slice of ntool>0 will be scheduled *after*
                the immediately higher slice of ntool==0. As a result, it is important to avoid
                discarding slices up to two previous slices of ntool==0. Using 2.1 is to be on
                the safe side regarding to round-off errors. But we use 3.1 (adding 1) because
                weird things may happen if applicationPoint is not in the middle for some tool*/
                //TODO: we have all the information needed to decided what raw/previous/additional slices are required to compute each slice, so we could write a rule engine to schedule the removal of slices exactly when we know they are not needed again!
                double zlimit = input[input_idx].z - tm.spec->pp[0].profile->sliceHeight * (sliceUpwards ? 2.1 : -2.1);
                tm.removeUsedSlicesPastZ(zlimit);
                tm.removeAdditionalContoursPastZ(zlimit);
                rm.removeUsedRawSlices();
            }
            ++input_idx;
            if (input_idx >= input.size()) break;
        } else {
            break;
        }
    }
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


std::string applyFeedback(Configuration &config, MetricFactors &factors, SimpleSlicingScheduler &sched, std::vector<double> &zs, std::vector<double> &scaled_zs) {
    if (sched.tm.spec->global.fb.feedbackMesh) {

        std::shared_ptr<SlicerManager> feedbackSlicer = getExternalSlicerManager(config, factors, config.getValue("SLICER_DEBUGFILE_FEEDBACK"), "");

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

        feedbackSlicer->sendZs(&(zs[0]), (int)zs.size());

        clp::Paths rawslice;

        for (int k = 0; k < zs.size(); ++k) {
            rawslice.clear();

            feedbackSlicer->readNextSlice(rawslice); {
                std::string err = feedbackSlicer->getErrorMessage();
                if (!err.empty()) {
                    feedbackSlicer->terminate();
                    return str("Error while trying to read the ", k, "-th slice from the slicer manager: ", err, "!!!\n");
                }
            }

            sched.tm.takeAdditionalAdditiveContours(scaled_zs[k], rawslice);

        }

        if (!feedbackSlicer->finalize()) {
            std::string err = feedbackSlicer->getErrorMessage();
            return str("Error while finalizing the feedback slicer manager: ", err, "!!!!");
        }

        return std::string();
    } else {

        FILE * f = fopen(sched.tm.spec->global.fb.feedbackFile.c_str(), "rb");
        if (f == NULL) { return str("Could not open input file ", sched.tm.spec->global.fb.feedbackFile); }
        IOPaths iop_f(f);

        FileHeader fileheader;
        std::string err = fileheader.readFromFile(f);
        if (!err.empty()) { fclose(f); return str("Error reading file header for ", sched.tm.spec->global.fb.feedbackFile, ": ", err); }

        SliceHeader sliceheader;
        for (int currentRecord = 0; currentRecord < fileheader.numRecords; ++currentRecord) {
            std::string e = sliceheader.readFromFile(f);
            if (!e.empty())                     { err = str("Error reading ", currentRecord, "-th slice header from ", sched.tm.spec->global.fb.feedbackFile, ": ", err); break; }
            if (sliceheader.alldata.size() < 7) { err = str("Error reading ", currentRecord, "-th slice header from ", sched.tm.spec->global.fb.feedbackFile, ": header is too short!"); break; }
            if (sliceheader.type == PATHTYPE_PROCESSED_CONTOUR) {
                clp::Paths paths;
                if (sliceheader.saveFormat == PATHFORMAT_INT64) {
                    if (!iop_f.readClipperPaths(paths)) {
                        err = str("Error reading ", currentRecord, "-th integer clipperpaths: could not read record ", currentRecord, " data!");
                        break;
                    }
                } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE) {
                    if (!iop_f.readDoublePaths(paths, 1 / sliceheader.scaling)) {
                        err = str("Error reading ", currentRecord, "-th integer clipperpaths: could not read record ", currentRecord, " data!");
                        break;
                    }
                } else if (sliceheader.saveFormat == PATHFORMAT_DOUBLE_3D) {
                    err = str("Error reading feedback from pathsfile ", sched.tm.spec->global.fb.feedbackFile, ", ", currentRecord, "-th record: unknown path save format cannot be 3D!!!!");
                    break;
                } else {
                    err = str("Error reading feedback from pathsfile ", sched.tm.spec->global.fb.feedbackFile, ", ", currentRecord, "-th record: unknown path save format ", sliceheader.saveFormat, " for processed contour!!!!");
                    break;
                }
                sched.tm.takeAdditionalAdditiveContours(sliceheader.z * factors.input_to_internal, paths);
            } else {
                fseek(f, (long)(sliceheader.totalSize - sliceheader.headerSize), SEEK_CUR);
            }
        }

        fclose(f);
        return err;
    }

}
