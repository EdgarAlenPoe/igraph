/* -*- mode: C -*-  */
/* vim:set ts=4 sw=4 sts=4 et: */
/*
   IGraph library.
   Copyright (C) 2012  Tamas Nepusz <ntamas@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301 USA

*/

#include "igraph_error.h"
#include "igraph_matching.h"

#include "igraph_adjlist.h"
#include "igraph_constructors.h"
#include "igraph_conversion.h"
#include "igraph_dqueue.h"
#include "igraph_interface.h"
#include "igraph_structural.h"
#include "igraph_vector.h"

#include <math.h>
#include <stdlib.h>

#define MATCHING_DEBUG // TODO: comment out when done debugging

#ifdef _MSC_VER
/* MSVC does not support variadic macros */
#include <stdarg.h>
static void debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
#ifdef MATCHING_DEBUG
    vfprintf(stderr, fmt, args);
#endif
    va_end(args);
}
#else
#ifdef MATCHING_DEBUG
    #define debug(...) fprintf(stderr, __VA_ARGS__)
#else
    #define debug(...)
#endif
#endif

/**
 * \function igraph_is_matching
 * Checks whether the given matching is valid for the given graph.
 *
 * This function checks a matching vector and verifies whether its length
 * matches the number of vertices in the given graph, its values are between
 * -1 (inclusive) and the number of vertices (exclusive), and whether there
 * exists a corresponding edge in the graph for every matched vertex pair.
 * For bipartite graphs, it also verifies whether the matched vertices are
 * in different parts of the graph.
 *
 * \param graph The input graph. It can be directed but the edge directions
 *              will be ignored.
 * \param types If the graph is bipartite and you are interested in bipartite
 *              matchings only, pass the vertex types here. If the graph is
 *              non-bipartite, simply pass \c NULL.
 * \param matching The matching itself. It must be a vector where element i
 *                 contains the ID of the vertex that vertex i is matched to,
 *                 or -1 if vertex i is unmatched.
 * \param result Pointer to a boolean variable, the result will be returned
 *               here.
 *
 * \sa \ref igraph_is_maximal_matching() if you are also interested in whether
 *     the matching is maximal (i.e. non-extendable).
 *
 * Time complexity: O(|V|+|E|) where |V| is the number of vertices and
 * |E| is the number of edges.
 *
 * \example examples/simple/igraph_maximum_bipartite_matching.c
 */
igraph_error_t igraph_is_matching(const igraph_t *graph,
                       const igraph_vector_bool_t *types, const igraph_vector_int_t *matching,
                       igraph_bool_t *result) {
    igraph_integer_t i, j, no_of_nodes = igraph_vcount(graph);
    igraph_bool_t conn;

    /* Checking match vector length */
    if (igraph_vector_int_size(matching) != no_of_nodes) {
        *result = false; return IGRAPH_SUCCESS;
    }

    for (i = 0; i < no_of_nodes; i++) {
        j = VECTOR(*matching)[i];

        /* Checking range of each element in the match vector */
        if (j < -1 || j >= no_of_nodes) {
            *result = false; return IGRAPH_SUCCESS;
        }
        /* When i is unmatched, we're done */
        if (j == -1) {
            continue;
        }
        /* Matches must be mutual */
        if (VECTOR(*matching)[j] != i) {
            *result = false; return IGRAPH_SUCCESS;
        }
        /* Matched vertices must be connected */
        IGRAPH_CHECK(igraph_are_connected(graph, i,
                                          j, &conn));
        if (!conn) {
            /* Try the other direction -- for directed graphs */
            IGRAPH_CHECK(igraph_are_connected(graph, j,
                                              i, &conn));
            if (!conn) {
                *result = false; return IGRAPH_SUCCESS;
            }
        }
    }

    if (types != 0) {
        /* Matched vertices must be of different types */
        for (i = 0; i < no_of_nodes; i++) {
            j = VECTOR(*matching)[i];
            if (j == -1) {
                continue;
            }
            if (VECTOR(*types)[i] == VECTOR(*types)[j]) {
                *result = false; return IGRAPH_SUCCESS;
            }
        }
    }

    *result = true;
    return IGRAPH_SUCCESS;
}

/**
 * \function igraph_is_maximal_matching
 * Checks whether a matching in a graph is maximal.
 *
 * A matching is maximal if and only if there exists no unmatched vertex in a
 * graph such that one of its neighbors is also unmatched.
 *
 * \param graph The input graph. It can be directed but the edge directions
 *              will be ignored.
 * \param types If the graph is bipartite and you are interested in bipartite
 *              matchings only, pass the vertex types here. If the graph is
 *              non-bipartite, simply pass \c NULL.
 * \param matching The matching itself. It must be a vector where element i
 *                 contains the ID of the vertex that vertex i is matched to,
 *                 or -1 if vertex i is unmatched.
 * \param result Pointer to a boolean variable, the result will be returned
 *               here.
 *
 * \sa \ref igraph_is_matching() if you are only interested in whether a
 *     matching vector is valid for a given graph.
 *
 * Time complexity: O(|V|+|E|) where |V| is the number of vertices and
 * |E| is the number of edges.
 *
 * \example examples/simple/igraph_maximum_bipartite_matching.c
 */
igraph_error_t igraph_is_maximal_matching(const igraph_t *graph,
                               const igraph_vector_bool_t *types, const igraph_vector_int_t *matching,
                               igraph_bool_t *result) {

    igraph_integer_t i, j, n, no_of_nodes = igraph_vcount(graph);
    igraph_vector_int_t neis;
    igraph_bool_t valid;

    IGRAPH_CHECK(igraph_is_matching(graph, types, matching, &valid));
    if (!valid) {
        *result = false; return IGRAPH_SUCCESS;
    }

    IGRAPH_VECTOR_INT_INIT_FINALLY(&neis, 0);

    valid = 1;
    for (i = 0; i < no_of_nodes; i++) {
        j = VECTOR(*matching)[i];
        if (j != -1) {
            continue;
        }

        IGRAPH_CHECK(igraph_neighbors(graph, &neis, i,
                                      IGRAPH_ALL));
        n = igraph_vector_int_size(&neis);
        for (j = 0; j < n; j++) {
            if (VECTOR(*matching)[VECTOR(neis)[j]] == -1) {
                if (types == 0 ||
                    VECTOR(*types)[i] != VECTOR(*types)[VECTOR(neis)[j]]) {
                    valid = 0; break;
                }
            }
        }
    }

    igraph_vector_int_destroy(&neis);
    IGRAPH_FINALLY_CLEAN(1);

    *result = valid;
    return IGRAPH_SUCCESS;
}

static igraph_error_t igraph_i_maximum_bipartite_matching_unweighted(
        const igraph_t *graph,
        const igraph_vector_bool_t *types, igraph_integer_t *matching_size,
        igraph_vector_int_t *matching);

static igraph_error_t igraph_i_maximum_bipartite_matching_weighted(
        const igraph_t *graph,
        const igraph_vector_bool_t *types, igraph_integer_t *matching_size,
        igraph_real_t *matching_weight, igraph_vector_int_t *matching,
        const igraph_vector_t *weights, igraph_real_t eps);

#define MATCHED(v) (VECTOR(match)[v] != -1)
#define UNMATCHED(v) (!MATCHED(v))

/**
 * \function igraph_maximum_bipartite_matching
 * Calculates a maximum matching in a bipartite graph.
 *
 * A matching in a bipartite graph is a partial assignment of vertices
 * of the first kind to vertices of the second kind such that each vertex of
 * the first kind is matched to at most one vertex of the second kind and
 * vice versa, and matched vertices must be connected by an edge in the graph.
 * The size (or cardinality) of a matching is the number of edges.
 * A matching is a maximum matching if there exists no other matching with
 * larger cardinality. For weighted graphs, a maximum matching is a matching
 * whose edges have the largest possible total weight among all possible
 * matchings.
 *
 * </para><para>
 * Maximum matchings in bipartite graphs are found by the push-relabel algorithm
 * with greedy initialization and a global relabeling after every n/2 steps where
 * n is the number of vertices in the graph.
 *
 * </para><para>
 * References: Cherkassky BV, Goldberg AV, Martin P, Setubal JC and Stolfi J:
 * Augment or push: A computational study of bipartite matching and
 * unit-capacity flow algorithms. ACM Journal of Experimental Algorithmics 3,
 * 1998.
 *
 * </para><para>
 * Kaya K, Langguth J, Manne F and Ucar B: Experiments on push-relabel-based
 * maximum cardinality matching algorithms for bipartite graphs. Technical
 * Report TR/PA/11/33 of the Centre Europeen de Recherche et de Formation
 * Avancee en Calcul Scientifique, 2011.
 *
 * \param graph The input graph. It can be directed but the edge directions
 *              will be ignored.
 * \param types Boolean vector giving the vertex types of the graph.
 * \param matching_size The size of the matching (i.e. the number of matched
 *                      vertex pairs will be returned here). It may be \c NULL
 *                      if you don't need this.
 * \param matching_weight The weight of the matching if the edges are weighted,
 *                        or the size of the matching again if the edges are
 *                        unweighted. It may be \c NULL if you don't need this.
 * \param matching The matching itself. It must be a vector where element i
 *                 contains the ID of the vertex that vertex i is matched to,
 *                 or -1 if vertex i is unmatched.
 * \param weights A null pointer (=no edge weights), or a vector giving the
 *                weights of the edges. Note that the algorithm is stable
 *                only for integer weights.
 * \param eps A small real number used in equality tests in the weighted
 *            bipartite matching algorithm. Two real numbers are considered
 *            equal in the algorithm if their difference is smaller than
 *            \c eps. This is required to avoid the accumulation of numerical
 *            errors. It is advised to pass a value derived from the
 *            \c DBL_EPSILON constant in \c float.h here. If you are
 *            running the algorithm with no \c weights vector, this argument
 *            is ignored.
 * \return Error code.
 *
 * Time complexity: O(sqrt(|V|) |E|) for unweighted graphs (according to the
 * technical report referenced above), O(|V||E|) for weighted graphs.
 *
 * \example examples/simple/igraph_maximum_bipartite_matching.c
 */
igraph_error_t igraph_maximum_bipartite_matching(const igraph_t *graph,
                                      const igraph_vector_bool_t *types, igraph_integer_t *matching_size,
                                      igraph_real_t *matching_weight, igraph_vector_int_t *matching,
                                      const igraph_vector_t *weights, igraph_real_t eps) {

    /* Sanity checks */
    if (igraph_vector_bool_size(types) < igraph_vcount(graph)) {
        IGRAPH_ERROR("types vector too short", IGRAPH_EINVAL);
    }
    if (weights && igraph_vector_size(weights) < igraph_ecount(graph)) {
        IGRAPH_ERROR("weights vector too short", IGRAPH_EINVAL);
    }

    if (weights == 0) {
        IGRAPH_CHECK(igraph_i_maximum_bipartite_matching_unweighted(graph, types,
                     matching_size, matching));
        if (matching_weight != 0) {
            *matching_weight = *matching_size;
        }
        return IGRAPH_SUCCESS;
    } else {
        IGRAPH_CHECK(igraph_i_maximum_bipartite_matching_weighted(graph, types,
                     matching_size, matching_weight, matching, weights, eps));
        return IGRAPH_SUCCESS;
    }
}

static igraph_error_t igraph_i_maximum_bipartite_matching_unweighted_relabel(
        const igraph_t* graph,
        const igraph_vector_bool_t* types, igraph_vector_int_t* labels,
        igraph_vector_int_t* matching, igraph_bool_t smaller_set);

/**
 * Finding maximum bipartite matchings on bipartite graphs using the
 * push-relabel algorithm.
 *
 * The implementation follows the pseudocode in Algorithm 1 of the
 * following paper:
 *
 * Kaya K, Langguth J, Manne F and Ucar B: Experiments on push-relabel-based
 * maximum cardinality matching algorithms for bipartite graphs. Technical
 * Report TR/PA/11/33 of CERFACS (Centre Européen de Recherche et de Formation
 * Avancée en Calcul Scientifique).
 * http://www.cerfacs.fr/algor/reports/2011/TR_PA_11_33.pdf
 */
static igraph_error_t igraph_i_maximum_bipartite_matching_unweighted(
        const igraph_t *graph,
        const igraph_vector_bool_t *types, igraph_integer_t *matching_size,
        igraph_vector_int_t *matching) {
    igraph_integer_t i, j, k, n, no_of_nodes = igraph_vcount(graph);
    igraph_integer_t num_matched;             /* number of matched vertex pairs */
    igraph_vector_int_t match;       /* will store the matching */
    igraph_vector_int_t labels;           /* will store the labels */
    igraph_vector_int_t neis;             /* used to retrieve the neighbors of a node */
    igraph_dqueue_int_t q;           /* a FIFO for push ordering */
    igraph_bool_t smaller_set;        /* denotes which part of the bipartite graph is smaller */
    igraph_integer_t label_changed = 0;       /* Counter to decide when to run a global relabeling */
    igraph_integer_t relabeling_freq = no_of_nodes / 2;

    /* We will use:
     * - FIFO push ordering
     * - global relabeling frequency: n/2 steps where n is the number of nodes
     * - simple greedy matching for initialization
     */

    /* (1) Initialize data structures */
    IGRAPH_CHECK(igraph_vector_int_init(&match, no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_int_destroy, &match);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&labels, no_of_nodes);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&neis, 0);
    IGRAPH_CHECK(igraph_dqueue_int_init(&q, 0));
    IGRAPH_FINALLY(igraph_dqueue_int_destroy, &q);

    /* (2) Initially, every node is unmatched */
    igraph_vector_int_fill(&match, -1);

    /* (3) Find an initial matching in a greedy manner.
     *     At the same time, find which side of the graph is smaller. */
    num_matched = 0; j = 0;
    for (i = 0; i < no_of_nodes; i++) {
        if (VECTOR(*types)[i]) {
            j++;
        }
        if (MATCHED(i)) {
            continue;
        }
        IGRAPH_CHECK(igraph_neighbors(graph, &neis, i,
                                      IGRAPH_ALL));
        n = igraph_vector_int_size(&neis);
        for (j = 0; j < n; j++) {
            k = VECTOR(neis)[j];
            if (VECTOR(*types)[k] == VECTOR(*types)[i]) {
                IGRAPH_ERROR("Graph is not bipartite with supplied types vector", IGRAPH_EINVAL);
            }
            if (UNMATCHED(k)) {
                /* We match vertex i to vertex VECTOR(neis)[j] */
                VECTOR(match)[k] = i;
                VECTOR(match)[i] = k;
                num_matched++;
                break;
            }
        }
    }
    smaller_set = (j <= no_of_nodes / 2);

    /* (4) Set the initial labeling -- lines 1 and 2 in the tech report */
    IGRAPH_CHECK(igraph_i_maximum_bipartite_matching_unweighted_relabel(
                     graph, types, &labels, &match, smaller_set));

    /* (5) Fill the push queue with the unmatched nodes from the smaller set. */
    for (i = 0; i < no_of_nodes; i++) {
        if (UNMATCHED(i) && VECTOR(*types)[i] == smaller_set) {
            IGRAPH_CHECK(igraph_dqueue_int_push(&q, i));
        }
    }

    /* (6) Main loop from the referenced tech report -- lines 4--13 */
    label_changed = 0;
    while (!igraph_dqueue_int_empty(&q)) {
        igraph_integer_t v = igraph_dqueue_int_pop(&q);             /* Line 13 */
        igraph_integer_t u = -1, label_u = 2 * no_of_nodes;
        igraph_integer_t w;

        if (label_changed >= relabeling_freq) {
            /* Run global relabeling */
            IGRAPH_CHECK(igraph_i_maximum_bipartite_matching_unweighted_relabel(
                             graph, types, &labels, &match, smaller_set));
            label_changed = 0;
        }

        debug("Considering vertex %ld\n", v);

        /* Line 5: find row u among the neighbors of v s.t. label(u) is minimal */
        IGRAPH_CHECK(igraph_neighbors(graph, &neis, v,
                                      IGRAPH_ALL));
        n = igraph_vector_int_size(&neis);
        for (i = 0; i < n; i++) {
            if (VECTOR(labels)[VECTOR(neis)[i]] < label_u) {
                u = VECTOR(neis)[i];
                label_u = VECTOR(labels)[u];
                label_changed++;
            }
        }

        debug("  Neighbor with smallest label: %ld (label=%ld)\n", u, label_u);

        if (label_u < no_of_nodes) {                         /* Line 6 */
            VECTOR(labels)[v] = VECTOR(labels)[u] + 1;         /* Line 7 */
            if (MATCHED(u)) {                                  /* Line 8 */
                w = VECTOR(match)[u];
                debug("  Vertex %ld is matched to %ld, performing a double push\n", u, w);
                if (w != v) {
                    VECTOR(match)[u] = -1; VECTOR(match)[w] = -1;  /* Line 9 */
                    IGRAPH_CHECK(igraph_dqueue_int_push(&q, w));  /* Line 10 */
                    debug("  Unmatching & activating vertex %ld\n", w);
                    num_matched--;
                }
            }
            VECTOR(match)[u] = v; VECTOR(match)[v] = u;      /* Line 11 */
            num_matched++;
            VECTOR(labels)[u] += 2;                          /* Line 12 */
            label_changed++;
        }
    }

    /* Fill the output parameters */
    if (matching != 0) {
        IGRAPH_CHECK(igraph_vector_int_update(matching, &match));
    }
    if (matching_size != 0) {
        *matching_size = num_matched;
    }

    /* Release everything */
    igraph_dqueue_int_destroy(&q);
    igraph_vector_int_destroy(&neis);
    igraph_vector_int_destroy(&labels);
    igraph_vector_int_destroy(&match);
    IGRAPH_FINALLY_CLEAN(4);

    return IGRAPH_SUCCESS;
}

static igraph_error_t igraph_i_maximum_bipartite_matching_unweighted_relabel(
        const igraph_t *graph,
        const igraph_vector_bool_t *types, igraph_vector_int_t *labels,
        igraph_vector_int_t *match, igraph_bool_t smaller_set) {
    igraph_integer_t i, j, n, no_of_nodes = igraph_vcount(graph), matched_to;
    igraph_dqueue_int_t q;
    igraph_vector_int_t neis;

    debug("Running global relabeling.\n");

    /* Set all the labels to no_of_nodes first */
    igraph_vector_int_fill(labels, no_of_nodes);

    /* Allocate vector for neighbors */
    IGRAPH_VECTOR_INT_INIT_FINALLY(&neis, 0);

    /* Create a FIFO for the BFS and initialize it with the unmatched rows
     * (i.e. members of the larger set) */
    IGRAPH_CHECK(igraph_dqueue_int_init(&q, 0));
    IGRAPH_FINALLY(igraph_dqueue_int_destroy, &q);
    for (i = 0; i < no_of_nodes; i++) {
        if (VECTOR(*types)[i] != smaller_set && VECTOR(*match)[i] == -1) {
            IGRAPH_CHECK(igraph_dqueue_int_push(&q, i));
            VECTOR(*labels)[i] = 0;
        }
    }

    /* Run the BFS */
    while (!igraph_dqueue_int_empty(&q)) {
        igraph_integer_t v = igraph_dqueue_int_pop(&q);
        igraph_integer_t w;

        IGRAPH_CHECK(igraph_neighbors(graph, &neis, v,
                                      IGRAPH_ALL));

        n = igraph_vector_int_size(&neis);
        for (j = 0; j < n; j++) {
            w = VECTOR(neis)[j];
            if (VECTOR(*labels)[w] == no_of_nodes) {
                VECTOR(*labels)[w] = VECTOR(*labels)[v] + 1;
                matched_to = VECTOR(*match)[w];
                if (matched_to != -1 && VECTOR(*labels)[matched_to] == no_of_nodes) {
                    IGRAPH_CHECK(igraph_dqueue_int_push(&q, matched_to));
                    VECTOR(*labels)[matched_to] = VECTOR(*labels)[w] + 1;
                }
            }
        }
    }

    igraph_dqueue_int_destroy(&q);
    igraph_vector_int_destroy(&neis);
    IGRAPH_FINALLY_CLEAN(2);

    return IGRAPH_SUCCESS;
}

/**
 * Finding maximum bipartite matchings on bipartite graphs using the
 * Hungarian algorithm (a.k.a. Kuhn-Munkres algorithm).
 *
 * The algorithm uses a maximum cardinality matching on a subset of
 * tight edges as a starting point. This is achieved by
 * \c igraph_i_maximum_bipartite_matching_unweighted on the restricted
 * graph.
 *
 * The algorithm works reliably only if the weights are integers. The
 * \c eps parameter should specity a very small number; if the slack on
 * an edge falls below \c eps, it will be considered tight. If all your
 * weights are integers, you can safely set \c eps to zero.
 */
static igraph_error_t igraph_i_maximum_bipartite_matching_weighted(
        const igraph_t *graph,
        const igraph_vector_bool_t *types, igraph_integer_t *matching_size,
        igraph_real_t *matching_weight, igraph_vector_int_t *matching,
        const igraph_vector_t *weights, igraph_real_t eps) {
    igraph_integer_t i, j, k, n, no_of_nodes, no_of_edges;
    igraph_integer_t u, v, w, msize;
    igraph_t newgraph;
    igraph_vector_int_t match;       /* will store the matching */
    igraph_vector_t slack;            /* will store the slack on each edge */
    igraph_vector_int_t parent;           /* parent vertices during a BFS */
    igraph_vector_int_t vec1, vec2;       /* general temporary vectors */
    igraph_vector_t labels;           /* will store the labels */
    igraph_dqueue_int_t q;           /* a FIFO for BST */
    igraph_bool_t smaller_set_type;   /* denotes which part of the bipartite graph is smaller */
    igraph_vector_t smaller_set;      /* stores the vertex IDs of the smaller set */
    igraph_vector_t larger_set;       /* stores the vertex IDs of the larger set */
    igraph_integer_t smaller_set_size;        /* size of the smaller set */
    igraph_integer_t larger_set_size;         /* size of the larger set */
    igraph_real_t dual;               /* solution of the dual problem */
    IGRAPH_UNUSED(dual);              /* We mark it as unused to prevent warnings about unused-but-set-variables. */
    igraph_adjlist_t tight_phantom_edges; /* adjacency list to manage tight phantom edges */
    igraph_integer_t alternating_path_endpoint;
    igraph_vector_int_t* neis;
    igraph_vector_int_t *neis2;
    igraph_inclist_t inclist;         /* incidence list of the original graph */

    /* The Hungarian algorithm is originally for complete bipartite graphs.
     * For non-complete bipartite graphs, a phantom edge of weight zero must be
     * added between every pair of non-connected vertices. We don't do this
     * explicitly of course. See the comments below about how phantom edges
     * are taken into account. */

    no_of_nodes = igraph_vcount(graph);
    no_of_edges = igraph_ecount(graph);
    if (eps < 0) {
        IGRAPH_WARNING("negative epsilon given, clamping to zero");
        eps = 0;
    }

    /* (1) Initialize data structures */
    IGRAPH_CHECK(igraph_vector_int_init(&match, no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_int_destroy, &match);
    IGRAPH_CHECK(igraph_vector_init(&slack, no_of_edges));
    IGRAPH_FINALLY(igraph_vector_destroy, &slack);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&vec1, 0);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&vec2, 0);
    IGRAPH_VECTOR_INIT_FINALLY(&labels, no_of_nodes);
    IGRAPH_CHECK(igraph_dqueue_int_init(&q, 0));
    IGRAPH_FINALLY(igraph_dqueue_int_destroy, &q);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&parent, no_of_nodes);
    IGRAPH_CHECK(igraph_adjlist_init_empty(&tight_phantom_edges,
                                           no_of_nodes));
    IGRAPH_FINALLY(igraph_adjlist_destroy, &tight_phantom_edges);
    IGRAPH_CHECK(igraph_inclist_init(graph, &inclist, IGRAPH_ALL, IGRAPH_LOOPS_TWICE));
    IGRAPH_FINALLY(igraph_inclist_destroy, &inclist);
    IGRAPH_VECTOR_INIT_FINALLY(&smaller_set, 0);
    IGRAPH_VECTOR_INIT_FINALLY(&larger_set, 0);

    /* (2) Find which set is the smaller one */
    j = 0;
    for (i = 0; i < no_of_nodes; i++) {
        if (VECTOR(*types)[i] == 0) {
            j++;
        }
    }
    smaller_set_type = (j > no_of_nodes / 2);
    smaller_set_size = smaller_set_type ? (no_of_nodes - j) : j;
    larger_set_size = no_of_nodes - smaller_set_size;
    IGRAPH_CHECK(igraph_vector_reserve(&smaller_set, smaller_set_size));
    IGRAPH_CHECK(igraph_vector_reserve(&larger_set, larger_set_size));
    for (i = 0; i < no_of_nodes; i++) {
        if (VECTOR(*types)[i] == smaller_set_type) {
            IGRAPH_CHECK(igraph_vector_push_back(&smaller_set, i));
        } else {
            IGRAPH_CHECK(igraph_vector_push_back(&larger_set, i));
        }
    }

    /* (3) Calculate the initial labeling and the set of tight edges. Use the
     *     smaller set only. Here we can assume that there are no phantom edges
     *     among the tight ones. */
    dual = 0;
    for (i = 0; i < no_of_nodes; i++) {
        igraph_real_t max_weight = 0;

        if (VECTOR(*types)[i] != smaller_set_type) {
            VECTOR(labels)[i] = 0;
            continue;
        }

        neis = igraph_inclist_get(&inclist, i);
        n = igraph_vector_int_size(neis);
        for (j = 0, k = 0; j < n; j++) {
            k = VECTOR(*neis)[j];
            u = IGRAPH_OTHER(graph, k, i);
            if (VECTOR(*types)[u] == VECTOR(*types)[i]) {
                IGRAPH_ERROR("Graph is not bipartite with supplied types vector", IGRAPH_EINVAL);
            }
            if (VECTOR(*weights)[k] > max_weight) {
                max_weight = VECTOR(*weights)[k];
            }
        }

        VECTOR(labels)[i] = max_weight;
        dual += max_weight;
    }

    igraph_vector_int_clear(&vec1);
    IGRAPH_CHECK(igraph_get_edgelist(graph, &vec2, 0));
#define IS_TIGHT(i) (VECTOR(slack)[i] <= eps)
    for (i = 0, j = 0; i < no_of_edges; i++, j += 2) {
        u = VECTOR(vec2)[j];
        v = VECTOR(vec2)[j + 1];
        VECTOR(slack)[i] = VECTOR(labels)[u] + VECTOR(labels)[v] - VECTOR(*weights)[i];
        if (IS_TIGHT(i)) {
            IGRAPH_CHECK(igraph_vector_int_push_back(&vec1, u));
            IGRAPH_CHECK(igraph_vector_int_push_back(&vec1, v));
        }
    }
    igraph_vector_int_clear(&vec2);

    /* (4) Construct a temporary graph on which the initial maximum matching
     *     will be calculated (only on the subset of tight edges) */
    IGRAPH_CHECK(igraph_create(&newgraph, &vec1,
                               no_of_nodes, 0));
    IGRAPH_FINALLY(igraph_destroy, &newgraph);
    IGRAPH_CHECK(igraph_maximum_bipartite_matching(&newgraph, types, &msize, 0, &match, 0, 0));
    igraph_destroy(&newgraph);
    IGRAPH_FINALLY_CLEAN(1);

    /* (5) Main loop until the matching becomes maximal */
    while (msize < smaller_set_size) {
        igraph_real_t min_slack, min_slack_2;
        igraph_integer_t min_slack_u, min_slack_v;

        /* mark min_slack_u as unused; it is actually used when debugging, but
         * gcc complains when we are not debugging */
        IGRAPH_UNUSED(min_slack_u);

        /* (7) Fill the push queue with the unmatched nodes from the smaller set. */
        igraph_vector_int_clear(&vec1);
        igraph_vector_int_clear(&vec2);
        igraph_vector_int_fill(&parent, -1);
        for (j = 0; j < smaller_set_size; j++) {
            i = VECTOR(smaller_set)[j];
            if (UNMATCHED(i)) {
                IGRAPH_CHECK(igraph_dqueue_int_push(&q, i));
                VECTOR(parent)[i] = i;
                IGRAPH_CHECK(igraph_vector_int_push_back(&vec1, i));
            }
        }

#ifdef MATCHING_DEBUG
        debug("Matching:");
        igraph_vector_int_print(&match);
        debug("Unmatched vertices are marked by non-negative numbers:\n");
        igraph_vector_print(&parent);
        debug("Labeling:");
        igraph_vector_print(&labels);
        debug("Slacks:");
        igraph_vector_print(&slack);
#endif

        /* (8) Run the BFS */
        alternating_path_endpoint = -1;
        while (!igraph_dqueue_int_empty(&q)) {
            v = igraph_dqueue_int_pop(&q);

            debug("Considering vertex %ld\n", v);

            /* v is always in the smaller set. Find the neighbors of v, which
             * are all in the larger set. Find the pairs of these nodes in
             * the smaller set and push them to the queue. Mark the traversed
             * nodes as seen.
             *
             * Here we have to be careful as there are two types of incident
             * edges on v: real edges and phantom ones. Real edges are
             * given by igraph_inclist_get. Phantom edges are not given so we
             * (ab)use an adjacency list data structure that lists the
             * vertices connected to v by phantom edges only. */
            neis = igraph_inclist_get(&inclist, v);
            n = igraph_vector_int_size(neis);
            for (i = 0; i < n; i++) {
                j = VECTOR(*neis)[i];
                /* We only care about tight edges */
                if (!IS_TIGHT(j)) {
                    continue;
                }
                /* Have we seen the other endpoint already? */
                u = IGRAPH_OTHER(graph, j, v);
                if (VECTOR(parent)[u] >= 0) {
                    continue;
                }
                debug("  Reached vertex %" IGRAPH_PRId " via edge %" IGRAPH_PRId "\n", u, j);
                VECTOR(parent)[u] = v;
                IGRAPH_CHECK(igraph_vector_int_push_back(&vec2, u));
                w = VECTOR(match)[u];
                if (w == -1) {
                    /* u is unmatched and it is in the larger set. Therefore, we
                     * could improve the matching by following the parents back
                     * from u to the root.
                     */
                    alternating_path_endpoint = u;
                    break;  /* since we don't need any more endpoints that come from v */
                } else {
                    IGRAPH_CHECK(igraph_dqueue_int_push(&q, w));
                    VECTOR(parent)[w] = u;
                }
                IGRAPH_CHECK(igraph_vector_int_push_back(&vec1, w));
            }

            /* Now do the same with the phantom edges */
            neis2 = igraph_adjlist_get(&tight_phantom_edges, v);
            n = igraph_vector_int_size(neis2);
            for (i = 0; i < n; i++) {
                u = VECTOR(*neis2)[i];
                /* Have we seen u already? */
                if (VECTOR(parent)[u] >= 0) {
                    continue;
                }
                /* Check if the edge is really tight; it might have happened that the
                 * edge became non-tight in the meanwhile. We do not remove these from
                 * tight_phantom_edges at the moment, so we check them once again here.
                 */
                if (fabs(VECTOR(labels)[v] + VECTOR(labels)[u]) > eps) {
                    continue;
                }
                debug("  Reached vertex %" IGRAPH_PRId " via tight phantom edge\n", u);
                VECTOR(parent)[u] = v;
                IGRAPH_CHECK(igraph_vector_int_push_back(&vec2, u));
                w = VECTOR(match)[u];
                if (w == -1) {
                    /* u is unmatched and it is in the larger set. Therefore, we
                     * could improve the matching by following the parents back
                     * from u to the root.
                     */
                    alternating_path_endpoint = u;
                    break;  /* since we don't need any more endpoints that come from v */
                } else {
                    IGRAPH_CHECK(igraph_dqueue_int_push(&q, w));
                    VECTOR(parent)[w] = u;
                }
                IGRAPH_CHECK(igraph_vector_int_push_back(&vec1, w));
            }
        }

        /* Okay; did we have an alternating path? */
        if (alternating_path_endpoint != -1) {
#ifdef MATCHING_DEBUG
            debug("BFS parent tree:");
            igraph_vector_print(&parent);
#endif
            /* Increase the size of the matching with the alternating path. */
            v = alternating_path_endpoint;
            u = VECTOR(parent)[v];
            debug("Extending matching with alternating path ending in %ld.\n", v);

            while (u != v) {
                w = VECTOR(match)[v];
                if (w != -1) {
                    VECTOR(match)[w] = -1;
                }
                VECTOR(match)[v] = u;

                VECTOR(match)[v] = u;
                w = VECTOR(match)[u];
                if (w != -1) {
                    VECTOR(match)[w] = -1;
                }
                VECTOR(match)[u] = v;

                v = VECTOR(parent)[u];
                u = VECTOR(parent)[v];
            }

            msize++;

#ifdef MATCHING_DEBUG
            debug("New matching after update:");
            igraph_vector_int_print(&match);
            debug("Matching size is now: %" IGRAPH_PRId "\n", msize);
#endif
            continue;
        }

#ifdef MATCHING_DEBUG
        debug("Vertices reachable from unmatched ones via tight edges:\n");
        igraph_vector_int_print(&vec1);
        igraph_vector_print(&vec2);
#endif

        /* At this point, vec1 contains the nodes in the smaller set (A)
         * reachable from unmatched nodes in A via tight edges only, while vec2
         * contains the nodes in the larger set (B) reachable from unmatched
         * nodes in A via tight edges only. Also, parent[i] >= 0 if node i
         * is reachable */

        /* Check the edges between reachable nodes in A and unreachable
         * nodes in B, and find the minimum slack on them.
         *
         * Since the weights are positive, we do no harm if we first
         * assume that there are no "real" edges between the two sets
         * mentioned above and determine an upper bound for min_slack
         * based on this. */
        min_slack = IGRAPH_INFINITY;
        min_slack_u = min_slack_v = 0;
        n = igraph_vector_int_size(&vec1);
        for (j = 0; j < larger_set_size; j++) {
            i = VECTOR(larger_set)[j];
            if (VECTOR(labels)[i] < min_slack) {
                min_slack = VECTOR(labels)[i];
                min_slack_v = i;
            }
        }
        min_slack_2 = IGRAPH_INFINITY;
        for (i = 0; i < n; i++) {
            u = VECTOR(vec1)[i];
            /* u is surely from the smaller set, but we are interested in it
             * only if it is reachable from an unmatched vertex */
            if (VECTOR(parent)[u] < 0) {
                continue;
            }
            if (VECTOR(labels)[u] < min_slack_2) {
                min_slack_2 = VECTOR(labels)[u];
                min_slack_u = u;
            }
        }
        min_slack += min_slack_2;
        debug("Starting approximation for min_slack = %.4f (based on vertex pair %ld--%ld)\n",
              min_slack, min_slack_u, min_slack_v);

        n = igraph_vector_int_size(&vec1);
        for (i = 0; i < n; i++) {
            u = VECTOR(vec1)[i];
            /* u is a reachable node in A; get its incident edges.
             *
             * There are two types of incident edges: 1) real edges,
             * 2) phantom edges. Phantom edges were treated earlier
             * when we determined the initial value for min_slack. */
            debug("Trying to expand along vertex %" IGRAPH_PRId "\n", u);
            neis = igraph_inclist_get(&inclist, u);
            k = igraph_vector_int_size(neis);
            for (j = 0; j < k; j++) {
                /* v is the vertex sitting at the other end of an edge incident
                 * on u; check whether it was reached */
                v = IGRAPH_OTHER(graph, VECTOR(*neis)[j], u);
                debug("  Edge %" IGRAPH_PRId " -- %" IGRAPH_PRId " (ID=%" IGRAPH_PRId ")\n", u, v, VECTOR(*neis)[j]);
                if (VECTOR(parent)[v] >= 0) {
                    /* v was reached, so we are not interested in it */
                    debug("    %" IGRAPH_PRId " was reached, so we are not interested in it\n", v);
                    continue;
                }
                /* v is the ID of the edge from now on */
                v = VECTOR(*neis)[j];
                if (VECTOR(slack)[v] < min_slack) {
                    min_slack = VECTOR(slack)[v];
                    min_slack_u = u;
                    min_slack_v = IGRAPH_OTHER(graph, v, u);
                }
                debug("    Slack of this edge: %.4f, min slack is now: %.4f\n",
                      VECTOR(slack)[v], min_slack);
            }
        }
        debug("Minimum slack: %.4f on edge %" IGRAPH_PRId "--%" IGRAPH_PRId "\n", min_slack, min_slack_u, min_slack_v);

        if (min_slack > 0) {
            /* Decrease the label of reachable nodes in A by min_slack.
             * Also update the dual solution */
            n = igraph_vector_int_size(&vec1);
            for (i = 0; i < n; i++) {
                u = VECTOR(vec1)[i];
                VECTOR(labels)[u] -= min_slack;
                neis = igraph_inclist_get(&inclist, u);
                k = igraph_vector_int_size(neis);
                for (j = 0; j < k; j++) {
                    debug("  Decreasing slack of edge %" IGRAPH_PRId " (%" IGRAPH_PRId "--%" IGRAPH_PRId ") by %.4f\n",
                          VECTOR(*neis)[j], u,
                          IGRAPH_OTHER(graph, VECTOR(*neis)[j], u), min_slack);
                    VECTOR(slack)[VECTOR(*neis)[j]] -= min_slack;
                }
                dual -= min_slack;
            }

            /* Increase the label of reachable nodes in B by min_slack.
             * Also update the dual solution */
            n = igraph_vector_int_size(&vec2);
            for (i = 0; i < n; i++) {
                u = VECTOR(vec2)[i];
                VECTOR(labels)[u] += min_slack;
                neis = igraph_inclist_get(&inclist, u);
                k = igraph_vector_int_size(neis);
                for (j = 0; j < k; j++) {
                    debug("  Increasing slack of edge %" IGRAPH_PRId " (%" IGRAPH_PRId"--%" IGRAPH_PRId ") by %.4f\n",
                          VECTOR(*neis)[j], u,
                          IGRAPH_OTHER(graph, VECTOR(*neis)[j], u), min_slack);
                    VECTOR(slack)[VECTOR(*neis)[j]] += min_slack;
                }
                dual += min_slack;
            }
        }

        /* Update the set of tight phantom edges.
         * Note that we must do it even if min_slack is zero; the reason is that
         * it can happen that min_slack is zero in the first step if there are
         * isolated nodes in the input graph.
         *
         * TODO: this is O(n^2) here. Can we do it faster? */
        for (i = 0; i < smaller_set_size; i++) {
            u = VECTOR(smaller_set)[i];
            for (j = 0; j < larger_set_size; j++) {
                v = VECTOR(larger_set)[j];
                if (VECTOR(labels)[u] + VECTOR(labels)[v] <= eps) {
                    /* Tight phantom edge found. Note that we don't have to check whether
                     * u and v are connected; if they were, then the slack of this edge
                     * would be negative. */
                    neis2 = igraph_adjlist_get(&tight_phantom_edges, u);
                    if (!igraph_vector_int_binsearch(neis2, v, &k)) {
                        debug("New tight phantom edge: %" IGRAPH_PRId " -- %" IGRAPH_PRId "\n", u, v);
                        IGRAPH_CHECK(igraph_vector_int_insert(neis2, k, v));
                    }
                }
            }
        }

#ifdef MATCHING_DEBUG
        debug("New labels:");
        igraph_vector_print(&labels);
        debug("Slacks after updating with min_slack:");
        igraph_vector_print(&slack);
#endif
    }

    /* Cleanup: remove phantom edges from the matching */
    for (i = 0; i < smaller_set_size; i++) {
        u = VECTOR(smaller_set)[i];
        v = VECTOR(match)[u];
        if (v != -1) {
            neis2 = igraph_adjlist_get(&tight_phantom_edges, u);
            if (igraph_vector_int_binsearch(neis2, v, 0)) {
                VECTOR(match)[u] = VECTOR(match)[v] = -1;
                msize--;
            }
        }
    }

    /* Fill the output parameters */
    if (matching != 0) {
        IGRAPH_CHECK(igraph_vector_int_update(matching, &match));
    }
    if (matching_size != 0) {
        *matching_size = msize;
    }
    if (matching_weight != 0) {
        *matching_weight = 0;
        for (i = 0; i < no_of_edges; i++) {
            if (IS_TIGHT(i)) {
                IGRAPH_CHECK(igraph_edge(graph, i, &u, &v));
                if (VECTOR(match)[u] == v) {
                    *matching_weight += VECTOR(*weights)[i];
                }
            }
        }
    }

    /* Release everything */
#undef IS_TIGHT
    igraph_vector_destroy(&larger_set);
    igraph_vector_destroy(&smaller_set);
    igraph_inclist_destroy(&inclist);
    igraph_adjlist_destroy(&tight_phantom_edges);
    igraph_vector_int_destroy(&parent);
    igraph_dqueue_int_destroy(&q);
    igraph_vector_destroy(&labels);
    igraph_vector_int_destroy(&vec1);
    igraph_vector_int_destroy(&vec2);
    igraph_vector_destroy(&slack);
    igraph_vector_int_destroy(&match);
    IGRAPH_FINALLY_CLEAN(11);

    return IGRAPH_SUCCESS;
}




static igraph_error_t igraph_i_maximum_matching_unweighted(
        const igraph_t *graph,
        igraph_integer_t *matching_size,
        igraph_vector_int_t *matching);

static igraph_error_t igraph_i_maximum_matching_unweighted_bloss_aug(igraph_integer_t w1, igraph_integer_t w2, igraph_t *graph, igraph_vector_int_t *even_level, igraph_vector_int_t *odd_level,
    igraph_vector_bool_t *visited_nodes, igraph_vector_bool_t *visited_edges, igraph_vector_bool_t *used_edges, igraph_vector_bool_t *erased_nodes,
    igraph_integer_t no_of_nodes, igraph_vector_int_t *blossom, igraph_vector_int_list_t *predecessors, igraph_vector_int_list_t *unused_ancestors, igraph_vector_int_list_t *unvisited_ancestors,
    igraph_vector_int_list_t *anomalies, igraph_vector_int_list_t *bridges, igraph_vector_int_t *match);

/**
 * \function igraph_maximum_matching
 * \brief Calculates a maximum matching in a graph.
 *
 * A matching in a graph is a set of edges such that no endpoints are shared.
 * The size (or cardinality) of a matching is the number of edges.
 * A matching is a maximum matching if there exists no other matching with
 * larger cardinality. For weighted graphs, a maximum matching is a matching
 * whose edges have the largest possible total weight among all possible
 * matchings.
 *
 * </para><para>
 * Currently maximum weight matchings are not supported.
 *
 * </para><para>
 * Maximum matchings in bipartite graphs are found by the push-relabel algorithm
 * with greedy initialization and a global relabeling after every n/2 steps where
 * n is the number of vertices in the graph.
 *
 * </para><para>
 * References: Gabow H: The Weighted Matching Approach to Maximum Cardinality Matching.
 * Fundamenta Informaticae, 2017.
 *
 * \param graph The input graph. It can be directed but the edge directions
 *              will be ignored.
 * \param matching_size The size of the matching (i.e. the number of matched
 *                      vertex pairs will be returned here). It may be \c NULL
 *                      if you don't need this.
 * \param matching_weight The weight of the matching if the edges are weighted,
 *                        or the size of the matching again if the edges are
 *                        unweighted. It may be \c NULL if you don't need this.
 * \param matching The matching itself. It must be a vector where element i
 *                 contains the ID of the vertex that vertex i is matched to,
 *                 or -1 if vertex i is unmatched.
 * \param weights A null pointer (=no edge weights), or a vector giving the
 *                weights of the edges. Currently ignored.
 * \return Error code.
 *
 * Time complexity: O(sqrt(|V|) |E|) for unweighted graphs (according to Gabow(2017)).
 *
 * \example TODO: make an example
 */
igraph_error_t igraph_maximum_matching(const igraph_t *graph, igraph_integer_t *matching_size,
                            igraph_real_t *matching_weight, igraph_vector_int_t *matching,
                            const igraph_vector_t *weights) {
    // TODO: add eps to function declaration?
    // TODO: do boring input validity test or whatever
    // TODO: make dependent on if weights are given
    // TODO: figure out function signature better
    igraph_i_maximum_matching_unweighted(graph, matching_size, matching);


    return IGRAPH_SUCCESS;
}


static igraph_error_t igraph_i_maximum_matching_unweighted(const igraph_t *graph, igraph_integer_t *matching_size, igraph_vector_int_t *matching) {
    /* Data Structures
     ***********************
     * collection of sets bridges(i), from which one bridge is pulled at a time, from the current phase set.
     * Implement as array of queues?
     *
     * even level and odd level vectors associated with vertices
     *
     * predecessors and anomalies of each vertex, implement as vector for each vertex, list of vectors
     *
     * left and right markings for DDPS, integer or boolean vector will suffice
     *
     * erased vector for vertices that have already been in a augmenting path at this length, or have no remaining predecessors after those are removed.
     *
     * incremental tree set union of Gabow and Tarjan to track the blossom base* of a given vertex.
     * base* is the base of the blossom a vertex is in or, if that blossom is embedded in another, the base of that blossom, continuing until we are not in an embedded blossom.
     *
     * arrays holding the peaks and base of each blossom
     */


    /* Data Structures in this section
     *********************************
     * even/odd level arrays of length n, -1 taken as infinity
     *
     * TODO figure out these two set representations
     * predecessors and anomalies n length list of arrays of length n
     *
     * bridges as a list of vectors length n, each list length 2n? - igraph_vector_push_back igraph_vector_pop_back
     *
     * level_i_queue and next_level_i_queue
     *
     * Blossom array of length n
     *
     * TODO figure out other blossom representation
     */

    // NOTE use IGRAPH_CHECK() macro

    igraph_integer_t i,j,u,v,temp;
    igraph_vector_int_t even_level;
    igraph_vector_int_t odd_level;
    igraph_vector_bool_t visited_nodes;
    igraph_vector_bool_t visited_edges;
    igraph_vector_bool_t used_edges;
    igraph_vector_bool_t erased_nodes;
    igraph_integer_t no_of_nodes;
    igraph_vector_int_t blossom;
    igraph_vector_int_list_t predecessors;
    igraph_vector_int_list_t unused_ancestors;
    igraph_vector_int_list_t unvisited_ancestors;
    igraph_vector_int_list_t anomalies;
    igraph_vector_int_list_t bridges;
    igraph_vector_int_t match;
    igraph_vector_int_t neighbors;
    igraph_dqueue_int_t level_i_queue;
    igraph_dqueue_int_t next_level_i_queue;


    igraph_vs_t all_selector; // TODO: is this needed?

    // initialize data structures
    no_of_nodes = igraph_vcount(graph);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&even_level, no_of_nodes);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&odd_level, no_of_nodes);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&blossom, no_of_nodes);
    //IGRAPH_VECTOR_BOOL_INIT_FINALLY(&visited_nodes, no_of_nodes);
    IGRAPH_CHECK(igraph_vector_int_list_init(&unused_ancestors, no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_int_list_destroy, &unused_ancestors);
    IGRAPH_CHECK(igraph_vector_int_list_init(&unvisited_ancestors, no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_int_list_destroy, &unvisited_ancestors);
    IGRAPH_VECTOR_BOOL_INIT_FINALLY(&visited_edges, no_of_nodes); //TODO: make into matrix?
    IGRAPH_VECTOR_BOOL_INIT_FINALLY(&used_edges, no_of_nodes); //TODO: make into matrix?
    //unused ancestor list?
    //unvistied predecessor list?
    IGRAPH_VECTOR_BOOL_INIT_FINALLY(&erased_nodes, no_of_nodes);
    IGRAPH_CHECK(igraph_vector_int_list_init(&predecessors, no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_int_list_destroy, &predecessors);
    IGRAPH_CHECK(igraph_vector_int_list_init(&anomalies, no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_int_list_destroy, &anomalies);
    IGRAPH_CHECK(igraph_vector_int_list_init(&bridges, 2*no_of_nodes));
    IGRAPH_FINALLY(igraph_vector_int_list_destroy, &bridges);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&match, no_of_nodes);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&neighbors, 0);
    // TODO: decide if all these need to be this size
    IGRAPH_CHECK(igraph_dqueue_int_init(&level_i_queue, no_of_nodes));
    IGRAPH_FINALLY(igraph_dqueue_int_destroy, &level_i_queue);
    IGRAPH_CHECK(igraph_dqueue_int_init(&next_level_i_queue, no_of_nodes));
    IGRAPH_FINALLY(igraph_dqueue_int_destroy, &next_level_i_queue);

    // TODO: reserve memory for vector lists?

    // Search(graph)
    //******************************************
    // (0) Initialize loop variables
    igraph_bool_t has_vertices = 1;
    if (no_of_nodes == 0) {
        has_vertices = 0;
    }
    igraph_vector_int_fill(&match, -1);

    while (has_vertices) {
        /* remove when finished with algorithm
        igraph_vit_t iterator; // does this need to go at the top?
        igraph_vit_create(graph,all_selector,&iterator);
        while (!IGRAPH_VIT_END(iterator)) {
            igraph_integer_t vertex = IGRAPH_VIT_GET(iterator);
            // predecessors(v) = empty set
            // anomalies(v) = empty set
            // v marked unvisited
            IGRAPH_VIT_NEXT(iterator);
        }
        */
        // Beginning of a phase

        // for each vertex v
        // evenlevel(v) = infinity
        igraph_vector_int_fill(&even_level, -1);
        // oddlevel(v) = infinity
        igraph_vector_int_fill(&odd_level, -1);
        // blossom(v) = undefined
        igraph_vector_int_fill(&blossom, -1);
        // predecessors(v) = empty set
        for (j=0; j<no_of_nodes; j++) {
            igraph_vector_int_clear(igraph_vector_int_list_get_ptr(&predecessors,j));
            igraph_vector_int_clear(igraph_vector_int_list_get_ptr(&unused_ancestors,j));
            igraph_vector_int_clear(igraph_vector_int_list_get_ptr(&unvisited_ancestors,j));
        }
        // anomalies(v) = empty set
        for (j=0; j<no_of_nodes; j++) {
            igraph_vector_int_clear(igraph_vector_int_list_get_ptr(&anomalies,j));
        }
        // v marked unvisited
        igraph_vector_bool_fill(&visited_nodes,0);

        // all edges marked unused and unvisited
        igraph_vector_bool_fill(&visited_edges,0);
        igraph_vector_bool_fill(&used_edges,0);
        // mark all nodes not erased
        igraph_vector_bool_fill(&erased_nodes,0);

        // bridges(j) = empty set for all j
        for (j=0; j<no_of_nodes; j++) {
            igraph_vector_int_clear(igraph_vector_int_list_get_ptr(&bridges,j));
        }

        // i = -1
        i = -1;

        // for each exposed vertex v, even_level = 0
        // for each vertex
        for (j=0; j<no_of_nodes; j++){
            // if not matched
            if (VECTOR(match)[j] == -1) {
                // even_level = 0;
                VECTOR(even_level)[j] = 0;
                // place into level_i_queue
                igraph_dqueue_int_push(&level_i_queue, j);
            }

        }

        // (2)
        igraph_bool_t augmented = 0;
        while (!augmented && has_vertices) {
            // i = i+1
            i++;
            // if i is even
            if (i%2 == 0) {
                // for each v with evenlevel(v) = i find its unmatched, unused neighbors.
                // TODO: unused condition
                while (!igraph_dqueue_int_empty(&level_i_queue)) {
                    v = igraph_dqueue_int_pop(&level_i_queue);
                    // for each neighbor u
                    igraph_neighbors(graph, &neighbors, v, IGRAPH_ALL);
                    for (j=0;j<igraph_vector_int_size(&neighbors);j++) {
                        u = VECTOR(neighbors)[j];
                        // if evenlevel(u) is finite
                        if (VECTOR(even_level)[u] != -1) {
                            // temp = (evenlevel(u) + evenlevel(v))/2
                            temp = (VECTOR(even_level)[u] + VECTOR(even_level)[v]) / 2;
                            // add (u,v) to bridges(temp)
                            igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&bridges,temp), u);
                            igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&bridges,temp), v);
                        }
                        else {
                            // handle oddlevel
                            // if oddlevel(u) == infinity
                            if (VECTOR(odd_level)[u] == -1) {
                                // oddlevel(u) = i+1
                                VECTOR(odd_level)[u] = i+1;
                                igraph_dqueue_int_push(&next_level_i_queue, u);
                            }
                            // handle predecessors
                            // if oddlevel(u) == i+1
                            if (VECTOR(odd_level)[u] == i+1) {
                                // add v to predecessors(u)
                                igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&predecessors,u), v);
                                igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&unused_ancestors,u), v);
                                igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&unvisited_ancestors,u), v);
                            }
                            // handle anomalies
                            if (VECTOR(odd_level)[u] < i) {
                                // add v to anomalies(u)
                                igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&anomalies,u), v);
                            }
                        }
                    }
                }
            }
            // if i is odd
            else {
                // for each v with odd_level(v) = i and v not in any blossom take its matched neighbor u
                // TODO: blossom condition
                while (!igraph_dqueue_int_empty(&level_i_queue)) {
                    v = igraph_dqueue_int_pop(&level_i_queue);
                    u = VECTOR(match)[v]; // BUG: can u be -1?
                    // handle bridges
                    // if odd_level(u) = i
                    if (VECTOR(odd_level)[u] == i) {
                        // temp = (odd_level(u)+odd_level(v))/2
                        temp = (VECTOR(odd_level)[u] + VECTOR(odd_level)[v]) / 2;
                        // add (u,v) to bridges(temp)
                        igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&bridges,temp), u);
                        igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&bridges,temp), v);
                    }
                    //  handle predecessors
                    //  if oddlevel(u) = infinity
                    if (VECTOR(odd_level)[u] == -1) {
                        // even_level(u) = i+1
                        VECTOR(even_level)[u] = i+1;
                        igraph_dqueue_int_push(&next_level_i_queue, u);
                        // predecessors(u) = {v}
                        igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&predecessors,u), v);
                        igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&unused_ancestors,u), v);
                        igraph_vector_int_push_back(igraph_vector_int_list_get_ptr(&unvisited_ancestors,u), v);
                    }
                }

            }
#ifdef MATCHING_DEBUG
                debug("bridges(i):");
                igraph_vector_int_print(igraph_vector_int_list_get_ptr(&bridges, i));
#endif
            // for each edge (u,v) in bridges(i) call bloss_aug(u,v)
            for (j=0; j<igraph_vector_int_size(igraph_vector_int_list_get_ptr(&bridges, i));j++) {
                // TODO: call bloss_aug
            }

            igraph_dqueue_int_t temp = level_i_queue;
            level_i_queue = next_level_i_queue;
            next_level_i_queue = temp;
            // if no more vertices have level i then halt

            if (igraph_dqueue_int_size(&level_i_queue) == 0) {
                has_vertices = 0;
            }
        }

        //igraph_vit_destroy(&iterator);
    }

    // compute matching size
    *matching_size = 0;

    igraph_vector_int_destroy(&even_level);
    igraph_vector_int_destroy(&odd_level);
    igraph_vector_int_destroy(&blossom);
    igraph_vector_bool_destroy(&visited_nodes);
    igraph_vector_bool_destroy(&visited_edges);
    igraph_vector_bool_destroy(&used_edges);
    igraph_vector_bool_destroy(&erased_nodes);
    igraph_vector_int_list_destroy(&predecessors);
    igraph_vector_int_list_destroy(&anomalies);
    igraph_vector_int_list_destroy(&bridges);
    igraph_vector_int_destroy(&match);
    igraph_vector_int_destroy(&neighbors);
    igraph_dqueue_int_destroy(&level_i_queue);
    igraph_dqueue_int_destroy(&next_level_i_queue);
    IGRAPH_FINALLY_CLEAN(14);


    return IGRAPH_SUCCESS;
}

//TODO: tidy up parameters
static igraph_error_t igraph_i_maximum_matching_unweighted_bloss_aug(igraph_integer_t w1, igraph_integer_t w2, igraph_t *graph, igraph_vector_int_t *even_level, igraph_vector_int_t *odd_level,
    igraph_vector_bool_t *visited_nodes, igraph_vector_bool_t *visited_edges, igraph_vector_bool_t *used_edges, igraph_vector_bool_t *erased_nodes,
    igraph_integer_t no_of_nodes, igraph_vector_int_t *blossom, igraph_vector_int_list_t *predecessors,igraph_vector_int_list_t *unused_ancestors, igraph_vector_int_list_t *unvisited_ancestors,
    igraph_vector_int_list_t *anomalies, igraph_vector_int_list_t *bridges, igraph_vector_int_t *match) {

    // BlossAug(w1, w2) w1, w2 are vertices - return augmenting path, if empty none was found
    //
    //******************************************
    // TODO: Check if w1 and w2 are in same blossom, exit if yes
    //
    // Initialization

    // grown_back is flagged when the tree grows, backtracks, or fails to grow along a specific edge
    igraph_bool_t pathFound, blossomFound, erased, grown_back, has_ancestors;
    igraph_integer_t dcv, barrier, vl, vr, level_vl, level_vr, u;
    igraph_vector_bool_t left;
    igraph_vector_bool_t right;
    igraph_vector_int_t father;
    igraph_vector_int_t *ancestors;

    //TODO: decide vl and vr using blossom info
    vl = w1;
    vr = w2;

    pathFound = false;
    blossomFound = false;
    erased = false;
    dcv = -1;
    barrier = vr;
    IGRAPH_VECTOR_BOOL_INIT_FINALLY(&left, no_of_nodes);
    IGRAPH_VECTOR_BOOL_INIT_FINALLY(&right, no_of_nodes);
    IGRAPH_VECTOR_INT_INIT_FINALLY(&father, no_of_nodes);
    igraph_vector_bool_fill(&left, 0);
    igraph_vector_bool_fill(&right, 0);
    igraph_vector_int_fill(&father, -1);

    // check if path discovered, if yes flag path found
    if (VECTOR(*even_level)[vl] == 0 && VECTOR(*even_level)[vr] == 0) {
        pathFound = true;
    }
    // while path not found and blossom not found and not erased
    while (!pathFound && !blossomFound && !erased) {
        // get level of vl and vr
        level_vl = VECTOR(*even_level)[vl] <= VECTOR(*odd_level)[vl] ? VECTOR(*even_level)[vl] : VECTOR(*odd_level)[vl];
        level_vl = VECTOR(*even_level)[vl] == -1 ? VECTOR(*odd_level)[vl] : level_vl;
        level_vl = VECTOR(*odd_level)[vl] == -1 ? VECTOR(*even_level)[vl] : level_vl;

        level_vr = VECTOR(*even_level)[vr] <= VECTOR(*odd_level)[vr] ? VECTOR(*even_level)[vr] : VECTOR(*odd_level)[vr];
        level_vr = VECTOR(*even_level)[vr] == -1 ? VECTOR(*odd_level)[vr] : level_vr;
        level_vr = VECTOR(*odd_level)[vr] == -1 ? VECTOR(*even_level)[vr] : level_vr;

        //TODO: error if one is -1?

        // if left tree should grow
        if (level_vl >= level_vr) {
            // init flags grown/backtracked, has ancestors
            grown_back = false;
            has_ancestors = true;
            // if vl has no unused ancestors
            // TODO: is this even correct? do i need to create an ancestors array in initial search?
            ancestors = igraph_vector_int_list_get_ptr(unused_ancestors, vl); // TODO: make copy instead?
            if (igraph_vector_int_empty(ancestors)) {
                // flag no ancestors
                has_ancestors = false;
                // if f(vl) is undefined
                if (VECTOR(father)[vl] == -1) {
                    // flag blossom found
                    blossomFound = true;
                }
                // else backtrack
                else {
                    // vl = f(vl)
                    vl = VECTOR(father)[vl];
                }
            }
            // while left tree hasn't grown/backtraced and vl has unused ancestors and not erased and blossom not found
            while (!grown_back && has_ancestors && !erased && !blossomFound) {
                // choose unused ancestor edge (vl,u)
                u = igraph_vector_int_pop_back(ancestors);
                // if u is marked erased
                if (VECTOR(*erased_nodes)[u]) {
                    // delete u from predecessors(vl)
                    // if vl has no unused ancestors
                    if (igraph_vector_int_empty(ancestors)) {
                        // flag no ancestors
                        has_ancestors = false;
                        // if vl==w1
                        if (vl == w1) {
                        // flag erased
                            erased = true;
                        }
                        // 	else
                        else {
                            // TODO: mark vl erased
                            // vl = f(vl)
                            vl = VECTOR(father)[vl];
                            // flag grown/backtracked
                            grown_back = true;
                        }
                    }
                }
                else {
                    // TODO: mark (vl,u) used
                    //
                    // if u is in a blossom B
                    if (VECTOR(*blossom)[u] != -1) {
                        // TODO: u = base*(B)
                    }
                    // if u is unmarked 	// grow left tree
                    if (!VECTOR(left)[u] && !VECTOR(right)[u]) {
                        // mark u "left"
                        VECTOR(left)[u] = true;
                        // f(u) = vl
                        VECTOR(father)[u] = vl;
                        // vl = u
                        vl = u;
                        // flag has grown/backtracked
                        grown_back = true;
                    }
                    // else
                    else {
                        // if u == barrier or u == vr //left tree can't grow this way
                        if (u == barrier || u == vr) {
                            // flag grown/backtracked
                            grown_back = true;
                        }
                        // else steal from right tree
                        else {
                            // mark u "left"
                            VECTOR(left)[u] = true;
                            // vr = f(vr)
                            vr = VECTOR(father)[vr];
                            // vl = u
                            vl = u;
                            // DCV = u
                            dcv = u;
                            // flag has grown/backtracked
                            grown_back = true;
                        }
                    }
                }
            }
        }
        // 	else right tree should grow
        else {
            // init flags grown/backtracked, has ancestors
            grown_back = false;
            has_ancestors = true;
            // if vr has no unused ancestors
            ancestors = igraph_vector_int_list_get_ptr(predecessors, vr);
            if (igraph_vector_int_empty(ancestors)) {
                // flag no ancestors
                has_ancestors = false;
                // if vr == barrier then steal from left tree
                if (vr == barrier) {
                    // vr = DCV
                    vr = dcv;
                    // barrier = DCV
                    barrier = dcv;
                    // mark vr "right"
                    VECTOR(left)[vr] = false;
                    VECTOR(right)[vr] = true;
                    // vl = f(vl)
                    vl = VECTOR(father)[vl];
                }
                // else backtrack
                else {
                    // vr = f(vr)
                    vr = VECTOR(father)[vr];
                }
            }
            // while right tree hasn't grown/backtracked/intersect and vr has unused ancestors and not erased
            while (!grown_back && has_ancestors && !erased && !blossomFound) {
                // choose unused ancestor edge (vr,u)
                u = igraph_vector_int_pop_back(ancestors);
                // if u is marked erased
                if (VECTOR(*erased_nodes)[u]) {
                    // TODO: delete u from predecessors(vr)
                    //
                    // if vr has no unused ancestor
                    if (igraph_vector_int_empty(ancestors)) {
                        // 					flag no ancestors
                        has_ancestors = false;
                        // if vr==w2
                        if (vr == w2) {
                            // flag erased
                            erased = true;
                        }
                        else {
                            // mark vr erased
                            VECTOR(*erased_nodes)[vr] = true;
                            // vr = f(vr)
                            vr = VECTOR(father)[vr];
                            // flag grown/backtracked/intersect
                            grown_back = true;
                        }
                    }
                }
                else {
                    // TODO: mark (vr,u) used
                    //
                    // if u is in a blossom B
                    if (VECTOR(*blossom)[u] != -1) {
                        // TODO: u = base*(B)
                    }
                    // if u is unmarked
                    if (!VECTOR(left)[u] && !VECTOR(right)[u]) {
                        // mark u "right"
                        VECTOR(right)[u] = true;
                        // f(u) = vr
                        VECTOR(father)[u] = vr;
                        // vr = u
                        vr = u;
                        // flag has grown/backtracked/intersect
                        grown_back = true;
                    }
                    // else encounter with left tree
                    else {
                        // if u == vl
                        if (u == vl) {
                            // DCV = u
                            dcv = u;
                        }
                        // flag has grown/backtracked/intersect
                        grown_back = true;
                    }
                }
            }
        }
        // check if path discovered, if yes flag path found
        if (VECTOR(*match)[vl] == -1 && VECTOR(*match)[vr] == -1) {
            pathFound = true;
        }
    }
    // if path found
    // 	run findPath()
    // 	augment matching, and erased path vertices
    // if blossom found
    // 	remove "right" mark from DCV
    // 	create new blossom set B, consisting of all vertices mark "left" or "right"
    // 	peakL(B) = w1, peakR(B) = w2, base(B) = DCV
    // 	for each u in B:
    // 		blossom(u) = B
    // 		if u is outer
    // 			oddlevel(u) = 2i + 1 - evenlevel(u)
    // 		if u is inner
    // 			evenlevel(u) = 2i + 1 - oddlevel(u)
    // 			for each v in anomalies(u)
    // 				temp = (evenlevel(u)+evenlevel(v))/2
    // 				add edge (u,v) to bridges(temp)
    // 				Mark (u,v) used
    return IGRAPH_SUCCESS;
}


// findPath(high, low, B, even_level, odd_level, LR_marks, blossoms) high and low vertices, B is a blossom, levels and LR_marks are arrays, blossoms is three arrays storing left and right peaks and bases of blossoms
//******************************************
// Must be called with a path existing between high and low, with the corresponding data used by BlossAug() to find it, otherwise errors may occur
// Note: open() from paper is combined with findPath to remove recursive calls
// path represented as vertex list, linked list will be used to allow efficient insertion. TODO: check if this is necessary for time complexity
// Initialize a stack, each entry holding high, low, B, insertionPoint, and reverse
// 	high, low, and B integers, insertionPoint is linked list pointer, reverse is boolean
// Initialize linked list path
// push high, low, B, head of path, and false onto stack
// while stack isn't empty
// 	pop stack entry
// 	init flags
// 	while path not found
// 		if v has no more unvisited predeccessor edges
// 			v=f(v)
// 		else
// 			if blossom(v) = B
// 				choose unvisited predecessor edge (v,u)
// 				mark (v,u) visited
// 			else
// 				u = base(blossom(v))
// 		if u==low
// 			flag path found
// 		if (u is visited) or (level(u) <= level(low)) or ((blossom(u)==B) and (u does not have the same left right mark as high))
//
// 		else
// 			mark u visited
// 			f(u) = v
// 			v = u
// 	init tempPath
// 	until v==high
// 		if reverse is false
// 			insert v at begining of tempPath
// 			v=f(v)
// 		else
// 			insert v at end of tempPath //TODO: double check end is correct
// 			v=f(v)
// 	let m be length of tempPath
// 	insert temppath after insertionPoint
// 	for j=(start of tempPath in path) to (that start + m-2)
// 		if blossom(path(j)) != B
// 			B = blossom(path(j))
// 			if path(j) is outer
// 				push path(j), path(j+1), B, pointer to path(j), false
// 			else
// 				let PeakL and PeakR be the peaks of B
// 				if path(j) is marked left
// 					// ordering is important to ensure the two paths are inserted in correct order
// 					push PeakL, path(j), B, pointer to path(j), true
// 					push PeakR, path(j+1), B, pointer to path(j), false
// 				else
// 					push PeakR, path(j), B, pointer to path(j), true
// 					push PeakL, path(j+1), B, pointer to path(j), false
// return path

// more typical disjoint set implementation used by incremental tree set internally
struct igraph_i_disjoint_set_node {
    igraph_integer_t x;
    igraph_integer_t size;
    struct igraph_i_disjoint_set_node *parent;
};

struct igraph_i_disjoint_set {
    struct igraph_i_disjoint_set_node *nodes;
};


// elements must be integers 0 to element_count-1
void igraph_i_disjoint_set_init(struct igraph_i_disjoint_set *T, igraph_integer_t element_count) {
    T->nodes = (struct igraph_i_disjoint_set_node*) malloc(element_count*sizeof(struct igraph_i_disjoint_set_node));
    for (int i=0; i < element_count; i++) {
        T->nodes[i].x = i;
        T->nodes[i].size = 1;
        T->nodes[i].parent = NULL;
    }
}

// macrosetmake - unnecessary? all elements come in premade singleton sets?
void igraph_i_disjoint_set_make(struct igraph_i_disjoint_set *T, igraph_integer_t x) {
}

// macrofind
igraph_integer_t igraph_i_disjoint_set_find(struct igraph_i_disjoint_set *T, igraph_integer_t x) {
    struct igraph_i_disjoint_set_node *root = T->nodes + (x*sizeof(struct igraph_i_disjoint_set_node));
    while (root->parent != NULL) {
        root = root->parent;
    }

    struct igraph_i_disjoint_set_node *y = T->nodes + (x*sizeof(struct igraph_i_disjoint_set_node));
    while (y->parent != NULL) {
        struct igraph_i_disjoint_set_node *parent = y->parent;
        y->parent = root;
        y = parent;
    }

    return root->x;
}

// macrounite
void igraph_i_disjoint_set_unite(struct igraph_i_disjoint_set *T, igraph_integer_t x, igraph_integer_t y) {
    igraph_integer_t temp;
    x = igraph_i_disjoint_set_find(T, x);
    y = igraph_i_disjoint_set_find(T, y);

    if (x != y) {
        if (T->nodes[x].size < T->nodes[y].size) {
            temp = x;
            y = x;
            x = temp;
        }
        T->nodes[y].parent = &T->nodes[x];
        T->nodes[y].size = T->nodes[x].size + T->nodes[y].size;
    }
}

// destroy
void igraph_i_disjoint_set_destroy(struct igraph_i_disjoint_set *T) {
    free(T->nodes);
}

// special set implementation used for blossoms, needed to meet time complexity of maximum matching algorithm
struct igraph_i_incremental_tree_set_node {
    igraph_integer_t x;
    struct igraph_i_incremental_tree_set_node *parent;
};

struct igraph_i_incremental_tree_set {
    // idk how to represent the tree
    struct igraph_i_disjoint_set macro_sets;
    igraph_vector_int_t micro; //micro set an element belongs to
    igraph_vector_int_t size; //the number of elements in the given micro set
    igraph_vector_int_t number; //element's number within its micro set
    igraph_vector_int_list_t node;
    igraph_matrix_int_t mark;
    igraph_matrix_int_t parent; //stores parent of node if parent in same micro set, 0 otherwise
    igraph_vector_int_t root; //stores the root of each micro set

    igraph_vector_int_t answer_q; //translates between rows of parent matrix and a number
    igraph_vector_int_t answer_s; //translates between rows of parent mark and a number
    igraph_vector_int_t answer; //treated as 3d array using igraph_i_incremental_tree_set_answer
};

void igraph_i_incremental_tree_set_init(struct igraph_i_incremental_tree_set *T) {
    // initialize root node and other sets
    //
    // initalize answers array

}
// grow(T,v,w) add w to tree making v its parent
// link(T,v) link v to its parent
void igraph_i_incremental_tree_set_link(struct igraph_i_incremental_tree_set *T, igraph_integer_t v) {
    MATRIX(T->mark,VECTOR(T->micro)[v],VECTOR(T->number)[v]) = 1;
}
// get answers
igraph_integer_t igraph_i_incremental_tree_set_answer(struct igraph_i_incremental_tree_set *T, igraph_integer_t i, igraph_integer_t j) {
    return VECTOR(T->answer)[VECTOR(T->answer_q)[i]+VECTOR(T->answer_s)[i]+j];
}
// microfind
igraph_integer_t igraph_i_incremental_tree_set_micro_find(struct igraph_i_incremental_tree_set *T, igraph_integer_t v) {
    igraph_integer_t i,j,k;
    i = VECTOR(T->micro)[v];
    j = VECTOR(T->number)[v];
    k = igraph_i_incremental_tree_set_answer(T,i,j);
    if (k == 0) {
        return VECTOR(T->root)[i];
    }
    else if (k > 0) {
        return VECTOR(*igraph_vector_int_list_get_ptr(&T->node, i))[k];
    }
}
// find(T,v) find the set v belongs to
igraph_integer_t igraph_i_incremental_tree_set_find(struct igraph_i_incremental_tree_set *T, igraph_integer_t v, igraph_integer_t *result) {
    igraph_integer_t x = v;
    if (VECTOR(T->micro)[x] != VECTOR(T->micro)[igraph_i_incremental_tree_set_micro_find(T,x)]) {
        x = igraph_i_disjoint_set_find(&T->macro_sets, igraph_i_incremental_tree_set_micro_find(T,x));
        while (VECTOR(T->micro)[x] != VECTOR(T->micro)[igraph_i_incremental_tree_set_micro_find(T,x)]) {
            igraph_i_disjoint_set_unite(&T->macro_sets, igraph_i_disjoint_set_find(&T->macro_sets, x), x);
            x = igraph_i_disjoint_set_find(&T->macro_sets, x);
        }
    }
    return igraph_i_incremental_tree_set_micro_find(T,x);
}
// destroy(T)


#ifdef MATCHING_DEBUG
    #undef MATCHING_DEBUG
#endif
