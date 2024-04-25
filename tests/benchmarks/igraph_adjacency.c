
#include <igraph.h>

#include "bench.h"

/*
 * Benchmark creating graphs from dense adjacency matrices.
 *
 * When there are a small number of non-zero elements (low mean degree),
 * iterating through the matrix dominates the timing. When there are
 * many non-zero elements, creating the graphfrom its edge list dominates.
 */

void run_bench(igraph_integer_t vcount, igraph_integer_t meandeg, igraph_integer_t rep) {
    igraph_t g;
    igraph_matrix_t mat;
    igraph_vector_t weights;
    char msg[255];
    igraph_adjacency_t types[] = {
        IGRAPH_ADJ_DIRECTED,
        IGRAPH_ADJ_MAX,
        IGRAPH_ADJ_PLUS,
        IGRAPH_ADJ_UPPER /* similar to DIRECTED when unweighted, similar to MAX when weighted */
    };
    const char *names[] = { "DIRECTED", "MAX", "PLUS", "UPPER" };

    igraph_matrix_init(&mat, 0, 0);

    igraph_erdos_renyi_game_gnm(&g, vcount, meandeg * vcount / 2, IGRAPH_DIRECTED, IGRAPH_LOOPS, IGRAPH_MULTIPLE);
    igraph_get_adjacency(&g, &mat, IGRAPH_GET_ADJACENCY_BOTH, NULL, IGRAPH_LOOPS_ONCE);

    igraph_vector_init(&weights, igraph_ecount(&g));

    igraph_destroy(&g);

    for (size_t i=0; i < sizeof(types) / sizeof(types[0]); i++) {
        snprintf(msg, sizeof(msg) / sizeof(msg[0]),
                 "%2d vcount=%" IGRAPH_PRId ", meandeg=%3" IGRAPH_PRId ", %8s, unweighted, %" IGRAPH_PRId "x",
                 (int) i+1, vcount, meandeg, names[i], rep);

        BENCH(msg, REPEAT(igraph_adjacency(&g, &mat, types[i], IGRAPH_LOOPS_ONCE), rep));
        igraph_destroy(&g);

        snprintf(msg, sizeof(msg) / sizeof(msg[0]),
                 "%2d vcount=%" IGRAPH_PRId ", meandeg=%3" IGRAPH_PRId ", %8s,   weighted, %" IGRAPH_PRId "x",
                 (int) i+1, vcount, meandeg, names[i], rep);

        BENCH(msg, REPEAT(igraph_weighted_adjacency(&g, &mat, types[i], &weights, IGRAPH_LOOPS_ONCE), rep));
        igraph_destroy(&g);
    }
    printf("\n");

    igraph_vector_destroy(&weights);
    igraph_matrix_destroy(&mat);
}

int main(void) {

    igraph_rng_seed(igraph_rng_default(), 137);
    BENCH_INIT();

    run_bench(100, 5, 10000);
    run_bench(100, 50, 10000);
    run_bench(1000, 5, 100);
    run_bench(1000, 50, 100);
    run_bench(1000, 500, 100);
    run_bench(10000, 5, 1);
    run_bench(10000, 50, 1);
    run_bench(10000, 500, 1);

    return 0;
}
