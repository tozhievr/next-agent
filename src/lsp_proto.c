#include "lsp.h"
#include "platform.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MESSAGE (16u * 1024u * 1024u)
#define MAX_HEADER 8192u

/* ------------------------------------------------------------------ codec */
int pos_enc_parse(const char *name, PosEnc *out) {
    if (strcmp(name, "utf-8") == 0) *out = ENC_UTF8;
    else if (strcmp(name, "utf-16") == 0) *out = ENC_UTF16;
    else if (strcmp(name, "utf-32") == 0) *out = ENC_UTF32;
    else return na_fail("unsupported position encoding: %s", name);
    return 0;
}

/* Units of one UTF-8 encoded scalar of `seq` bytes. */
static size_t units_of(size_t seq, PosEnc enc) {
    if (enc == ENC_UTF32) return 1;
    if (enc == ENC_UTF16) return seq == 4 ? 2 : 1;
    return seq;
}

int lsp_offset_to_position(const char *text, size_t len, size_t offset, PosEnc enc, size_t *line,
                           size_t *character) {
    size_t b = cp_to_byte(text, len, offset);
    if (b == (size_t)-1) return na_fail("offset out of bounds");
    size_t ln = 0, line_start = 0;
    for (size_t i = 0; i < b; i++)
        if (text[i] == '\n') {
            ln++;
            line_start = i + 1;
        }
    size_t units = 0;
    for (size_t i = line_start; i < b;) {
        size_t l = utf8_seq_len((unsigned char)text[i]);
        if (l == 0) l = 1;
        units += units_of(l, enc);
        i += l;
    }
    *line = ln;
    *character = units;
    return 0;
}

int lsp_position_to_offset(const char *text, size_t len, size_t line, size_t character, PosEnc enc,
                           size_t *offset) {
    size_t i = 0, cp = 0, ln = 0;
    while (ln < line) {
        const char *nl = memchr(text + i, '\n', len - i);
        if (!nl) return na_fail("position out of bounds");
        size_t next = (size_t)(nl - text) + 1;
        cp += utf8_cp_count(text + i, next - i);
        i = next;
        ln++;
    }
    size_t consumed = 0;
    while (true) {
        if (consumed == character) {
            *offset = cp;
            return 0;
        }
        if (i >= len || text[i] == '\n') return na_fail("position out of bounds");
        size_t l = utf8_seq_len((unsigned char)text[i]);
        if (l == 0) l = 1;
        consumed += units_of(l, enc);
        if (consumed > character) return na_fail("position splits Unicode scalar");
        i += l;
        cp++;
    }
}

/* ---------------------------------------------------------------- framing */
void lsp_reader_init(LspReader *r, int (*read)(void *, void *, size_t, int), void *ctx) {
    r->read = read;
    r->ctx = ctx;
    buf_init(&r->pending);
    r->pos = 0;
}

void lsp_reader_free(LspReader *r) { buf_free(&r->pending); }

/* Read one more chunk into the buffer. 1 = data, 0 = timeout, -1 = EOF. */
static int fill_more(LspReader *r, uint64_t deadline) {
    uint64_t now = pf_now_ms();
    int remaining = deadline > now ? (int)(deadline - now) : 0;
    char chunk[8192];
    int n = r->read(r->ctx, chunk, sizeof chunk, remaining);
    if (n < 0) return -1;
    if (n == 0) return 0;
    buf_put(&r->pending, chunk, (size_t)n);
    return 1;
}

static bool header_is_content_length(const char *line, size_t klen) {
    static const char want[] = "content-length";
    if (klen != 14) return false;
    for (size_t k = 0; k < 14; k++)
        if (tolower((unsigned char)line[k]) != want[k]) return false;
    return true;
}

/* Nothing is consumed until a complete message is buffered, so a timeout
 * (return 0) never loses partial input. */
int lsp_read_message(LspReader *r, int timeout_ms, J **out) {
    *out = NULL;
    uint64_t deadline = pf_now_ms() + (uint64_t)(timeout_ms < 0 ? 0 : timeout_ms);
    size_t p = r->pos; /* parse cursor */
    long long length = -1;
    while (true) { /* header lines */
        const char *nl = NULL;
        while (true) {
            if (r->pending.len > p) nl = memchr(r->pending.p + p, '\n', r->pending.len - p);
            if (nl) break;
            if (r->pending.len - r->pos > MAX_HEADER) return na_fail("LSP header too large");
            int rc = fill_more(r, deadline);
            if (rc == 0) return 0;
            if (rc < 0) {
                if (r->pending.len == r->pos) return 2;
                return na_fail("truncated LSP header");
            }
        }
        size_t line_len = (size_t)(nl - (r->pending.p + p)) + 1;
        if (p + line_len - r->pos > MAX_HEADER) return na_fail("LSP header too large");
        const char *line = r->pending.p + p;
        if (line_len == 2 && line[0] == '\r') {
            p += 2;
            break;
        }
        if (line_len < 2 || line[line_len - 2] != '\r') return na_fail("malformed LSP header");
        const char *colon = memchr(line, ':', line_len - 2);
        if (!colon) return na_fail("malformed LSP header");
        if (header_is_content_length(line, (size_t)(colon - line))) {
            const char *v = colon + 1, *e = line + line_len - 2;
            while (v < e && (*v == ' ' || *v == '\t')) v++;
            while (e > v && (e[-1] == ' ' || e[-1] == '\t')) e--;
            if (length >= 0 || v == e) return na_fail("invalid LSP Content-Length");
            long long val = 0;
            for (const char *q = v; q < e; q++) {
                if (*q < '0' || *q > '9') return na_fail("invalid LSP Content-Length");
                val = val * 10 + (*q - '0');
                if (val > (long long)MAX_MESSAGE) return na_fail("missing or oversized LSP Content-Length");
            }
            length = val;
        }
        p += line_len;
    }
    if (length < 0) return na_fail("missing or oversized LSP Content-Length");
    while (r->pending.len - p < (size_t)length) {
        int rc = fill_more(r, deadline);
        if (rc == 0) return 0;
        if (rc < 0) return na_fail("truncated LSP body");
    }
    J *j = json_parse(r->pending.p + p, (size_t)length);
    p += (size_t)length;
    if (p == r->pending.len) {
        r->pending.len = 0;
        r->pos = 0;
    } else {
        r->pos = p;
        if (r->pos > (1u << 20)) {
            memmove(r->pending.p, r->pending.p + r->pos, r->pending.len - r->pos);
            r->pending.len -= r->pos;
            r->pos = 0;
        }
    }
    if (!j) return -1;
    *out = j;
    return 1;
}

int lsp_frame_message(const J *msg, Buf *out) {
    Buf body;
    buf_init(&body);
    json_write(msg, &body);
    if (body.len > MAX_MESSAGE) {
        buf_free(&body);
        return na_fail("LSP message too large");
    }
    buf_printf(out, "Content-Length: %zu\r\n\r\n", body.len);
    buf_put(out, body.p, body.len);
    buf_free(&body);
    return 0;
}

/* ------------------------------------------------------------ diagnostics */
static char *stringify(const J *v, const char *dflt) {
    if (!v) return na_strdup(dflt);
    if (v->k == J_STR) return na_strndup(v->s, v->slen);
    return json_dump(v);
}

static int fingerprint(const J *d, DiagFingerprint *fp) {
    if (!d || d->k != J_OBJ) return na_fail("invalid diagnostics payload");
    const J *sev = j_get(d, "severity");
    if (sev && !j_is_int(sev)) return na_fail("invalid diagnostic severity");
    fp->severity = sev ? (int)sev->i : 1;
    fp->code = stringify(j_get(d, "code"), "");
    fp->message = stringify(j_get(d, "message"), "");
    return 0;
}

static int fp_cmp(const void *a, const void *b) {
    const DiagFingerprint *x = a, *y = b;
    if (x->severity != y->severity) return x->severity < y->severity ? -1 : 1;
    int c = strcmp(x->code, y->code);
    if (c) return c;
    return strcmp(x->message, y->message);
}

static void fp_free_all(DiagFingerprint *v, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free(v[i].code);
        free(v[i].message);
    }
    free(v);
}

static int fingerprints(const J *arr, DiagFingerprint **out, size_t *n) {
    *out = NULL;
    *n = 0;
    if (!arr || arr->k != J_ARR) return na_fail("invalid diagnostics payload");
    DiagFingerprint *v = na_alloc((arr->n ? arr->n : 1) * sizeof *v);
    for (size_t i = 0; i < arr->n; i++) {
        if (fingerprint(arr->items[i], &v[i]) != 0) {
            fp_free_all(v, i);
            return -1;
        }
    }
    if (arr->n > 1) qsort(v, arr->n, sizeof *v, fp_cmp);
    *out = v;
    *n = arr->n;
    return 0;
}

static DiagFingerprint fp_copy(const DiagFingerprint *f) {
    DiagFingerprint c;
    c.severity = f->severity;
    c.code = na_strdup(f->code);
    c.message = na_strdup(f->message);
    return c;
}

int diag_compare(const J *before, const J *after, int maximum_severity, DiagDelta *out) {
    memset(out, 0, sizeof *out);
    if (maximum_severity < 1 || maximum_severity > 4) return na_fail("invalid severity threshold");
    DiagFingerprint *a, *b;
    size_t na, nb;
    if (fingerprints(before, &a, &na) != 0) return -1;
    if (fingerprints(after, &b, &nb) != 0) {
        fp_free_all(a, na);
        return -1;
    }
    out->added = na_alloc((nb ? nb : 1) * sizeof *out->added);
    out->removed = na_alloc((na ? na : 1) * sizeof *out->removed);
    size_t i = 0, j = 0;
    while (i < na || j < nb) {
        int c;
        if (i >= na) c = 1;
        else if (j >= nb) c = -1;
        else c = fp_cmp(&a[i], &b[j]);
        if (c == 0) {
            i++;
            j++;
        } else if (c < 0) {
            out->removed[out->nremoved++] = fp_copy(&a[i++]);
        } else {
            out->added[out->nadded++] = fp_copy(&b[j++]);
        }
    }
    out->acceptable = true;
    for (size_t k = 0; k < out->nadded; k++)
        if (out->added[k].severity <= maximum_severity) out->acceptable = false;
    fp_free_all(a, na);
    fp_free_all(b, nb);
    return 0;
}

void diag_delta_free(DiagDelta *d) {
    fp_free_all(d->added, d->nadded);
    fp_free_all(d->removed, d->nremoved);
    memset(d, 0, sizeof *d);
}

int diag_require_fresh(const J *report, int64_t expected_version, const J **diagnostics) {
    const J *ver = report ? j_get(report, "version") : NULL;
    if (!report || !j_is_int(ver) || ver->i != expected_version)
        return na_fail("diagnostics_unavailable_or_stale");
    const J *d = j_get(report, "diagnostics");
    if (!d || d->k != J_ARR) return na_fail("invalid diagnostics payload");
    for (size_t i = 0; i < d->n; i++)
        if (d->items[i]->k != J_OBJ) return na_fail("invalid diagnostics payload");
    *diagnostics = d;
    return 0;
}
