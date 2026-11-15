#pragma once

#include "lx/lx_language.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional extension C ABI structs returned by lx_language_v1::get_extension(name) (§7.5). */

/* "lx.highlight.v1" */
typedef struct lx_ext_highlight_v1 {
  uint32_t size;
  lx_str query_scm; /* optional S-expression highlight query */
} lx_ext_highlight_v1;

/* "lx.fold.v1" */
typedef struct lx_ext_fold_v1 {
  uint32_t size;
  lx_str query_scm; /* optional S-expression fold query */
} lx_ext_fold_v1;

/* "lx.outline.v1" */
typedef struct lx_ext_outline_v1 {
  uint32_t size;
  lx_str query_scm; /* optional S-expression outline query */
} lx_ext_outline_v1;

/* "lx.inject.v1" */
typedef struct lx_inject_rule_v1 {
  lx_symbol node_symbol; /* rule/token symbol in host language */
  lx_field  field_id;    /* optional child field (0 = whole node) */
  lx_str    language_id; /* target language id */
  uint32_t  combined;    /* non-zero = combine all matching ranges into one RopeView */
  uint32_t  trim_quotes; /* non-zero = strip 1-char surrounding delimiters if present */
} lx_inject_rule_v1;

typedef struct lx_ext_inject_v1 {
  uint32_t size;
  lx_str query_scm;                  /* optional S-expression injection query (@injection.content) */
  const lx_inject_rule_v1* rules;
  uint32_t rule_count;
} lx_ext_inject_v1;

#ifdef __cplusplus
}
#endif
