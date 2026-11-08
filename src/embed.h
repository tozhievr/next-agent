/* Lightweight caller for an external embedding model, configured in <root>/.aed/embed.json:
 *
 *   {"url": "http://127.0.0.1:11434/v1/embeddings", "model": "nomic-embed-text"}   plain-HTTP, OpenAI style
 *   {"url": "http://127.0.0.1:11434/api/embed",     "model": "nomic-embed-text"}   Ollama style
 *   {"command": "my-embedder", "args": "--fast"}                                    JSON lines on stdin/stdout
 *
 * Optional: "api_key_env" (name of an environment variable holding a bearer token), "batch" (texts per
 * request, default 16), "timeout_ms" (default 30000), "max_chars" (text cut-off, default 1500),
 * "doc_prefix" / "query_prefix" (put before every chunk / query; "search_document: " / "search_query: " by
 * default for nomic-embed models, which are trained with them).
 * Request  : {"model": M, "input": ["text", ...]}
 * Response : {"data":[{"embedding":[...]}, ...]}  or  {"embeddings":[[...], ...]}  (one object per line for `command`)
 * There is no TLS and no DNS: for a remote or https backend run a local proxy, or use `command`.
 * Vectors are L2-normalised and kept in an on-disk cache (.aed/embed.cache) keyed by model + text, so a
 * text is sent to the model once. */
#ifndef NA_EMBED_H
#define NA_EMBED_H
#include "util.h"

typedef struct Embedder Embedder;

/* NULL with *error == NULL: not configured. NULL with *error set: configured but invalid. */
Embedder *embed_open(const char *root, char **error);
void embed_close(Embedder *e);
const char *embed_model(const Embedder *e);
size_t embed_max_chars(const Embedder *e);
const char *embed_doc_prefix(const Embedder *e);   /* "" unless configured (default for nomic-embed models) */
const char *embed_query_prefix(const Embedder *e);
size_t embed_dim(const Embedder *e); /* 0 until the first vector is known */

uint64_t embed_key(const Embedder *e, const char *text, size_t n);
/* Cached normalised vector (embed_dim floats) or NULL. */
const float *embed_get(const Embedder *e, uint64_t key);
/* Embeds the texts (sending only those not cached) and caches the results; 0 ok, -1 error (na_errmsg). */
int embed_texts(Embedder *e, const char *const *texts, const size_t *lens, size_t n);
void embed_flush(Embedder *e); /* appends new vectors to the on-disk cache */

/* HTTP response decoding, exposed for tests. Returns 1 when `buf` holds a complete response (status and
 * decoded body filled in), 0 when more bytes are needed, -1 when malformed. */
int embed_http_parse(const char *buf, size_t n, int *status, Buf *body);

#endif
