/* Line-based three-way merge (diff3 style). Changes of both sides that touch or overlap are a conflict
 * unless both sides made the identical change. */
#ifndef NA_MERGE_H
#define NA_MERGE_H
#include "util.h"

typedef struct {
    size_t first, last; /* base lines (1-based, inclusive); last < first for a pure insertion */
    char *ours, *theirs;
} MergeConflict;

typedef struct {
    MergeConflict *v;
    size_t n;
} MergeConflicts;

/* 0: merged cleanly into `out`; 1: conflicts listed in `conf`, `out` unusable; -1: error (too large). */
int merge3(const char *base, size_t bn, const char *ours, size_t on, const char *theirs, size_t tn, Buf *out,
           MergeConflicts *conf);
void merge_conflicts_free(MergeConflicts *c);

#endif
