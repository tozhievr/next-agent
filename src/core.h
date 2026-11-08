/* Immutable codepoint-based snapshots and atomic changesets. */
#ifndef NA_CORE_H
#define NA_CORE_H
#include "util.h"

typedef struct {
    int64_t rev;
    char *text; /* UTF-8, NUL-terminated */
    size_t len; /* bytes */
    size_t ncp; /* codepoints */
    uint8_t hash[16];
    size_t *line_starts; /* codepoint offsets of line starts (first is 0) */
    size_t nlines;
} Snapshot;

/* `text` must be valid UTF-8 (copied). */
Snapshot *snap_new(int64_t rev, const char *text, size_t len);
void snap_free(Snapshot *s);
/* 1-based line, 0-based codepoint column. */
int snap_position(const Snapshot *s, size_t offset, size_t *line, size_t *col);
int snap_offset(const Snapshot *s, size_t line, size_t col, size_t *offset);

/* Codepoint offset -> byte offset, or (size_t)-1 if beyond the text. */
size_t cp_to_byte(const char *text, size_t len, size_t cp);

typedef struct {
    size_t start, end; /* codepoint offsets in the original document */
    char *text;
    size_t tlen;
} Edit;

typedef struct {
    Edit *v;
    size_t n, cap;
} ChangeSet;

void cs_init(ChangeSet *cs);
void cs_free(ChangeSet *cs);
void cs_add(ChangeSet *cs, size_t start, size_t end, const char *text, size_t tlen);
void cs_sort(ChangeSet *cs); /* stable by (start, end) */
int cs_validate(const ChangeSet *cs); /* sorted, non-overlapping */
ChangeSet cs_clone(const ChangeSet *cs);
int cs_apply(const ChangeSet *cs, const Snapshot *s, int64_t new_rev, Snapshot **out);
int cs_apply_text(const ChangeSet *cs, const char *text, size_t len, char **out, size_t *outlen);
int cs_invert(const ChangeSet *cs, const Snapshot *s, ChangeSet *out);
size_t cs_map_pos(const ChangeSet *cs, size_t offset, int assoc);
int cs_stats(const ChangeSet *cs, const Snapshot *s, size_t *added, size_t *removed);

#endif
