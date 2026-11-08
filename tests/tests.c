/* NextAgent test suite (C23, built with nextc). Run: build.ps1 test */
#include "../src/c23.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/lsp.h"
#include "../src/platform.h"
#include "../src/refactor.h"
#include "../src/rpc.h"
#include "../src/store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failed;
static const char *g_test;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        g_checks++;                                                                       \
        if (!(cond)) {                                                                    \
            g_failed++;                                                                   \
            printf("  FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond);        \
        }                                                                                 \
    } while (0)
#define CHECK_STR(a, b)                                                                   \
    do {                                                                                  \
        g_checks++;                                                                       \
        const char *_a = (a), *_b = (b);                                                  \
        if (!_a || strcmp(_a, _b) != 0) {                                                 \
            g_failed++;                                                                   \
            printf("  FAIL [%s] %s:%d: \"%s\" != \"%s\"\n", g_test, __FILE__, __LINE__,   \
                   _a ? _a : "(null)", _b);                                               \
        }                                                                                 \
    } while (0)
#define CHECK_ERR(rc, needle)                                                             \
    do {                                                                                  \
        g_checks++;                                                                       \
        if ((rc) == 0 || !strstr(na_errmsg(), needle)) {                                  \
            g_failed++;                                                                   \
            printf("  FAIL [%s] %s:%d: expected error \"%s\", got rc=%d \"%s\"\n", g_test, \
                   __FILE__, __LINE__, needle, (int)(rc), na_errmsg());                   \
        }                                                                                 \
    } while (0)

/* ------------------------------------------------------------ workspace */
static int g_ws_counter;
static char g_base[512];

static char *new_ws(void) {
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s/ws%llu_%d", g_base, (unsigned long long)pf_now_ms(), ++g_ws_counter);
    pf_mkdirs(tmp);
    return pf_fullpath(tmp);
}

static void put_file(const char *root, const char *rel, const char *text) {
    char p[800];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    char *full = path_normalize(p);
    char *slash = strrchr(full, '/');
    if (slash) {
        *slash = 0;
        pf_mkdirs(full);
        *slash = '/';
    }
    pf_write_atomic(full, text, strlen(text));
    free(full);
}

static char *get_file(const char *root, const char *rel) {
    char p[800];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    Buf b;
    buf_init(&b);
    if (pf_read_file(p, &b) != 0) {
        buf_free(&b);
        return NULL;
    }
    return buf_take(&b);
}

/* One-op edit helper: replace [start,end) in `path` with `text`. */
static int edit1(Store *st, const char *path, size_t start, size_t end, const char *text, int64_t if_rev,
                 const Validator *v, EditResult *res) {
    ChangeSet cs;
    cs_init(&cs);
    cs_add(&cs, start, end, text, strlen(text));
    cs_sort(&cs);
    const char *names[1] = {path};
    EditResult local;
    int rc = store_edit(st, names, &cs, 1, if_rev, "t", "test", v, res ? res : &local);
    cs_free(&cs);
    if (rc == 0 && !res) edit_result_free(&local);
    return rc;
}

static char *view_text(Store *st, const char *path) {
    Snapshot *s = NULL;
    if (store_view(st, path, &s) != 0) return NULL;
    char *t = na_strndup(s->text, s->len);
    snap_free(s);
    return t;
}

/* --------------------------------------------------------------- util */
static void test_util(void) {
    g_test = "util";
    uint8_t h[16];
    char hex[33];
    blake2b16("", 0, h);
    hex_encode(h, 16, hex);
    CHECK_STR(hex, "cae66941d9efbd404e4d88758ea67670");
    blake2b16("abc", 3, h);
    hex_encode(h, 16, hex);
    CHECK_STR(hex, "cf4ab791c62b8d2b2109c90275287816");
    char a200[200];
    memset(a200, 'a', sizeof a200);
    blake2b16(a200, sizeof a200, h);
    hex_encode(h, 16, hex);
    CHECK_STR(hex, "d63a46941ee0449fd1a7292005e08096");

    char *p = path_normalize("a/./b/../c");
    CHECK_STR(p, "a/c");
    free(p);
    p = path_normalize("C:\\x\\..\\y");
    CHECK_STR(p, "C:/y");
    free(p);
    CHECK(path_inside("D:/ws", "d:/WS/a.c"));
    CHECK(!path_inside("D:/ws", "D:/ws"));
    CHECK(!path_inside("D:/ws", "D:/ws2/a.c"));
    CHECK(path_inside_or_equal("D:/ws", "D:/ws"));
    CHECK(utf8_valid("a\xC3\xA9", 3));
    CHECK(!utf8_valid("\xC0\x80", 2));
    CHECK(!utf8_valid("\xED\xA0\x80", 3)); /* surrogate */
    CHECK(utf8_cp_count("a\xF0\x9F\x98\x80z", 6) == 3);
}

/* --------------------------------------------------------------- json */
static void test_json(void) {
    g_test = "json";
    const char *src = "{\"a\":[1,-2,true,null,\"x\\n\\u00e9\\ud83d\\ude00\"],\"b\":{\"c\":\"d\"}}";
    J *j = json_parse(src, strlen(src));
    CHECK(j != NULL);
    if (!j) return;
    const J *a = j_get(j, "a");
    CHECK(a && a->k == J_ARR && a->n == 5);
    CHECK(a->items[1]->i == -2);
    CHECK(a->items[4]->slen == 1 + 1 + 2 + 4); /* x \n é U+1F600 */
    char *out = json_dump(j);
    J *again = json_parse(out, strlen(out));
    char *out2 = json_dump(again);
    CHECK_STR(out, out2);
    free(out);
    free(out2);
    json_free(again);
    json_free(j);
    CHECK(json_parse("{} x", 4) == NULL);
    CHECK(json_parse("{\"a\":", 5) == NULL);
    CHECK(json_parse("\"\\ud800\"", 8) == NULL); /* lone surrogate */
}

/* --------------------------------------------------------------- core */
static void test_core(void) {
    g_test = "core";
    const char *text = "a\xF0\x9F\x98\x80" "b\nline2\n";
    Snapshot *s = snap_new(0, text, strlen(text));
    CHECK(s->ncp == 10 && s->len == 13);
    ChangeSet cs;
    cs_init(&cs);
    cs_add(&cs, 1, 2, "X", 1);
    cs_add(&cs, 3, 3, "!", 1);
    cs_sort(&cs);
    CHECK(cs_validate(&cs) == 0);
    Snapshot *after = NULL;
    CHECK(cs_apply(&cs, s, 1, &after) == 0);
    CHECK_STR(after->text, "aXb!\nline2\n");
    /* exact expectations */
    CHECK(after->rev == 1);
    ChangeSet inv;
    CHECK(cs_invert(&cs, s, &inv) == 0);
    Snapshot *back = NULL;
    CHECK(cs_apply(&inv, after, 2, &back) == 0);
    CHECK(memcmp(back->hash, s->hash, 16) == 0);
    size_t line, col;
    CHECK(snap_position(s, 4, &line, &col) == 0 && line == 2 && col == 0);
    size_t off;
    CHECK(snap_offset(s, 2, 3, &off) == 0 && off == 7);
    CHECK(cs_map_pos(&cs, 0, 0) == 0);
    CHECK(cs_map_pos(&cs, 2, 0) == 1); /* end of a replaced range collapses to its start */
    CHECK(cs_map_pos(&cs, 2, 1) == 2);
    CHECK(cs_map_pos(&cs, 4, 0) == 5);
    size_t added, removed;
    CHECK(cs_stats(&cs, s, &added, &removed) == 0 && added == 0 && removed == 0);
    snap_free(back);
    snap_free(after);
    cs_free(&inv);
    cs_free(&cs);

    cs_init(&cs);
    cs_add(&cs, 0, 3, "x", 1);
    cs_add(&cs, 2, 4, "y", 1);
    cs_sort(&cs);
    CHECK(cs_validate(&cs) != 0); /* overlap */
    cs_free(&cs);
    cs_init(&cs);
    cs_add(&cs, 0, 99, "x", 1);
    CHECK(cs_apply(&cs, s, 1, &after) != 0); /* out of range */
    cs_free(&cs);
    snap_free(s);
}

/* -------------------------------------------------------------- store */
static void test_store(void) {
    g_test = "store";
    char *ws = new_ws();
    put_file(ws, "a.txt", "hello world\n");
    Store *st = store_open(ws);
    CHECK(st != NULL);
    if (!st) return;
    CHECK(store_head(st) == 0);
    char *t = view_text(st, "a.txt");
    CHECK_STR(t, "hello world\n");
    free(t);

    CHECK_ERR(edit1(st, "a.txt", 0, 5, "HELLO", 7, NULL, NULL), "stale_rev");
    EditResult r;
    CHECK(edit1(st, "a.txt", 0, 5, "HELLO", 0, NULL, &r) == 0 && r.rev == 1);
    edit_result_free(&r);
    t = view_text(st, "a.txt");
    CHECK_STR(t, "HELLO world\n");
    free(t);
    char *disk = get_file(ws, "a.txt");
    CHECK_STR(disk, "hello world\n"); /* not flushed yet */
    free(disk);

    CHECK(edit1(st, "a.txt", 6, 11, "there", STORE_NO_REV, NULL, NULL) == 0); /* rev 2 */
    CHECK(store_head(st) == 2);
    /* undo a non-head revision: rev 1 rebases over rev 2 */
    CHECK(store_undo(st, 1, NULL, &r) == 0);
    edit_result_free(&r);
    t = view_text(st, "a.txt");
    CHECK_STR(t, "hello there\n");
    free(t);
    /* redo reapplies the undone head */
    CHECK(store_redo(st, NULL, &r) == 0);
    edit_result_free(&r);
    t = view_text(st, "a.txt");
    CHECK_STR(t, "HELLO there\n");
    free(t);

    /* overlapping undo conflicts */
    CHECK(edit1(st, "a.txt", 0, 5, "J", STORE_NO_REV, NULL, NULL) == 0);
    CHECK(edit1(st, "a.txt", 0, 1, "K", STORE_NO_REV, NULL, NULL) == 0);
    int64_t head = store_head(st);
    CHECK_ERR(store_undo(st, head - 1, NULL, &r), "undo_conflict");

    StrList written;
    sl_init(&written);
    CHECK(store_flush(st, NULL, 0, &written) == 0 && written.n == 1);
    sl_free(&written);
    disk = get_file(ws, "a.txt");
    t = view_text(st, "a.txt");
    CHECK(disk && t && strcmp(disk, t) == 0);
    free(disk);
    free(t);

    J *hist = store_history(st, 3);
    CHECK(hist && hist->k == J_ARR && hist->n == 3);
    json_free(hist);
    store_close(st);

    /* durability across reopen */
    st = store_open(ws);
    CHECK(st && store_head(st) == head);
    t = st ? view_text(st, "a.txt") : NULL;
    CHECK(t && strncmp(t, "K", 1) == 0);
    free(t);
    store_close(st);

    /* path safety */
    st = store_open(ws);
    Snapshot *s = NULL;
    CHECK_ERR(store_view(st, "../x.txt", &s), "");
    CHECK(store_view(st, ".aed/journal.aedj", &s) != 0);
    CHECK(store_view(st, "C:/Windows/win.ini", &s) != 0);
    store_close(st);
    free(ws);
}

static void test_journal_deltas(void) {
    g_test = "journal_deltas";
    char *ws = new_ws();
    Buf big;
    buf_init(&big);
    for (int i = 0; i < 2000; i++) buf_printf(&big, "int f%d(void) { return %d; }\n", i, i); /* ~56 KB */
    put_file(ws, "big.c", big.p);
    Store *st = store_open(ws);
    CHECK(st != NULL);
    for (int i = 0; i < 60; i++) CHECK(edit1(st, "big.c", 4, 5, i % 2 ? "g" : "h", STORE_NO_REV, NULL, NULL) == 0);
    char *expect = view_text(st, "big.c");
    store_close(st);
    char jp[800];
    snprintf(jp, sizeof jp, "%s/.aed/journal.aedj", ws);
    Buf jb;
    buf_init(&jb);
    CHECK(pf_read_file(jp, &jb) == 0);
    CHECK(jb.len < 3 * big.len); /* full-text journaling would be ~60x */
    buf_free(&jb);
    st = store_open(ws); /* replay deltas */
    CHECK(st != NULL && store_head(st) == 60);
    char *again = view_text(st, "big.c");
    CHECK_STR(again, expect);
    EditResult res;
    CHECK(store_undo(st, 0, NULL, &res) == 0);
    edit_result_free(&res);
    char *u = view_text(st, "big.c");
    CHECK(strncmp(u + 4, "g", 1) == 0 || strncmp(u + 4, "h", 1) == 0);
    free(u);
    free(again);
    free(expect);
    store_close(st);
    buf_free(&big);
    free(ws);
}

static void test_store_torn_tail_and_conflict(void) {
    g_test = "store-journal";
    char *ws = new_ws();
    put_file(ws, "a.txt", "abc\n");
    Store *st = store_open(ws);
    CHECK(edit1(st, "a.txt", 0, 1, "X", 0, NULL, NULL) == 0);
    CHECK(edit1(st, "a.txt", 1, 2, "Y", 1, NULL, NULL) == 0);
    store_close(st);
    /* append a torn record */
    char jp[800];
    snprintf(jp, sizeof jp, "%s/.aed/journal.aedj", ws);
    Buf data;
    buf_init(&data);
    CHECK(pf_read_file(jp, &data) == 0);
    size_t good = data.len;
    buf_free(&data);
    CHECK(pf_append_at(jp, good, "\x20\x00\x00\x00{\"t\":\"comm", 17) == 0);
    st = store_open(ws);
    CHECK(st && store_head(st) == 2);
    CHECK(st && edit1(st, "a.txt", 2, 3, "Z", 2, NULL, NULL) == 0); /* torn tail truncated */
    store_close(st);
    st = store_open(ws);
    CHECK(st && store_head(st) == 3);
    store_close(st);

    /* disk changed behind our back -> flush refuses */
    st = store_open(ws);
    CHECK(edit1(st, "a.txt", 0, 1, "Q", 3, NULL, NULL) == 0);
    put_file(ws, "a.txt", "external\n");
    StrList written;
    sl_init(&written);
    CHECK_ERR(store_flush(st, NULL, 0, &written), "disk_conflict");
    sl_free(&written);
    store_close(st);
    free(ws);
}

typedef struct {
    int calls, rechecks;
    bool reject;
} CountingValidator;

static int cv_validate(void *ctx, const ValidateDoc *docs, size_t n) {
    CountingValidator *c = ctx;
    c->calls++;
    (void)docs;
    (void)n;
    return c->reject ? na_fail("rejected_by_validator") : 0;
}
static int cv_recheck(void *ctx) {
    ((CountingValidator *)ctx)->rechecks++;
    return 0;
}

static void test_validator(void) {
    g_test = "validator";
    char *ws = new_ws();
    put_file(ws, "a.txt", "abc\n");
    Store *st = store_open(ws);
    CountingValidator cv = {0, 0, false};
    Validator v = {cv_validate, cv_recheck, &cv};
    CHECK(edit1(st, "a.txt", 0, 1, "X", 0, &v, NULL) == 0);
    CHECK(cv.calls == 1 && cv.rechecks == 1);
    cv.reject = true;
    CHECK_ERR(edit1(st, "a.txt", 0, 1, "Y", 1, &v, NULL), "rejected_by_validator");
    CHECK(store_head(st) == 1);
    store_close(st);
    free(ws);
}

/* ---------------------------------------------------------------- c23 */
static const char *SAMPLE =
    "#include <stdio.h>\n"
    "#define X 1\n"
    "typedef struct Pt { int x; int y; } Pt;\n"
    "enum Color { RED, GREEN };\n"
    "static int add(int a, int b) { return a + b; /* } */ }\n"
    "int add2(int a) { const char *s = \"}\"; return a; }\n"
    "int counter = 0;\n";

static void test_c23(void) {
    g_test = "c23";
    Snapshot *s = snap_new(0, SAMPLE, strlen(SAMPLE));
    SymbolList l;
    CHECK(c23_outline(s, false, &l) == 0);
    bool has_add = false, has_add2 = false, has_pt = false, has_counter = false;
    for (size_t i = 0; i < l.n; i++) {
        if (!strcmp(l.v[i].name, "add") && !strcmp(l.v[i].kind, "function")) has_add = true;
        if (!strcmp(l.v[i].name, "add2")) has_add2 = true;
        if (!strcmp(l.v[i].name, "Pt")) has_pt = true;
        if (!strcmp(l.v[i].name, "counter")) has_counter = true;
    }
    CHECK(has_add && has_add2 && has_pt && has_counter);
    symbols_free(&l);

    CHECK(c23_outline(s, true, &l) == 0);
    bool has_field = false;
    for (size_t i = 0; i < l.n; i++)
        if (!strcmp(l.v[i].name, "x") && !strcmp(l.v[i].kind, "field")) has_field = true;
    CHECK(has_field);
    bool has_color = false;
    for (size_t i = 0; i < l.n; i++)
        if (!strcmp(l.v[i].name, "Color") && !strcmp(l.v[i].kind, "enum")) has_color = true;
    CHECK(has_color);
    symbols_free(&l);

    Symbol sym;
    CHECK(c23_select(s, "add", "function", NULL, &sym) == 0);
    CHECK(strncmp(s->text + cp_to_byte(s->text, s->len, sym.start), "static int add", 14) == 0);
    free(sym.name);
    free(sym.kind);
    free(sym.scope);
    CHECK_ERR(c23_select(s, "nope", NULL, NULL, &sym), "symbol_not_found");

    ChangeSet cs;
    const char *repl = "int add(int a, int b) { return b + a; }";
    CHECK(c23_replace_symbol(s, "add", repl, strlen(repl), "function", NULL, &cs) == 0);
    Snapshot *after = NULL;
    CHECK(cs_apply(&cs, s, 1, &after) == 0);
    CHECK(strstr(after->text, "return b + a;") != NULL);
    CHECK(strstr(after->text, "int add2") != NULL);
    snap_free(after);
    cs_free(&cs);
    snap_free(s);

    const char *dup = "int f(void) { return 1; }\nint f(void) { return 2; }\n";
    s = snap_new(0, dup, strlen(dup));
    CHECK_ERR(c23_select(s, "f", NULL, NULL, &sym), "ambiguous_symbol");
    snap_free(s);
}

/* ---------------------------------------------------------------- lsp */
typedef struct {
    const char *data;
    size_t len, pos, chunk;
    int timeouts_before_data;
} FakeStream;

static int fake_read(void *ctx, void *buf, size_t cap, int timeout_ms) {
    FakeStream *f = ctx;
    (void)timeout_ms;
    if (f->timeouts_before_data > 0) {
        f->timeouts_before_data--;
        return 0;
    }
    if (f->pos >= f->len) return -1;
    size_t n = f->len - f->pos;
    if (n > f->chunk) n = f->chunk;
    if (n > cap) n = cap;
    memcpy(buf, f->data + f->pos, n);
    f->pos += n;
    return (int)n;
}

static void test_lsp_codec_framing(void) {
    g_test = "lsp-codec";
    const char *text = "a\xF0\x9F\x98\x80" "b\nxy";
    size_t len = strlen(text), line, ch, off;
    CHECK(lsp_offset_to_position(text, len, 3, ENC_UTF16, &line, &ch) == 0 && line == 0 && ch == 4);
    CHECK(lsp_offset_to_position(text, len, 3, ENC_UTF8, &line, &ch) == 0 && ch == 6);
    CHECK(lsp_offset_to_position(text, len, 3, ENC_UTF32, &line, &ch) == 0 && ch == 3);
    CHECK(lsp_offset_to_position(text, len, 5, ENC_UTF16, &line, &ch) == 0 && line == 1 && ch == 1);
    CHECK(lsp_position_to_offset(text, len, 0, 3, ENC_UTF16, &off) == 0 && off == 2);
    CHECK_ERR(lsp_position_to_offset(text, len, 0, 2, ENC_UTF16, &off), "splits");
    CHECK_ERR(lsp_position_to_offset(text, len, 5, 0, ENC_UTF16, &off), "out of bounds");
    PosEnc e;
    CHECK(pos_enc_parse("utf-16", &e) == 0 && e == ENC_UTF16);
    CHECK(pos_enc_parse("ebcdic", &e) != 0);

    g_test = "lsp-framing";
    Buf framed;
    buf_init(&framed);
    const char *s1 = "{\"id\":1}", *s2 = "{\"id\":2,\"x\":\"\\u00e9\"}";
    J *m1 = json_parse(s1, strlen(s1)), *m2 = json_parse(s2, strlen(s2));
    CHECK(m1 && m2);
    CHECK(lsp_frame_message(m1, &framed) == 0 && lsp_frame_message(m2, &framed) == 0);
    json_free(m1);
    json_free(m2);
    FakeStream fs = {framed.p, framed.len, 0, 3, 2}; /* 3-byte chunks, 2 timeouts first */
    LspReader r;
    lsp_reader_init(&r, fake_read, &fs);
    J *out = NULL;
    CHECK(lsp_read_message(&r, 0, &out) == 0); /* timeout loses nothing */
    CHECK(lsp_read_message(&r, 0, &out) == 0);
    int rc = lsp_read_message(&r, 100, &out);
    CHECK(rc == 1 && out && j_get(out, "id")->i == 1);
    json_free(out);
    rc = lsp_read_message(&r, 100, &out);
    CHECK(rc == 1 && out && j_get(out, "id")->i == 2);
    json_free(out);
    CHECK(lsp_read_message(&r, 100, &out) == 2); /* clean EOF */
    lsp_reader_free(&r);

    const char *bad[] = {"Content-Length: 5\r\nContent-Length: 5\r\n\r\n{}   ", "Content-Length: x\r\n\r\n",
                         "X: 1\r\n\r\n{}", "Content-Length: 99999999999\r\n\r\n"};
    for (size_t i = 0; i < 4; i++) {
        FakeStream b = {bad[i], strlen(bad[i]), 0, 64, 0};
        lsp_reader_init(&r, fake_read, &b);
        CHECK(lsp_read_message(&r, 100, &out) == -1);
        lsp_reader_free(&r);
    }
    FakeStream trunc = {"Content-Length: 10\r\n\r\n{}", 25, 0, 64, 0};
    lsp_reader_init(&r, fake_read, &trunc);
    CHECK(lsp_read_message(&r, 100, &out) == -1);
    lsp_reader_free(&r);
    buf_free(&framed);
}

static J *parse(const char *s) { return json_parse(s, strlen(s)); }

static void test_lsp_diag(void) {
    g_test = "lsp-diag";
    J *before = parse("[{\"severity\":1,\"code\":\"a\",\"message\":\"m1\"},{\"severity\":2,\"message\":\"w\"}]");
    J *same = parse("[{\"severity\":2,\"message\":\"w\"},{\"severity\":1,\"code\":\"a\",\"message\":\"m1\"}]");
    J *worse = parse("[{\"severity\":1,\"code\":\"a\",\"message\":\"m1\"},{\"severity\":1,\"code\":\"a\",\"message\":\"m1\"}]");
    J *warn = parse("[{\"severity\":1,\"code\":\"a\",\"message\":\"m1\"},{\"severity\":2,\"message\":\"w\"},{\"severity\":3,\"message\":\"new\"}]");
    DiagDelta d;
    CHECK(diag_compare(before, same, 1, &d) == 0 && d.acceptable && d.nadded == 0 && d.nremoved == 0);
    diag_delta_free(&d);
    CHECK(diag_compare(before, worse, 1, &d) == 0 && !d.acceptable && d.nadded == 1 && d.nremoved == 1);
    diag_delta_free(&d);
    CHECK(diag_compare(before, warn, 1, &d) == 0 && d.acceptable && d.nadded == 1);
    diag_delta_free(&d);
    CHECK(diag_compare(before, warn, 3, &d) == 0 && !d.acceptable);
    diag_delta_free(&d);
    CHECK(diag_compare(before, same, 9, &d) != 0);
    J *bad = parse("{}");
    CHECK(diag_compare(bad, same, 1, &d) != 0);

    J *rep = parse("{\"version\":2,\"diagnostics\":[]}");
    const J *dd;
    CHECK(diag_require_fresh(rep, 2, &dd) == 0);
    CHECK_ERR(diag_require_fresh(rep, 3, &dd), "stale");
    CHECK_ERR(diag_require_fresh(NULL, 1, &dd), "stale");
    json_free(rep);
    json_free(bad);
    json_free(before);
    json_free(same);
    json_free(worse);
    json_free(warn);
}

static void test_lsp_build(void) {
    g_test = "lsp-build";
    char *ws = new_ws();
    put_file(ws, "src/a.c", "#include \"util.h\"\nint a(void){return 0;}\n");
    put_file(ws, "src/b.c", "#include \"other.h\"\n");
    put_file(ws, "src/util.h", "#include \"deep.h\"\n");
    put_file(ws, "inc/deep.h", "int deep;\n");
    put_file(ws, "inc/other.h", "int other;\n");
    char cc[1200];
    snprintf(cc, sizeof cc,
             "[{\"directory\":\"%s\",\"file\":\"src/a.c\",\"command\":\"cc -I \\\"%s/inc\\\" -c src/a.c\"},"
             "{\"directory\":\"%s\",\"file\":\"src/b.c\",\"arguments\":[\"cc\",\"-Iinc\",\"b.c\"]}]",
             ws, ws, ws);
    put_file(ws, "compile_commands.json", cc);
    CompDb db;
    CHECK(comp_load(ws, 100, &db) == 0 && db.n == 2);
    const StrList *dirs = comp_dirs(&db, "src/a.c");
    CHECK(dirs && dirs->n == 1);
    comp_free(&db);

    StrList changed, deps;
    sl_init(&changed);
    sl_init(&deps);
    sl_push(&changed, "inc/deep.h");
    DepOptions o;
    memset(&o, 0, sizeof o);
    CHECK(comp_load(ws, 100, &db) == 0);
    o.by_source = &db;
    CHECK(dependent_sources(ws, &changed, &o, &deps) == 0);
    CHECK(deps.n == 1 && strcmp(deps.v[0], "src/a.c") == 0);
    sl_free(&deps);
    sl_free(&changed);
    comp_free(&db);

    /* escape attempts fail closed */
    snprintf(cc, sizeof cc, "[{\"directory\":\"%s/..\",\"file\":\"x.c\",\"arguments\":[\"cc\"]}]", ws);
    put_file(ws, "compile_commands.json", cc);
    CHECK_ERR(comp_load(ws, 100, &db), "escapes");
    put_file(ws, "compile_commands.json", "{}");
    CHECK(comp_load(ws, 100, &db) != 0);
    free(ws);
}

/* A scripted LSP double: reports one error diagnostic when the text holds "BAD". */
typedef struct {
    int opened, closed, changed;
    char versions[8][2];
} FakeLsp;

static J *fake_report(const char *text, int64_t version) {
    J *r = j_obj(), *diags = j_arr();
    if (strstr(text, "BAD")) {
        J *d = j_obj();
        j_set(d, "severity", j_int(1));
        j_set(d, "message", j_str("bad token"));
        j_push(diags, d);
    }
    j_set(r, "version", j_int(version));
    j_set(r, "diagnostics", diags);
    return r;
}

typedef struct {
    char *text[16];
    char *uri[16];
    int64_t version[16];
    size_t n;
    int opens;
} FakeState;

static size_t fake_find(FakeState *s, const char *uri) {
    for (size_t i = 0; i < s->n; i++)
        if (!strcmp(s->uri[i], uri)) return i;
    return (size_t)-1;
}
static int f_open(void *c, const char *abs, const char *t, size_t len, int64_t v, char **uri) {
    FakeState *s = c;
    s->opens++;
    *uri = lsp_path_to_uri(abs);
    size_t i = s->n++;
    s->uri[i] = na_strdup(*uri);
    s->text[i] = na_strndup(t, len);
    s->version[i] = v;
    return 0;
}
static int f_change(void *c, const char *uri, const char *t, size_t len, int64_t v) {
    FakeState *s = c;
    size_t i = fake_find(s, uri);
    if (i == (size_t)-1) return na_fail("unknown uri");
    free(s->text[i]);
    s->text[i] = na_strndup(t, len);
    s->version[i] = v;
    return 0;
}
static int f_wait(void *c, const char *uri, int64_t v, J **rep) {
    FakeState *s = c;
    size_t i = fake_find(s, uri);
    if (i == (size_t)-1 || s->version[i] != v) return na_fail("diagnostics_unavailable_or_stale");
    *rep = fake_report(s->text[i], v);
    return 0;
}
static int f_close(void *c, const char *uri) {
    FakeState *s = c;
    size_t i = fake_find(s, uri);
    if (i == (size_t)-1) return 0;
    free(s->uri[i]);
    free(s->text[i]);
    s->n--;
    s->uri[i] = s->uri[s->n];
    s->text[i] = s->text[s->n];
    s->version[i] = s->version[s->n];
    return 0;
}

static void test_gate(void) {
    g_test = "gate";
    char *ws = new_ws();
    put_file(ws, "a.c", "#include \"a.h\"\nint a(void){return 0;}\n");
    put_file(ws, "a.h", "int a(void);\n");
    Store *st = store_open(ws);
    FakeState fake;
    memset(&fake, 0, sizeof fake);
    LspOps ops = {f_open, f_change, f_wait, f_close, &fake};
    DiagnosticGate *g = gate_new(ws, ops, st, 1);
    Validator v = gate_validator(g);

    CHECK_ERR(edit1(st, "a.c", 0, 0, "BAD\n", 0, &v, NULL), "diagnostics_regression");
    CHECK(store_head(st) == 0);
    CHECK(fake.n == 0); /* every document closed again */
    CHECK(edit1(st, "a.c", 0, 0, "/* fine */\n", 0, &v, NULL) == 0);
    /* a header edit re-validates the translation unit that includes it */
    int opens_before = fake.opens;
    CHECK(edit1(st, "a.h", 0, 0, "BAD\n", 1, &v, NULL) != 0 || fake.opens > opens_before);
    /* non-C files bypass the gate */
    put_file(ws, "n.txt", "x\n");
    int opens = fake.opens;
    CHECK(edit1(st, "n.txt", 0, 1, "BAD", STORE_NO_REV, &v, NULL) == 0);
    CHECK(fake.opens == opens);
    gate_free(g);
    store_close(st);
    free(ws);
}

static void test_gate_dependents(void) {
    g_test = "gate-dependents";
    char *ws = new_ws();
    put_file(ws, "m.c", "#include \"m.h\"\nint m(void){return 0;}\n");
    put_file(ws, "m.h", "int m(void);\n");
    Store *st = store_open(ws);
    FakeState fake;
    memset(&fake, 0, sizeof fake);
    LspOps ops = {f_open, f_change, f_wait, f_close, &fake};
    DiagnosticGate *g = gate_new(ws, ops, st, 1);
    Validator v = gate_validator(g);
    /* m.h now "breaks" - the fake only inspects text of each document, so the
     * header's own diagnostics must reject it. */
    CHECK_ERR(edit1(st, "m.h", 0, 0, "BAD\n", 0, &v, NULL), "diagnostics_regression");
    CHECK(fake.opens >= 2); /* header + dependent translation unit were opened */
    gate_free(g);
    store_close(st);
    free(ws);
}

/* ---------------------------------------------------------------- rpc */
static J *call(Rpc *r, const char *req) {
    J *q = parse(req);
    J *a = rpc_dispatch(r, q);
    json_free(q);
    return a;
}

static bool history_undo_ok(Store *st) {
    EditResult r;
    if (store_undo(st, 0, NULL, &r) != 0) return false;
    edit_result_free(&r);
    char *a = view_text(st, "a.c");
    bool ok = a && strstr(a, "int calc(int x)") != NULL && strstr(a, "area") == NULL;
    free(a);
    return ok;
}

static void test_refactor(void) {
    g_test = "refactor";
    char *ws = new_ws();
    put_file(ws, "a.h", "#ifndef A_H\n#define A_H\nint calc(int x);\nvoid log_it(const char *fmt, ...);\n#endif\n");
    put_file(ws, "a.c",
             "/* calc(1) in a comment, \xc3\xa9\xc3\xa8 */\n#include \"a.h\"\n#include <stdio.h>\n"
             "int calc(int x) { return x * 2; }\n"
             "int run(void) {\n  const char *s = \"calc(2)\";\n  int v = calc(calc(3) + 1);\n"
             "  struct { int calc; } m; m.calc = 4;\n  return v + m.calc + (s != 0);\n}\n"
             "void log_it(const char *fmt, ...) { (void)fmt; }\n"
             "int other(void) { log_it(\"x\", 1, 2); return calc(0); }\n");
    Store *st = store_open(ws);
    CHECK(st != NULL);
    J *o = NULL;
    CHECK(rf_references(st, "calc", NULL, true, &o) == 0);
    CHECK(o && j_get(o, "count")->i == 8); /* decl, def, 3 calls, field decl, 2 member uses; none in comment/string */
    json_free(o);
    CHECK(rf_references(st, "calc", NULL, false, &o) == 0 && j_get(o, "count")->i == 6);
    json_free(o);
    CHECK_ERR(rf_references(st, "1bad", NULL, true, &o), "invalid parameter");

    /* rename */
    EditResult res;
    RfCounts c;
    CHECK_ERR(rf_rename(st, "calc", "if", NULL, true, false, STORE_NO_REV, NULL, "t", NULL, &res, &c), "non-keyword");
    CHECK_ERR(rf_rename(st, "calc", "other", NULL, true, false, STORE_NO_REV, NULL, "t", NULL, &res, &c), "name_conflict");
    CHECK_ERR(rf_rename(st, "calc", "area", NULL, true, false, 5, NULL, "t", NULL, &res, &c), "stale_rev");
    CHECK_ERR(rf_rename(st, "nosuch", "area", NULL, true, false, STORE_NO_REV, NULL, "t", NULL, &res, &c), "symbol_not_found");
    CHECK(rf_rename(st, "calc", "area", NULL, true, false, 0, "rn", "t", NULL, &res, &c) == 0);
    CHECK(res.rev == 1 && res.paths.n == 2 && c.sites == 8);
    edit_result_free(&res);
    char *a = view_text(st, "a.c");
    CHECK(strstr(a, "int area(int x)") != NULL);
    CHECK(strstr(a, "area(area(3) + 1)") != NULL);
    CHECK(strstr(a, "/* calc(1) in a comment") != NULL); /* comment untouched */
    CHECK(strstr(a, "\"calc(2)\"") != NULL);             /* string untouched */
    CHECK(strstr(a, "m.area = 4") != NULL);
    free(a);
    CHECK(history_undo_ok(st));

    /* add_param: definition, prototype and 4 call sites; offsets after multibyte text */
    CHECK_ERR(rf_add_param(st, "calc", "int mode", NULL, -1, NULL, STORE_NO_REV, NULL, "t", NULL, &res, &c), "default");
    CHECK_ERR(rf_add_param(st, "calc", "int mode", "0", 9, NULL, STORE_NO_REV, NULL, "t", NULL, &res, &c), "position out of range");
    CHECK_ERR(rf_add_param(st, "nofn", "int m", "0", -1, NULL, STORE_NO_REV, NULL, "t", NULL, &res, &c), "symbol_not_found");
    CHECK(rf_add_param(st, "calc", "int mode", "0", -1, NULL, STORE_NO_REV, NULL, "t", NULL, &res, &c) == 0);
    CHECK(c.sites == 2 && c.calls == 3); /* run: 2 calls, other: 1 call, member use has no parens -> not counted */
    edit_result_free(&res);
    a = view_text(st, "a.c");
    CHECK(strstr(a, "int calc(int x, int mode) {") != NULL);
    CHECK(strstr(a, "calc(calc(3, 0) + 1, 0)") != NULL);
    CHECK(strstr(a, "return calc(0, 0);") != NULL);
    CHECK(strstr(a, "/* calc(1) in a comment") != NULL);
    free(a);
    char *h = view_text(st, "a.h");
    CHECK(strstr(h, "int calc(int x, int mode);") != NULL);
    free(h);
    /* position 0 and variadic: new parameter goes before `...` */
    CHECK(rf_add_param(st, "log_it", "int level", "3", 0, NULL, STORE_NO_REV, NULL, "t", NULL, &res, &c) == 0);
    edit_result_free(&res);
    CHECK(rf_add_param(st, "log_it", "const char *tag", "\"t\"", -1, NULL, STORE_NO_REV, NULL, "t", NULL, &res, &c) == 0);
    edit_result_free(&res);
    a = view_text(st, "a.c");
    CHECK(strstr(a, "void log_it(int level, const char *fmt, const char *tag, ...)") != NULL);
    CHECK(strstr(a, "log_it(3, \"x\", \"t\", 1, 2)") != NULL);
    free(a);
    /* (void) and () parameter lists */
    put_file(ws, "v.c", "int z(void);\nint z(void) { return 1; }\nint y(void) { return z(); }\nint w() { return z(); }\n");
    CHECK(rf_add_param(st, "z", "int k", "5", -1, NULL, STORE_NO_REV, NULL, "t", NULL, &res, &c) == 0);
    edit_result_free(&res);
    char *v = view_text(st, "v.c");
    CHECK_STR(v, "int z(int k);\nint z(int k) { return 1; }\nint y(void) { return z(5); }\nint w() { return z(5); }\n");
    free(v);
    CHECK(rf_add_param(st, "w", "int q", "0", -1, NULL, STORE_NO_REV, NULL, "t", NULL, &res, &c) == 0);
    edit_result_free(&res);
    v = view_text(st, "v.c");
    CHECK(strstr(v, "int w(int q) {") != NULL);
    free(v);

    /* add_include */
    bool changed;
    CHECK(rf_add_include(st, "a.c", "stdlib.h", true, STORE_NO_REV, NULL, "t", NULL, &res, &changed) == 0 && changed);
    edit_result_free(&res);
    a = view_text(st, "a.c");
    CHECK(strstr(a, "#include <stdio.h>\n#include <stdlib.h>\nint calc") != NULL);
    free(a);
    CHECK(rf_add_include(st, "a.c", "stdlib.h", true, STORE_NO_REV, NULL, "t", NULL, &res, &changed) == 0 && !changed);
    CHECK(res.paths.n == 0);
    edit_result_free(&res);
    CHECK(rf_add_include(st, "a.h", "types.h", false, STORE_NO_REV, NULL, "t", NULL, &res, &changed) == 0 && changed);
    edit_result_free(&res);
    h = view_text(st, "a.h");
    CHECK(strstr(h, "#define A_H\n#include \"types.h\"\nint calc") != NULL); /* after the include guard */
    free(h);
    put_file(ws, "crlf.c", "int q;\r\nint r;\r\n");
    CHECK(rf_add_include(st, "crlf.c", "q.h", false, STORE_NO_REV, NULL, "t", NULL, &res, &changed) == 0);
    edit_result_free(&res);
    char *cr = view_text(st, "crlf.c");
    CHECK_STR(cr, "#include \"q.h\"\r\n\r\nint q;\r\nint r;\r\n");
    free(cr);
    CHECK_ERR(rf_add_include(st, "a.c", "bad\"name.h", false, STORE_NO_REV, NULL, "t", NULL, &res, &changed), "invalid parameter");

    /* find_symbols and unflushed documents */
    CHECK(edit1(st, "n.c", 0, 0, "int calc_extra(void) { return 0; }\n", STORE_NO_REV, NULL, NULL) == 0);
    CHECK(rf_find_symbols(st, "CALC", NULL, 0, &o) == 0);
    CHECK(j_get(o, "count")->i >= 1 && !j_get(o, "truncated")->b);
    json_free(o);
    CHECK(rf_find_symbols(st, "", "function", 2, &o) == 0 && j_get(o, "truncated")->b && j_get(o, "count")->i == 2);
    json_free(o);
    CHECK(rf_references(st, "calc_extra", NULL, true, &o) == 0 && j_get(o, "count")->i == 1); /* sees the unflushed file */
    json_free(o);
    store_close(st);
    free(ws);
}

static void test_rpc(void) {
    g_test = "rpc";
    char *ws = new_ws();
    put_file(ws, "a.c", "int f(void) { return 1; }\n");
    Store *st = store_open(ws);
    Rpc r;
    memset(&r, 0, sizeof r);
    r.store = st;
    J *a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"open\"}");
    CHECK(a && j_get(j_get(a, "result"), "rev")->i == 0);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"method\":\"open\"}");
    CHECK(a == NULL); /* notification */
    a = call(&r, "{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"open\"}");
    CHECK(j_get(j_get(a, "error"), "code")->i == -32600);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":true,\"method\":\"open\"}");
    CHECK(j_get(j_get(a, "error"), "code")->i == -32600);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"zzz\"}");
    CHECK(j_get(j_get(a, "error"), "code")->i == -32601);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"view\",\"params\":[]}");
    CHECK(j_get(j_get(a, "error"), "code")->i == -32602);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"view\",\"params\":{\"target\":5}}");
    CHECK(j_get(j_get(a, "error"), "code")->i == -32602);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"edit\",\"params\":{\"ops\":[{\"path\":\"a.c\",\"start\":-1,\"end\":0,\"text\":\"\"}]}}");
    CHECK(j_get(a, "error") != NULL);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"edit\",\"params\":{\"ops\":[{\"path\":\"a.c\",\"start\":4,\"end\":5,\"text\":\"g\"}],\"if_rev\":0}}");
    CHECK(a && j_get(j_get(a, "result"), "rev")->i == 1);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"replace_symbol\",\"params\":{\"target\":\"a.c\",\"name\":\"g\",\"replacement\":\"int g(void) { return 2; }\",\"if_rev\":0}}");
    CHECK(j_get(a, "error") != NULL); /* stale head */
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"replace_symbol\",\"params\":{\"target\":\"a.c\",\"name\":\"g\",\"replacement\":\"int g(void) { return 2; }\",\"if_rev\":1}}");
    CHECK(a && j_get(j_get(a, "result"), "rev")->i == 2);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"history\",\"params\":{\"op\":\"undo\"}}");
    CHECK(a && j_get(j_get(a, "result"), "rev")->i == 3);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"history\",\"params\":{\"op\":\"redo\"}}");
    CHECK(a && j_get(j_get(a, "result"), "rev")->i == 4);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"history\",\"params\":{\"op\":\"bogus\"}}");
    CHECK(j_get(a, "error") != NULL);
    json_free(a);
    a = call(&r, "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"sync\"}");
    CHECK(a && j_get(j_get(a, "result"), "ok")->b);
    json_free(a);
    char *disk = get_file(ws, "a.c");
    CHECK(disk && strstr(disk, "return 2;"));
    free(disk);

    /* MCP bridge */
    J *q = parse("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}");
    a = rpc_mcp_dispatch(&r, q);
    CHECK(a && j_get(j_get(a, "result"), "tools")->n == 12);
    json_free(a);
    json_free(q);
    q = parse("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"view\",\"arguments\":{\"target\":\"../nope.c\"}}}");
    a = rpc_mcp_dispatch(&r, q);
    CHECK(a && j_get(j_get(a, "result"), "isError")->b);
    json_free(a);
    json_free(q);
    store_close(st);
    free(ws);
}

/* ------------------------------------------------------- live clangd */
static void test_live_clangd(void) {
    g_test = "clangd-live";
    char *exe = pf_which("clangd");
    if (!exe) {
        printf("  skip: clangd not on PATH\n");
        return;
    }
    free(exe);
    char *ws = new_ws();
    put_file(ws, "a.c", "int add(int a, int b) { return a + b; }\n");
    put_file(ws, "a.h", "int add(int a, int b);\n");
    put_file(ws, "b.c", "#include \"a.h\"\nint use(void) { return add(1, 2); }\n");
    Store *st = store_open(ws);
    ClangdClient *c = clangd_start(ws, "clangd", 30000);
    CHECK(c != NULL);
    if (!c) {
        printf("  clangd start failed: %s\n", na_errmsg());
        store_close(st);
        free(ws);
        return;
    }
    DiagnosticGate *g = gate_new(ws, clangd_ops(c), st, 1);
    Validator v = gate_validator(g);
    CHECK_ERR(edit1(st, "a.c", 26, 35, "return nope;", 0, &v, NULL), "diagnostics_regression");
    CHECK(store_head(st) == 0);
    CHECK(edit1(st, "a.c", 4, 7, "sum", 0, &v, NULL) == 0);
    /* a header parse error is rejected (also seen through the dependent b.c) */
    CHECK_ERR(edit1(st, "a.h", 0, 21, "int add(int a) = ;", 1, &v, NULL), "diagnostics_regression");
    CHECK(store_head(st) == 1);
    gate_free(g);
    clangd_close(c);
    store_close(st);
    free(ws);
}

#define RUN(f)                \
    do {                      \
        printf("[%s]\n", #f); \
        fflush(stdout);       \
        f();                  \
        fflush(stdout);       \
    } while (0)

int main(void) {
    const char *tmp = getenv("TEMP");
    snprintf(g_base, sizeof g_base, "%s/nextagent-tests", tmp ? tmp : ".");
    pf_mkdirs(g_base);
    RUN(test_util);
    RUN(test_json);
    RUN(test_core);
    RUN(test_store);
    RUN(test_journal_deltas);
    RUN(test_store_torn_tail_and_conflict);
    RUN(test_validator);
    RUN(test_c23);
    RUN(test_refactor);
    RUN(test_lsp_codec_framing);
    RUN(test_lsp_diag);
    RUN(test_lsp_build);
    RUN(test_gate);
    RUN(test_gate_dependents);
    RUN(test_rpc);
    RUN(test_live_clangd);
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
