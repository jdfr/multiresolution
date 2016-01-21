#include "medialaxis.hpp"

////////////////////////////////////////////////////////////
//Boost voronoi machinery
////////////////////////////////////////////////////////////

#define BOOST_POLYGON_NO_DEPS
#include <boost/polygon/polygon.hpp>

//heavy template machinery
namespace boost {
    namespace polygon {

        /////////////////////////////////////////
        //BINDING clp::cInt TO boost::polygon CONCEPTS IS NOT NECESSARY (AN PRODUCES ERRORS)
        //IN MSVS/MINGW BECAUSE BOOST ALREADY DECLARES THE TRAITS FOR "long long" 
        /////////////////////////////////////////

/*#if (!defined(_WIN32)) && (!defined(_WIN64)) //#ifndef _MSC_VER
        template <> struct geometry_concept<clp::cInt> { typedef coordinate_concept type; };

        template <> struct coordinate_traits<clp::cInt> {
            typedef clp::cInt coordinate_type;
            typedef long double area_type;
            typedef long long manhattan_area_type;
            typedef unsigned long long unsigned_area_type;
            typedef long long coordinate_difference;
            typedef long double coordinate_distance;
        };
#endif*/


        /////////////////////////////////////////
        //BINDING clp::IntPoint TO boost::polygon concepts
        /////////////////////////////////////////

        template <> struct geometry_concept<clp::IntPoint> { typedef point_concept type; };

        template <> struct point_traits<clp::IntPoint> {
            typedef clp::cInt coordinate_type;

            static inline coordinate_type get(const clp::IntPoint& point, orientation_2d orient) {
                return (orient == HORIZONTAL) ? point.X : point.Y;
            }
        };

        template <> struct point_mutable_traits<clp::IntPoint> {
            typedef clp::cInt coordinate_type;
            static inline void set(clp::IntPoint& point, orientation_2d orient, clp::cInt value) {
                if (orient == HORIZONTAL)
                    point.X = value;
                else
                    point.Y = value;
            }
            static inline clp::IntPoint construct(clp::cInt x_value, clp::cInt y_value) {
                clp::IntPoint retval;
                retval.X = x_value;
                retval.Y = y_value;
                return retval;
            }
        };

        /////////////////////////////////////////
        //BINDING Segment TO boost::polygon concepts
        /////////////////////////////////////////
        template <> struct geometry_concept<Segment> { typedef segment_concept type; };

        template <> struct segment_traits<Segment> {
            typedef clp::cInt coordinate_type;
            typedef clp::IntPoint point_type;

            static inline point_type get(const Segment& line, direction_1d dir) {
                return dir.to_int() ? line.b : line.a;
            }
        };


    }
}

inline void extend_path_start(clp::Path &path, double distance) {
    if (path.size()>1) path.front() = point_in_vector(path[1], path.front(), distance);
}
inline void extend_path_end(clp::Path &path, double distance) {
    if (path.size()>1) path.back() = point_in_vector(path[path.size() - 2], path.back(), distance);
}


inline bool near_equal_points(clp::IntPoint a, clp::IntPoint b, clp::cInt tolerance) {
    return (abs(a.X - b.X) <= tolerance) && (abs(a.Y - b.Y) <= tolerance);
    //return (a.X == b.X) && (a.Y - b.Y);
}

void prunedMedialAxis(HoledPolygon &hp, clp::Clipper &clipper, clp::Paths &lines, double min_width, double max_width
#ifdef TRY_TO_AVOID_EXTENDING_BIFURCATIONS
    , clp::cInt TOLERANCE
#endif
    ) {
    buildMedialAxis(hp, lines, min_width);

    //clip the lines (there might be segments external to the HoledPolygon)
    hp.clipPaths(clipper, lines);

    //very simplistic and brutish algorithm to find endpoints (toggled with USE_COMPLICATED_METHOD).
    //We hope that most instances will have very few lines
#ifdef TRY_TO_AVOID_EXTENDING_BIFURCATIONS
    std::vector<char>startIsEndpoint(lines.size(), true);
    std::vector<char>endIsEndpoint(lines.size(), true);
    std::vector<char>::iterator it;
    int numreps;
#endif
    for (clp::Paths::iterator line = lines.begin(); line != lines.end(); ++line) {
#ifdef TRY_TO_AVOID_EXTENDING_BIFURCATIONS
        size_t m = line - lines.begin();
        if (startIsEndpoint[m]) {
            numreps = 0;
            it = startIsEndpoint.begin();
            for (clp::Paths::iterator l = line; l != lines.end(); ++l, ++it) {
                if (*it) {
                    //*it = (l->front() != line->front());
                    *it = !near_equal_points(l->front(), line->front(), TOLERANCE);
                    numreps += !*it;
                }
            }
            it = endIsEndpoint.begin();
            for (clp::Paths::iterator l = line; l != lines.end(); ++l, ++it) {
                if (*it) {
                    //*it = (l->back() != line->front());
                    *it = !near_equal_points(l->back(), line->front(), TOLERANCE);
                    numreps += !*it;
                }
            }
            if (numreps <= 1)
#endif
                extend_path_start(*line, max_width + length(*line));
#ifdef TRY_TO_AVOID_EXTENDING_BIFURCATIONS
        }
        if (endIsEndpoint[m]) {
            numreps = 0;
            it = startIsEndpoint.begin();
            for (clp::Paths::iterator l = line; l != lines.end(); ++l, ++it) {
                if (*it) {
                    //*it = (l->front() != line->back());
                    *it = !near_equal_points(l->front(), line->back(), TOLERANCE);
                    numreps += !*it;
                }
            }
            it = endIsEndpoint.begin();
            for (clp::Paths::iterator l = line; l != lines.end(); ++l, ++it) {
                if (*it) {
                    //*it = (l->back() != line->back());
                    *it = !near_equal_points(l->back(), line->back(), TOLERANCE);
                    numreps += !*it;
                }
            }
            if (numreps <= 1)
#endif
                extend_path_end(*line, max_width + length(*line));
#ifdef TRY_TO_AVOID_EXTENDING_BIFURCATIONS
        }
#endif
    }

    //clip again the lines to make sure that the extended endpoint are not outside of the HoledPolygon
    hp.clipPaths(clipper, lines);

    /* remove short polylines. This check cannot be done before extending (and clipping)
    the endpoints because the extended length depends on polygon thickness, and that
    is variable (at most max_width/2 on each side)*/
    lines.erase(std::remove_if(lines.begin(), lines.end(),
        [max_width](clp::Path &line)->bool{return length(line) < max_width; }),
        lines.end());
}

//from http://stackoverflow.com/questions/23579832/why-is-there-no-transform-if-in-stl
template <
    class InputIterator, class OutputIterator,
    class UnaryOperator, class Pred
>
OutputIterator copytransform_if(InputIterator first1, InputIterator last1,
OutputIterator result, UnaryOperator op, Pred pred) {
        while (first1 != last1) {
            if (pred(*first1)) {
                *result = op(*first1);
                ++result;
            }
            ++first1;
        }
        return result;
    }

#pragma warning( push )
    /* disable this annoying warning: we are aware that we are using
    * boost::voronoi's standard int32 datatype scheme, and we are
    * being careful about it!*/
#  pragma warning( disable: 4244 4267 )
#  include "boost/polygon/voronoi.hpp"
#pragma warning( pop )

using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

typedef voronoi_diagram<double> VD;
typedef const VD::vertex_type vert_t;
typedef const VD::edge_type   edge_t;
typedef std::set<edge_t*> edgeset;


void process_neighbors(edgeset &edges, edge_t& edge, clp::Path &points);
bool valid_edge(Segments &lines, edge_t& edge, double min_width);

bool buildMedialAxis(HoledPolygon &hp, clp::Paths &paths, double min_width) {
    VD vd;
    edgeset edges;
    Segments lines;
    BBox bb = getBB(hp);
    Transformation t = bb.fitToInt32();
    //bool doFit = t.doit;
    //bool doFit = t.notTrivial();

    if (t.doit) {
        HoledPolygon scaled = hp; //create scaled copy
        transformAllPaths<applyTransform>(t, scaled);
        scaled.addToSegments(lines);
    }
    else {
        hp.addToSegments(lines);
    }

    construct_voronoi(lines.begin(), lines.end(), &vd);

    //filter out invalid edges (secondary edges contact input segments, infinite edges are not useful)
    copytransform_if(vd.edges().begin(), vd.edges().end(), std::inserter(edges, edges.end()),
        [](const VD::edge_type &edge){return &edge; },
        [](const VD::edge_type &edge){return !(edge.is_secondary() || edge.is_infinite()); });

    // find how many valid segments there are for each vertex
    std::map< vert_t*, std::set<edge_t*> > vertex_edges;  // the edges connected to each vertex
    std::set<vert_t*> startingpoints;                    // all vertices having a single starting edge
    for (VD::const_vertex_iterator it = vd.vertices().begin(); it != vd.vertices().end(); ++it) {
        vert_t* vertex = &*it;

        // loop through all edges originating from this vertex, starting from the "first" one (effectively random)
        edge_t* edge = vertex->incident_edge();
        do {
            // if this edge was not pruned by the copytransform_if, add it to vertex_edges
            if (edges.count(edge) > 0) vertex_edges[vertex].insert(edge);

            edge = edge->rot_next(); // continue with the next edge originating from this vertex
        } while (edge != vertex->incident_edge());

        // if there's only one edge incident to this vertex, it is a starting point!
        if (vertex_edges[vertex].size() == 1) startingpoints.insert(vertex);
    }

    // prune startpoints recursively if extreme segments are not valid
    while (!startingpoints.empty()) {
        vert_t* vA = *startingpoints.begin(); // get a random entry node

        //assert(vertex_edges[v].size()==1);
        edge_t* edge = *vertex_edges[vA].begin(); // get edge starting from the entry node

        if (!valid_edge(lines, *edge, min_width)) {
            // if the edge is invalid, remove it (and its twin) from the list
            edges.erase(edge);
            edges.erase(edge->twin());

            // remove accordingly the edges from the connection mapping for the affected nodes
            vert_t* vB = edge->vertex1();
            vertex_edges[vA].erase(edge);
            vertex_edges[vB].erase(edge->twin());

            // also, check whether the end vertex is a new leaf
            if (vertex_edges[vB].size() == 1)
                startingpoints.insert(vB);
            else if (vertex_edges[vB].empty())
                startingpoints.erase(vB);
        }

        // remove node from the set to prevent it from being visited again
        startingpoints.erase(vA);
    }

    // iterate through the valid edges to build paths
    clp::Path collected, path, concat;
    while (!edges.empty()) {
        edge_t &edge = **edges.begin();

        collected.clear();
        path.clear();
        concat.clear();

        // add an empty path, then populate it
        path.push_back(clp::IntPoint((clp::cInt)edge.vertex0()->x(), (clp::cInt)edge.vertex0()->y()));
        path.push_back(clp::IntPoint((clp::cInt)edge.vertex1()->x(), (clp::cInt)edge.vertex1()->y()));

        // remove this edge and its twin from the pool
        edges.erase(&edge);
        edges.erase(edge.twin());

        process_neighbors(edges, edge, path); // get next points

        // get previous points
        process_neighbors(edges, *edge.twin(), collected);

        std::move(collected.rbegin(), collected.rend(), std::back_inserter(concat));
        std::move(path.begin(),       path.end(),       std::back_inserter(concat)); //This is equivalent to MOVETO(path, concat), but do not use MOVETO for code clearness (the above statement cannot be encoded as MOVETO)
        //the preceding two moves should be more efficient than the following one insert:
        //path.insert(path.begin(), collected.rbegin(), collected.rend());

        if (t.doit) reverseTransform(t, concat);

        paths.push_back(std::move(concat));

    }

    return t.doit;
}

void process_neighbors(edgeset &edges, edge_t& edge, clp::Path &points) {
    /* rot_next() works on the edge start point but we are looking
    for neighbors on the end point, so we use the edge's twin*/
    edge_t & twin = *edge.twin();

    // find number of neighbors
    std::vector<edge_t*> neighs;
    for (edge_t* neigh = twin.rot_next(); neigh != &twin; neigh = neigh->rot_next())
        if (edges.count(neigh) > 0)
            neighs.push_back(neigh);

    // add neighbours recursively until we find more than one (i.e., until we find a bifurcation)
    if (neighs.size() == 1) {
        edge_t *neigh = neighs.front();
        points.push_back(clp::IntPoint((clp::cInt)neigh->vertex1()->x(), (clp::cInt)neigh->vertex1()->y()));
        edges.erase(neigh);
        edges.erase(neigh->twin());
        process_neighbors(edges, *neigh, points);
    }
}


bool valid_edge(Segments &lines, edge_t& edge, double min_width) {
    /* the edge is valid if the cells sharing this edge do not have a common vertex,
    since this means that the edge lies on the bisector of two contiguous input
    lines. For thin regions these edges will be very short and should be discarded*/

    // retrieve the original line segments for this segment
    const VD::cell_type &cellA = *edge.cell();
    const VD::cell_type &cellB = *edge.twin()->cell();
    if (!(cellA.contains_segment() && cellB.contains_segment())) return false;
    const Segment &segmentA = lines[cellA.source_index()];
    const Segment &segmentB = lines[cellB.source_index()];

    // relative angle between the two segments
    double angle = fabs(segmentB.orientation() - segmentA.orientation());

    // the angle can range from 0 (same direction) to PI (opposite direction)
    // we're interested only in segments close to the second case (facing segments)
    // but we allow a tolerance. this ensures that we're dealing with a thin area
    if (fabs(angle - M_PI) > M_PI / 5) return false;

    /* each edge vertex is equidistant to both cell segments but the distance
    will differ between the two vertices IF the shape is narrowing
    (f.ex. in a corner). If so, we skip the edge since it's not part of
    our intended output*/

    // distances of each vertices to the segments
    double distA = distance_to(segmentA.a, segmentB.b);
    double distB = distance_to(segmentA.b, segmentB.a);

    // skip the edge if the area is too thin
    return !(distA < min_width && distB < min_width);
}
