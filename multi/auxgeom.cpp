#include "auxgeom.hpp"

////////////////////////////////////////////////////////////
//Segments
////////////////////////////////////////////////////////////

//distance bewteen points
double distance_to(const clp::IntPoint &point1, const clp::IntPoint &point2) {
    clp::cInt dx = point1.X-point2.X;
    clp::cInt dy = point1.Y-point2.Y;
    return sqrt(dx*dx+dy*dy);
}

//distance from point to middle of the segment
double distance_to(const clp::IntPoint &point, const Segment &line) {
    const double dx = (double)(line.b.X - line.a.X);
    const double dy = (double)(line.b.Y - line.a.Y);
    
    const double l2 = dx*dx + dy*dy;  // avoid a sqrt
    if (l2 == 0.0) return distance_to(point, line.a);   // line.a == line.b case
    
    // Consider the line extending the segment, parameterized as line.a + t (line.b - line.a).
    // We find projection of this point onto the line. 
    // It falls where t = [(this-line.a) . (line.b-line.a)] / |line.b-line.a|^2
    const double t = ((point.X - line.a.X) * dx + (point.Y - line.a.Y) * dy) / l2;
    if (t < 0.0)      return distance_to(point, line.a);  // beyond the 'a' end of the segment
    else if (t > 1.0) return distance_to(point, line.b);  // beyond the 'b' end of the segment
    clp::IntPoint projection(
        (clp::cInt)(line.a.X + t * dx),
        (clp::cInt)(line.a.Y + t * dy)
    );
    return distance_to(point, projection);
}

double Segment::length() const {
    return distance_to(this->a, this->b);
}

double Segment::orientation() const {
    double angle = atan2(this->b.Y - this->a.Y, this->b.X - this->a.X);
    if (angle < 0) angle = 2*M_PI + angle;
    return angle;
}

clp::IntPoint point_in_vector(clp::IntPoint &origin, clp::IntPoint &dest, double distance) {
    double factor = distance / distance_to(origin, dest);
    clp::IntPoint point = origin;
    if (origin.X != dest.X)
        point.X = origin.X + (clp::cInt)((dest.X - origin.X) * factor);
    if (origin.Y != dest.Y)
        point.Y = origin.Y + (clp::cInt)((dest.Y - origin.Y) * factor);
    return point;
}

double length(clp::Path &path) {
    double len = 0;
    for (clp::Path::iterator point = path.begin() + 1; point < path.end(); ++point) {
        len += distance_to(*point, *(point - 1));
    }
    return len;
}

void addPathToSegments(clp::Path &path, Segments &segments, PathMode mode) {
    //size_t numnew = segments.size()+path.size();
    //if (mode==openMode) ++numnew;
    //segments.reserve(numnew);
    clp::Path::iterator last = path.end()-1;
    for (clp::Path::iterator it = path.begin(); it != last; ++it) {
        segments.push_back(Segment(*it, *(it+1)));
    }
    if (mode==openMode) segments.push_back(Segment(path.back(), path.front()));
}

void addPathsToSegments(clp::Paths &paths, Segments &segments, PathMode mode) {
    for (clp::Paths::iterator path = paths.begin(); path != paths.end(); ++path) {
        addPathToSegments(*path, segments, mode);
    }
}

void applyTransform(Transformation &t, clp::Path &path) {
    if (t.usescale) { //scale only if necessary
        for (clp::Path::iterator point = path.begin(); point != path.end(); ++point) {
            point->X = (clp::cInt)((point->X + t.dx) * t.scale);
            point->Y = (clp::cInt)((point->Y + t.dy) * t.scale);
        }
    } else {
        for (clp::Path::iterator point = path.begin(); point != path.end(); ++point) {
            point->X += t.dx;
            point->Y += t.dy;
        }
    }
}

void reverseTransform(Transformation &t, clp::Path &path) {
    if (t.usescale) { //scale only if necessary
        for (clp::Path::iterator point = path.begin(); point != path.end(); ++point) {
            point->X = (clp::cInt)((double)point->X * t.invscale) - t.dx;
            point->Y = (clp::cInt)((double)point->Y * t.invscale) - t.dy;
        }
    } else {
        for (clp::Path::iterator point = path.begin(); point != path.end(); ++point) {
            point->X -= t.dx;
            point->Y -= t.dy;
        }
    }
}


////////////////////////////////////////////////////////////
//BBox
////////////////////////////////////////////////////////////

BBox getBB(clp::Path &path) {
    if (path.empty()) {
        return BBox();
    }

    BBox bb(LLONG_MAX, LLONG_MIN, LLONG_MAX, LLONG_MIN);

    for (clp::Path::iterator point = path.begin(); point != path.end(); ++point) {
        bb.minx = std::min(bb.minx, point->X);
        bb.maxx = std::max(bb.maxx, point->X);
        bb.miny = std::min(bb.miny, point->Y);
        bb.maxy = std::max(bb.maxy, point->Y);
    }

    return bb;
}

BBox getBB(clp::Paths &paths){
    if (paths.empty()) {
        return BBox();
    }

    BBox bb(LLONG_MAX, LLONG_MIN, LLONG_MAX, LLONG_MIN);
    for (clp::Paths::const_iterator path = paths.begin(); path != paths.end(); ++path) {
        for (clp::Path::const_iterator point = path->begin(); point != path->end(); ++point) {
            bb.minx = std::min(bb.minx, point->X);
            bb.maxx = std::max(bb.maxx, point->X);
            bb.miny = std::min(bb.miny, point->Y);
            bb.maxy = std::max(bb.maxy, point->Y);
        }
    }

    return bb;
}

BBox getBB(HoledPolygon &hp) {
    return getBB(hp.contour); //we are relying on the contour ALWAYS being the outer path
}

BBox getBB(HoledPolygons &hps) {
    if (hps.empty()) {
        return BBox();
    }

    BBox bb(LLONG_MAX, LLONG_MIN, LLONG_MAX, LLONG_MIN);
    BBox sub;
    for (auto hp = hps.begin(); hp != hps.end(); ++hp) {
        sub = getBB(*hp);
        bb.minx = std::min(bb.minx, sub.minx);
        bb.maxx = std::max(bb.maxx, sub.maxx);
        bb.miny = std::min(bb.miny, sub.miny);
        bb.maxy = std::max(bb.maxy, sub.maxy);
    }

    return bb;
}

//const int maxpower = 27;
const int maxpower = 31;
const clp::cInt MAX32 = (((clp::cInt)1) << maxpower) - 1;
const clp::cInt MIN32 = -MAX32;
//do not feel confortable using up all the space in int32, so give it a little slack
const double TRANSFORMATION_CEILING = MAX32 - (1 << 10);

Transformation BBox::fitToInt32() {
    if ((this->minx > MIN32) && (this->maxx < MAX32) &&
        (this->miny > MIN32) && (this->maxy < MAX32))
        return Transformation(); //relying on int being 32 bits
    clp::cInt mx = (this->maxx - this->minx) / 2;
    clp::cInt my = (this->maxy - this->miny) / 2;
    double factor = TRANSFORMATION_CEILING / (double)std::max(mx, my);
    return Transformation(-(this->minx + mx), -(this->miny + my), (factor<1.0) ? factor : 1.0 );
}


////////////////////////////////////////////////////////////
//HoledPolygons
////////////////////////////////////////////////////////////

void HoledPolygon::addToSegments(Segments &segments) {
    addPathToSegments(this->contour, segments, openMode);// closedMode);
    addPathsToSegments(this->holes, segments, openMode);// closedMode);
}

void HoledPolygon::offset(clp::ClipperOffset &offset, double radius, HoledPolygons &result) {
    result.clear();
    clp::PolyTree pt;
    this->offset(offset, radius, pt);
    AddPolyTreeToHPs(pt, result);
}

void recursiveAddPolyTreeToHPs(clp::PolyNode& polynode, HoledPolygons &hps) {
    auto child = polynode.Childs.begin();
    auto childend = polynode.Childs.end();
    { //recursive function, try to keep the function state size down to the bare minimum
        HoledPolygon newhp;
        newhp.contour = std::move(polynode.Contour); //move semantics
        newhp.holes.resize(polynode.ChildCount());
        auto holesend = newhp.holes.end();
        //use move semantics both for adding the holes and the whole HoledPolygon
        for (clp::Paths::iterator hole = newhp.holes.begin(); hole != holesend; ++hole) {
            *hole = std::move((*child++)->Contour); //move semantics
        }
        hps.push_back(std::move(newhp)); //move semantics
    }
    for (child = polynode.Childs.begin(); child != childend; ++child) {
        auto subchildend = (*child)->Childs.end();
        for (auto subchild = (*child)->Childs.begin(); subchild != subchildend; ++subchild) {
            recursiveAddPolyTreeToHPs(**subchild, hps);
        }
    }
}

void AddPolyTreeToHPs(clp::PolyTree &pt, HoledPolygons &hps) {
    for (int i = 0; i < pt.ChildCount(); ++i)
        recursiveAddPolyTreeToHPs(*pt.Childs[i], hps);
}

void AddPathsToHPs(clp::Paths &paths, HoledPolygons &hps) {
    ClipperLib::Clipper clipper;
    
    clipper.AddPaths(paths, clp::ptSubject, true);
    clp::PolyTree pt;
    clipper.Execute(clp::ctUnion, pt, clp::pftEvenOdd, clp::pftEvenOdd);
    
    AddPolyTreeToHPs(pt, hps);
}

void AddHPsToPaths(HoledPolygons &hps, clp::Paths &paths) {
    size_t numpaths = 0;
    for (HoledPolygons::iterator hp = hps.begin(); hp != hps.end(); ++hp) {
        numpaths += 1 + hp->holes.size();
    }
    paths.reserve(paths.size() + numpaths);
    for (HoledPolygons::iterator hp = hps.begin(); hp != hps.end(); ++hp) {
        paths.push_back(hp->contour);
        COPYTO(hp->holes, paths);
    }
}

//same as the other function, but make the paths closed
void AddHPsToClosedPaths(HoledPolygons &hps, clp::Paths &paths) {
    size_t numpaths = 0;
    for (HoledPolygons::iterator hp = hps.begin(); hp != hps.end(); ++hp) {
        numpaths += 1 + hp->holes.size();
    }
    paths.reserve(paths.size() + numpaths);
    for (HoledPolygons::iterator hp = hps.begin(); hp != hps.end(); ++hp) {
        paths.push_back(clp::Path());
        paths.back().reserve(hp->contour.size() + 1);
        COPYTO(hp->contour, paths.back());
        paths.back().push_back(hp->contour.front());
        auto endhole = hp->holes.end();
        for (auto hole = hp->holes.begin(); hole != endhole; ++hole) {
            paths.push_back(clp::Path());
            paths.back().reserve(hole->size() + 1);
            COPYTO(*hole, paths.back());
            paths.back().push_back(hole->front());
        }
    }
}

void HoledPolygon::clipPaths(clp::Clipper &clipper, clp::Paths &paths) {
    //bool out;
    //out =
    clipper.AddPaths(paths, clp::ptSubject, false);
    //out =
    clipper.AddPath(this->contour, clp::ptClip, true);
    //out =
    clipper.AddPaths(this->holes, clp::ptClip, true);
    clp::PolyTree polytree;
    clipper.Execute(clp::ctIntersection, polytree, clp::pftEvenOdd, clp::pftEvenOdd);
    clipper.Clear();
    clp::PolyTreeToPaths(polytree, paths);
}

