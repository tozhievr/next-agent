#include "embed.h"
#include "json.h"
#include "platform.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Embedder {
    char *model, *api_env;
    char *doc_prefix, *query_prefix; /* text put before every chunk / query (models such as nomic-embed-text need them) */
    bool use_cmd;
    /* http */
    char *host, *path;
    int port;
    /* command */
    char *cmd, *args, *root;
    PfProc *proc;
    size_t batch, max_chars;
    int timeout_ms;
    /* cache */
    char *cache_path;
    size_t dim;
    uint64_t *keys;
    float *vecs; /* entry i at vecs + i * dim */
    size_t n, cap;
    uint32_t *slots; /* entry + 1 */
    size_t scap;
    size_t flushed; /* entries already on disk */
    uint64_t disk_len;
};

static uint64_t fnv(uint64_t h, const char *p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)p[i]) * 1099511628211ull;
    return h;
}

static double dsqrt(double x) {
    if (x <= 0) return 0;
    double g = (x + 1) / 2;
    for (int i = 0; i < 60; i++) g = 0.5 * (g + x / g);
    return g;
}

/* ---- cache index ---- */
static void idx_rehash(Embedder *e) {
    size_t cap = e->scap ? e->scap * 2 : 1024;
    free(e->slots);
    e->slots = na_alloc(cap * sizeof *e->slots);
    e->scap = cap;
    for (size_t i = 0; i < e->n; i++) {
        size_t h = (size_t)e->keys[i] & (cap - 1);
        while (e->slots[h]) h = (h + 1) & (cap - 1);
        e->slots[h] = (uint32_t)i + 1;
    }
}

static const float *idx_find(const Embedder *e, uint64_t key) {
    if (!e->scap) return NULL;
    size_t h = (size_t)key & (e->scap - 1);
    while (e->slots[h]) {
        size_t i = e->slots[h] - 1;
        if (e->keys[i] == key) return e->vecs + i * e->dim;
        h = (h + 1) & (e->scap - 1);
    }
    return NULL;
}

static void idx_add(Embedder *e, uint64_t key, const float *v) {
    if (idx_find(e, key)) return;
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 256;
        e->keys = na_realloc(e->keys, e->cap * sizeof *e->keys);
        e->vecs = na_realloc(e->vecs, e->cap * e->dim * sizeof *e->vecs);
    }
    e->keys[e->n] = key;
    memcpy(e->vecs + e->n * e->dim, v, e->dim * sizeof *v);
    e->n++;
    if (e->n * 2 > e->scap) idx_rehash(e);
    else {
        size_t h = (size_t)key & (e->scap - 1);
        while (e->slots[h]) h = (h + 1) & (e->scap - 1);
        e->slots[h] = (uint32_t)e->n;
    }
}

/* file: "NAEMB1\n" then records [u64 key][u32 dim][dim floats]; a torn tail is ignored */
static void cache_load(Embedder *e) {
    Buf b;
    buf_init(&b);
    if (pf_read_file(e->cache_path, &b) != 0 || b.len < 7 || memcmp(b.p, "NAEMB1\n", 7) != 0) {
        buf_free(&b);
        e->disk_len = 0;
        return;
    }
    size_t p = 7;
    while (p + 12 <= b.len) {
        uint64_t key;
        uint32_t dim;
        memcpy(&key, b.p + p, 8);
        memcpy(&dim, b.p + p + 8, 4);
        if (dim == 0 || dim > 8192 || p + 12 + (size_t)dim * 4 > b.len) break;
        if (e->dim == 0) e->dim = dim;
        if (dim == e->dim) {
            float tmp[8192];
            memcpy(tmp, b.p + p + 12, (size_t)dim * 4);
            idx_add(e, key, tmp);
        }
        p += 12 + (size_t)dim * 4;
    }
    e->disk_len = p;
    e->flushed = e->n;
    buf_free(&b);
}

void embed_flush(Embedder *e) {
    if (!e || e->flushed >= e->n || !e->cache_path) return;
    Buf b;
    buf_init(&b);
    if (e->disk_len == 0) buf_put(&b, "NAEMB1\n", 7);
    for (size_t i = e->flushed; i < e->n; i++) {
        uint32_t dim = (uint32_t)e->dim;
        buf_put(&b, &e->keys[i], 8);
        buf_put(&b, &dim, 4);
        buf_put(&b, e->vecs + i * e->dim, e->dim * sizeof(float));
    }
    if (pf_append_at(e->cache_path, e->disk_len, b.p, b.len) == 0) {
        e->disk_len += b.len;
        e->flushed = e->n;
    }
    buf_free(&b);
}

/* ---- open / close ---- */
static const char *jstr(const J *o, const char *k) {
    const J *v = j_get(o, k);
    return v && j_is_str(v) ? v->s : NULL;
}

static int jint(const J *o, const char *k, int def) {
    const J *v = j_get(o, k);
    return v && j_is_int(v) ? (int)v->i : def;
}

Embedder *embed_open(const char *root, char **error) {
    *error = NULL;
    Buf path, text;
    buf_init(&path);
    buf_init(&text);
    buf_printf(&path, "%s/.aed/embed.json", root);
    int rc = pf_read_file(path.p, &text);
    buf_free(&path);
    if (rc == 1) {
        buf_free(&text);
        return NULL;
    }
    J *cfg = rc == 0 ? json_parse(text.p, text.len) : NULL;
    buf_free(&text);
    if (!cfg || cfg->k != J_OBJ) {
        *error = na_strdup("embed.json: not a JSON object");
        json_free(cfg);
        return NULL;
    }
    Embedder *e = na_alloc(sizeof *e);
    const char *url = jstr(cfg, "url"), *cmd = jstr(cfg, "command");
    e->model = na_strdup(jstr(cfg, "model") ? jstr(cfg, "model") : "");
    e->api_env = jstr(cfg, "api_key_env") ? na_strdup(jstr(cfg, "api_key_env")) : NULL;
    {
        bool nomic = strstr(e->model, "nomic-embed") != NULL; /* trained with task prefixes; worse without them */
        const char *dp = jstr(cfg, "doc_prefix"), *qp = jstr(cfg, "query_prefix");
        e->doc_prefix = na_strdup(dp ? dp : nomic ? "search_document: " : "");
        e->query_prefix = na_strdup(qp ? qp : nomic ? "search_query: " : "");
    }
    e->batch = (size_t)jint(cfg, "batch", 16);
    if (e->batch < 1 || e->batch > 256) e->batch = 16;
    e->max_chars = (size_t)jint(cfg, "max_chars", 1500);
    e->timeout_ms = jint(cfg, "timeout_ms", 30000);
    e->root = na_strdup(root);
    if (cmd) {
        e->use_cmd = true;
        e->cmd = na_strdup(cmd);
        e->args = na_strdup(jstr(cfg, "args") ? jstr(cfg, "args") : "");
    } else if (url && strncmp(url, "http://", 7) == 0) {
        const char *h = url + 7, *slash = strchr(h, '/'), *colon = memchr(h, ':', slash ? (size_t)(slash - h) : strlen(h));
        size_t hl = colon ? (size_t)(colon - h) : slash ? (size_t)(slash - h) : strlen(h);
        e->host = na_strndup(h, hl);
        e->port = colon ? atoi(colon + 1) : 80;
        e->path = na_strdup(slash ? slash : "/");
        if (e->port <= 0 || e->port > 65535) {
            *error = na_strdup("embed.json: bad port in url");
        }
    } else {
        *error = na_strdup(url ? "embed.json: only http:// urls are supported (no TLS; use a local proxy or \"command\")"
                               : "embed.json: needs \"url\" or \"command\"");
    }
    json_free(cfg);
    if (*error) {
        embed_close(e);
        return NULL;
    }
    Buf cp;
    buf_init(&cp);
    buf_printf(&cp, "%s/.aed/embed.cache", root);
    e->cache_path = buf_take(&cp);
    cache_load(e);
    return e;
}

void embed_close(Embedder *e) {
    if (!e) return;
    embed_flush(e);
    if (e->proc) pf_proc_close(e->proc, 300);
    free(e->model);
    free(e->doc_prefix);
    free(e->query_prefix);
    free(e->api_env);
    free(e->host);
    free(e->path);
    free(e->cmd);
    free(e->args);
    free(e->root);
    free(e->cache_path);
    free(e->keys);
    free(e->vecs);
    free(e->slots);
    free(e);
}

const char *embed_model(const Embedder *e) { return e->use_cmd ? (e->model[0] ? e->model : e->cmd) : e->model; }
size_t embed_max_chars(const Embedder *e) { return e->max_chars; }
const char *embed_doc_prefix(const Embedder *e) { return e->doc_prefix; }
const char *embed_query_prefix(const Embedder *e) { return e->query_prefix; }
size_t embed_dim(const Embedder *e) { return e->dim; }

uint64_t embed_key(const Embedder *e, const char *text, size_t n) {
    uint64_t h = fnv(1469598103934665603ull, embed_model(e), strlen(embed_model(e)));
    h = fnv(h, "\0", 1);
    return fnv(h, text, n);
}

const float *embed_get(const Embedder *e, uint64_t key) { return idx_find(e, key); }

/* ---- HTTP ---- */
static bool ci_starts(const char *p, const char *lit) {
    for (; *lit; p++, lit++)
        if (tolower((unsigned char)*p) != *lit) return false;
    return true;
}

static int http_parse(const char *buf, size_t n, bool eof, int *status, Buf *body) {
    const char *end = NULL;
    for (size_t i = 0; i + 4 <= n; i++)
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
            end = buf + i;
            break;
        }
    if (!end) return eof ? -1 : 0;
    if (n < 12 || memcmp(buf, "HTTP/1.", 7) != 0) return -1;
    *status = atoi(buf + 9);
    long clen = -1;
    bool chunked = false;
    for (const char *l = memchr(buf, '\n', (size_t)(end - buf)); l && l < end; l = memchr(l + 1, '\n', (size_t)(end - l - 1))) {
        const char *h = l + 1;
        while (h < end && (*h == ' ' || *h == '\t')) h++;
        if (ci_starts(h, "content-length:")) clen = atol(h + 15);
        else if (ci_starts(h, "transfer-encoding:")) {
            const char *eol = memchr(h, '\r', (size_t)(end + 2 - h));
            for (const char *q = h + 18; q + 7 <= (eol ? eol : end); q++)
                if (ci_starts(q, "chunked")) chunked = true;
        }
    }
    const char *b = end + 4, *stop = buf + n;
    body->len = 0;
    if (chunked) {
        const char *p = b;
        while (true) {
            const char *eol = NULL;
            for (const char *q = p; q + 2 <= stop; q++)
                if (q[0] == '\r' && q[1] == '\n') {
                    eol = q;
                    break;
                }
            if (!eol) return eof ? -1 : 0;
            char *endp;
            unsigned long sz = strtoul(p, &endp, 16);
            if (endp == p) return -1;
            p = eol + 2;
            if (sz == 0) return 1;
            if ((size_t)(stop - p) < sz + 2) return eof ? -1 : 0;
            buf_put(body, p, sz);
            p += sz + 2;
        }
    }
    if (clen >= 0) {
        if ((size_t)(stop - b) < (size_t)clen) return eof ? -1 : 0;
        buf_put(body, b, (size_t)clen);
        return 1;
    }
    if (!eof) return 0;
    buf_put(body, b, (size_t)(stop - b));
    return 1;
}

int embed_http_parse(const char *buf, size_t n, int *status, Buf *body) { return http_parse(buf, n, false, status, body); }

static int http_post(Embedder *e, const char *json, size_t jn, Buf *reply) {
    PfSock *s = pf_tcp_connect(e->host, e->port, e->timeout_ms);
    if (!s) return -1;
    Buf req;
    buf_init(&req);
    buf_printf(&req, "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\nAccept: application/json\r\n"
                     "Connection: close\r\nContent-Length: %zu\r\n",
               e->path, e->host, e->port, jn);
    const char *key = e->api_env ? getenv(e->api_env) : NULL;
    if (key && *key) buf_printf(&req, "Authorization: Bearer %s\r\n", key);
    buf_puts(&req, "\r\n");
    buf_put(&req, json, jn);
    int rc = pf_sock_write(s, req.p, req.len);
    buf_free(&req);
    if (rc != 0) {
        pf_sock_close(s);
        return na_fail("embedding backend: write failed");
    }
    Buf raw;
    buf_init(&raw);
    uint64_t deadline = pf_now_ms() + (uint64_t)e->timeout_ms;
    int status = 0, done = 0;
    char chunk[16384];
    while (!done) {
        int left = (int)(deadline > pf_now_ms() ? deadline - pf_now_ms() : 0);
        if (left == 0) {
            done = -2;
            break;
        }
        int r = pf_sock_read(s, chunk, sizeof chunk, left);
        bool eof = r < 0;
        if (r > 0) buf_put(&raw, chunk, (size_t)r);
        reply->len = 0;
        int pr = http_parse(raw.p ? raw.p : "", raw.len, eof, &status, reply);
        if (pr != 0) done = pr;
        else if (eof) done = -1;
    }
    pf_sock_close(s);
    buf_free(&raw);
    if (done == -2) return na_fail("embedding backend: timed out after %d ms", e->timeout_ms);
    if (done < 0) return na_fail("embedding backend: malformed or truncated HTTP response");
    if (status != 200) {
        char head[160];
        size_t k = reply->len < sizeof head - 1 ? reply->len : sizeof head - 1;
        memcpy(head, reply->p ? reply->p : "", k);
        head[k] = 0;
        return na_fail("embedding backend: HTTP %d: %s", status, head);
    }
    return 0;
}

/* ---- command ---- */
static int cmd_call(Embedder *e, const char *json, size_t jn, Buf *reply) {
    if (!e->proc) {
        char *path = pf_which(e->cmd);
        if (!path) return na_fail("embedding command not found: %s", e->cmd);
        e->proc = pf_spawn(path, e->args, e->root);
        free(path);
        if (!e->proc) return -1;
    }
    if (pf_proc_write(e->proc, json, jn) != 0 || pf_proc_write(e->proc, "\n", 1) != 0) return na_fail("embedding command: write failed");
    reply->len = 0;
    uint64_t deadline = pf_now_ms() + (uint64_t)e->timeout_ms;
    char chunk[16384];
    while (true) {
        if (reply->len && memchr(reply->p, '\n', reply->len)) return 0;
        uint64_t now = pf_now_ms();
        if (now >= deadline) return na_fail("embedding command: timed out after %d ms", e->timeout_ms);
        int r = pf_proc_read(e->proc, chunk, sizeof chunk, (int)(deadline - now));
        if (r < 0) return na_fail("embedding command exited");
        if (r > 0) buf_put(reply, chunk, (size_t)r);
    }
}

/* ---- request ---- */
static int fetch_batch(Embedder *e, const char *const *texts, const size_t *lens, size_t n, float **out, size_t *dim) {
    J *req = j_obj(), *in = j_arr();
    if (e->model[0]) j_set(req, "model", j_str(e->model));
    for (size_t i = 0; i < n; i++) j_push(in, j_strn(texts[i], lens[i]));
    j_set(req, "input", in);
    char *body = json_dump(req);
    json_free(req);
    Buf reply;
    buf_init(&reply);
    int rc = e->use_cmd ? cmd_call(e, body, strlen(body), &reply) : http_post(e, body, strlen(body), &reply);
    free(body);
    if (rc != 0) {
        buf_free(&reply);
        return -1;
    }
    J *res = json_parse(reply.p, reply.len);
    buf_free(&reply);
    if (!res) return na_fail("embedding backend: reply is not JSON: %s", na_errmsg());
    const J *list = j_get(res, "embeddings"), *data = j_get(res, "data");
    size_t cnt = 0;
    const J *rows[256];
    if (list && list->k == J_ARR) {
        for (size_t i = 0; i < list->n && i < 256; i++) rows[cnt++] = list->items[i];
    } else if (data && data->k == J_ARR) {
        for (size_t i = 0; i < data->n && i < 256; i++) rows[cnt++] = j_get(data->items[i], "embedding");
    } else if (j_get(res, "embedding") && n == 1) {
        rows[cnt++] = j_get(res, "embedding");
    }
    if (cnt != n) {
        const J *err = j_get(res, "error");
        int r = na_fail("embedding backend: expected %zu vectors, got %zu%s%s", n, cnt, err && j_is_str(err) ? ": " : "", err && j_is_str(err) ? err->s : "");
        json_free(res);
        return r;
    }
    size_t d = rows[0] && rows[0]->k == J_ARR ? rows[0]->n : 0;
    if (d == 0 || d > 8192) {
        json_free(res);
        return na_fail("embedding backend: bad vector size");
    }
    float *v = na_alloc(n * d * sizeof *v);
    for (size_t i = 0; i < n; i++) {
        if (!rows[i] || rows[i]->k != J_ARR || rows[i]->n != d) {
            free(v);
            json_free(res);
            return na_fail("embedding backend: vectors of different size");
        }
        double s = 0;
        for (size_t k = 0; k < d; k++) {
            const J *x = rows[i]->items[k];
            double f = x->k == J_INT ? (double)x->i : x->k == J_DBL ? x->d : 0;
            v[i * d + k] = (float)f;
            s += f * f;
        }
        s = dsqrt(s);
        if (s > 1e-12)
            for (size_t k = 0; k < d; k++) v[i * d + k] = (float)(v[i * d + k] / s);
    }
    json_free(res);
    *out = v;
    *dim = d;
    return 0;
}

int embed_texts(Embedder *e, const char *const *texts, const size_t *lens, size_t n) {
    const char *bt[256];
    size_t bl[256];
    uint64_t bk[256];
    size_t k = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i < n) {
            uint64_t key = embed_key(e, texts[i], lens[i]);
            if (idx_find(e, key)) continue;
            bt[k] = texts[i];
            bl[k] = lens[i];
            bk[k++] = key;
        }
        if (k == 0 || (k < e->batch && i < n)) continue;
        float *v = NULL;
        size_t d = 0;
        if (fetch_batch(e, bt, bl, k, &v, &d) != 0) return -1;
        if (e->dim == 0) e->dim = d;
        if (d != e->dim) { /* the cache belongs to another model: start over */
            free(v);
            size_t old = e->dim;
            e->n = e->flushed = 0;
            e->dim = d;
            e->disk_len = 0;
            free(e->slots);
            e->slots = NULL;
            e->scap = 0;
            pf_remove(e->cache_path);
            return na_fail("embedding cache held %zu-dimensional vectors, the model returns %zu: cache reset, repeat the query", old, d);
        }
        for (size_t j = 0; j < k; j++) idx_add(e, bk[j], v + j * d);
        free(v);
        k = 0;
    }
    return 0;
}
