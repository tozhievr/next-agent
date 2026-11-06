#include "lsp.h"
#include "platform.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------- POSIX shlex.split */
static int shlex_split(const char *s, StrList *out) {
    Buf cur;
    buf_init(&cur);
    bool have = false;
    for (const char *p = s; *p;) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (have) {
                char *t = buf_take(&cur);
                sl_push(out, t);
                free(t);
                have = false;
            }
            p++;
        } else if (c == '\\') {
            if (!p[1]) {
                buf_free(&cur);
                return na_fail("No escaped character");
            }
            buf_putc(&cur, p[1]);
            have = true;
            p += 2;
        } else if (c == '\'') {
            const char *q = strchr(p + 1, '\'');
            if (!q) {
                buf_free(&cur);
                return na_fail("No closing quotation");
            }
            buf_put(&cur, p + 1, (size_t)(q - p - 1));
            have = true;
            p = q + 1;
        } else if (c == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) p++;
                buf_putc(&cur, *p++);
            }
            if (!*p) {
                buf_free(&cur);
                return na_fail("No closing quotation");
            }
            have = true;
            p++;
        } else {
            buf_putc(&cur, c);
            have = true;
            p++;
        }
    }
    if (have) {
        char *t = buf_take(&cur);
        sl_push(out, t);
        free(t);
    }
    buf_free(&cur);
    return 0;
}

/* ------------------------------------------------- compilation database */
static char *resolve_in(const char *base, const char *value) {
    bool absolute = value[0] == '/' || value[0] == '\\' || (value[0] && value[1] == ':');
    Buf b;
    buf_init(&b);
    if (!absolute) {
        buf_puts(&b, base);
        buf_putc(&b, '/');
    }
    buf_puts(&b, value);
    char *joined = buf_take(&b);
    char *norm = path_normalize(joined);
    free(joined);
    return norm;
}

void comp_free(CompDb *db) {
    for (size_t i = 0; i < db->n; i++) {
        free(db->v[i].source);
        sl_free(&db->v[i].dirs);
    }
    free(db->v);
    memset(db, 0, sizeof *db);
}

static CompEntry *comp_slot(CompDb *db, const char *source) {
    for (size_t i = 0; i < db->n; i++)
        if (strcmp(db->v[i].source, source) == 0) return &db->v[i];
    if (db->n == db->cap) {
        db->cap = db->cap ? db->cap * 2 : 16;
        db->v = na_realloc(db->v, db->cap * sizeof *db->v);
    }
    CompEntry *e = &db->v[db->n++];
    e->source = na_strdup(source);
    sl_init(&e->dirs);
    return e;
}

const StrList *comp_dirs(const CompDb *db, const char *source) {
    for (size_t i = 0; i < db->n; i++)
        if (path_equal(db->v[i].source, source)) return &db->v[i].dirs;
    return NULL;
}

int comp_load(const char *root, size_t max_entries, CompDb *out) {
    memset(out, 0, sizeof *out);
    Buf p1, p2;
    buf_init(&p1);
    buf_init(&p2);
    buf_printf(&p1, "%s/compile_commands.json", root);
    buf_printf(&p2, "%s/build/compile_commands.json", root);
    const char *database = pf_is_file(p1.p) ? p1.p : pf_is_file(p2.p) ? p2.p : NULL;
    if (!database) {
        buf_free(&p1);
        buf_free(&p2);
        return 0;
    }
    Buf data;
    buf_init(&data);
    int rc = -1;
    J *doc = NULL;
    char *dbdir = NULL;
    if (pf_read_file(database, &data) != 0) goto out;
    doc = json_parse(data.p ? data.p : "", data.len);
    if (!doc) goto out;
    if (doc->k != J_ARR || doc->n > max_entries) {
        na_seterr("invalid_compilation_database");
        goto out;
    }
    const char *slash = strrchr(database, '/');
    dbdir = na_strndup(database, (size_t)(slash - database));
    for (size_t i = 0; i < doc->n; i++) {
        const J *e = doc->items[i];
        const J *dir = j_get(e, "directory");
        if (!e || e->k != J_OBJ || !j_is_str(dir)) {
            na_seterr("invalid_compilation_database_entry");
            goto out;
        }
        char *cwd = resolve_in(dbdir, dir->s);
        if (!path_inside_or_equal(root, cwd)) {
            free(cwd);
            na_seterr("compilation_directory_escapes_workspace");
            goto out;
        }
        const J *file = j_get(e, "file");
        if (!j_is_str(file)) {
            free(cwd);
            na_seterr("invalid_compilation_file");
            goto out;
        }
        char *source = resolve_in(cwd, file->s);
        if (!path_inside(root, source)) {
            free(cwd);
            free(source);
            na_seterr("compilation_file_escapes_workspace");
            goto out;
        }
        StrList args;
        sl_init(&args);
        const J *arguments = j_get(e, "arguments"), *command = j_get(e, "command");
        if (arguments) {
            bool ok = arguments->k == J_ARR;
            for (size_t k = 0; ok && k < arguments->n; k++) {
                if (!j_is_str(arguments->items[k])) ok = false;
                else sl_push(&args, arguments->items[k]->s);
            }
            if (!ok) {
                sl_free(&args);
                free(cwd);
                free(source);
                na_seterr("invalid_compilation_arguments");
                goto out;
            }
        } else if (j_is_str(command)) {
            if (shlex_split(command->s, &args) != 0) {
                sl_free(&args);
                free(cwd);
                free(source);
                goto out;
            }
        } else {
            sl_free(&args);
            free(cwd);
            free(source);
            na_seterr("invalid_compilation_command");
            goto out;
        }
        const char *rel = source + strlen(root);
        while (*rel == '/') rel++;
        CompEntry *slot = comp_slot(out, rel);
        for (size_t k = 0; k < args.n; k++) {
            const char *arg = args.v[k];
            const char *value = NULL;
            if (strcmp(arg, "-I") == 0 || strcmp(arg, "-iquote") == 0 || strcmp(arg, "/I") == 0) {
                if (++k >= args.n) {
                    sl_free(&args);
                    free(cwd);
                    free(source);
                    na_seterr("missing_include_directory");
                    goto out;
                }
                value = args.v[k];
            } else if (strncmp(arg, "-iquote", 7) == 0 && arg[7]) {
                value = arg + 7;
            } else if ((strncmp(arg, "-I", 2) == 0 || strncmp(arg, "/I", 2) == 0) && arg[2]) {
                value = arg + 2;
            }
            if (!value) continue;
            char *target = resolve_in(cwd, value);
            if (!path_inside_or_equal(root, target)) {
                free(target);
                sl_free(&args);
                free(cwd);
                free(source);
                na_seterr("include_directory_escapes_workspace");
                goto out;
            }
            if (!sl_has(&slot->dirs, target)) sl_push(&slot->dirs, target);
            free(target);
        }
        sl_free(&args);
        free(cwd);
        free(source);
    }
    rc = 0;
out:
    free(dbdir);
    json_free(doc);
    buf_free(&data);
    buf_free(&p1);
    buf_free(&p2);
    if (rc != 0) comp_free(out);
    return rc;
}

/* ------------------------------------------------------ dependency scan */
static bool is_source_suffix(const char *p) {
    return path_has_suffix_ci(p, ".c") || path_has_suffix_ci(p, ".cc") || path_has_suffix_ci(p, ".cpp") ||
           path_has_suffix_ci(p, ".cxx");
}

static bool is_header_suffix(const char *p) {
    return path_has_suffix_ci(p, ".h") || path_has_suffix_ci(p, ".hh") || path_has_suffix_ci(p, ".hpp") ||
           path_has_suffix_ci(p, ".hxx");
}

/* Recursively collect C/C++ files below `dir` (absolute). */
static int walk(const char *root, const char *dir, size_t max_files, StrList *files) {
    StrList f, d;
    sl_init(&f);
    sl_init(&d);
    if (pf_list_dir(dir, &f, &d) != 0) {
        sl_free(&f);
        sl_free(&d);
        return -1;
    }
    sl_sort(&f);
    sl_sort(&d);
    int rc = 0;
    for (size_t i = 0; i < f.n && rc == 0; i++) {
        if (!is_source_suffix(f.v[i]) && !is_header_suffix(f.v[i])) continue;
        Buf b;
        buf_init(&b);
        buf_printf(&b, "%s/%s", dir, f.v[i]);
        if (files->n + 1 > max_files) {
            buf_free(&b);
            rc = na_fail("dependency_scan_limit_exceeded");
            break;
        }
        sl_push(files, b.p);
        buf_free(&b);
    }
    for (size_t i = 0; i < d.n && rc == 0; i++) {
        if (strcmp(d.v[i], ".git") == 0 || strcmp(d.v[i], ".aed") == 0) continue;
        Buf b;
        buf_init(&b);
        buf_printf(&b, "%s/%s", dir, d.v[i]);
        rc = walk(root, b.p, max_files, files);
        buf_free(&b);
    }
    sl_free(&f);
    sl_free(&d);
    return rc;
}

int list_workspace_sources(const char *root, size_t max_files, StrList *abs_files) {
    return walk(root, root, max_files, abs_files);
}

/* Extract literal `#include "x"` targets from one document. */
static void find_includes(const char *text, size_t len, StrList *out) {
    size_t i = 0;
    while (i < len) {
        size_t eol = i;
        while (eol < len && text[eol] != '\n') eol++;
        size_t p = i;
        while (p < eol && (text[p] == ' ' || text[p] == '\t' || text[p] == '\r')) p++;
        if (p < eol && text[p] == '#') {
            p++;
            while (p < eol && (text[p] == ' ' || text[p] == '\t')) p++;
            if (eol - p >= 7 && strncmp(text + p, "include", 7) == 0) {
                p += 7;
                while (p < eol && (text[p] == ' ' || text[p] == '\t')) p++;
                if (p < eol && text[p] == '"') {
                    size_t q = p + 1;
                    while (q < eol && text[q] != '"') q++;
                    if (q < eol && q > p + 1) {
                        char *name = na_strndup(text + p + 1, q - p - 1);
                        sl_push(out, name);
                        free(name);
                    }
                }
            }
        }
        i = eol + 1;
    }
}

static char *dir_of(const char *abs_path) {
    const char *slash = strrchr(abs_path, '/');
    return na_strndup(abs_path, slash ? (size_t)(slash - abs_path) : 0);
}

static const char *rel_of(const char *root, const char *abs) {
    const char *r = abs + strlen(root);
    while (*r == '/') r++;
    return r;
}

int dependent_sources(const char *root, const StrList *changed, const DepOptions *opt, StrList *out) {
    size_t max_files = opt->max_files ? opt->max_files : 4096;
    StrSet changed_set, source_set;
    ss_init(&changed_set);
    ss_init(&source_set);
    StrList sources;
    sl_init(&sources);
    StrSet found;
    ss_init(&found);
    int rc = -1;
    bool any_header = false;
    for (size_t i = 0; i < changed->n; i++) {
        char *abs = resolve_in(root, changed->v[i]);
        if (!path_inside(root, abs)) {
            free(abs);
            na_seterr("path escapes workspace");
            goto out;
        }
        if (is_header_suffix(abs)) any_header = true;
        ss_add(&changed_set, abs);
        free(abs);
    }
    if (!any_header) {
        rc = 0;
        goto out;
    }
    if (walk(root, root, max_files, &sources) != 0) goto out;
    for (size_t i = 0; i < sources.n; i++) ss_add(&source_set, sources.v[i]);
    if (opt->snapshot_paths) {
        for (size_t i = 0; i < opt->snapshot_paths->n; i++) {
            char *target = resolve_in(root, opt->snapshot_paths->v[i]);
            if (!path_inside(root, target)) {
                free(target);
                na_seterr("snapshot path escapes workspace");
                goto out;
            }
            if ((is_source_suffix(target) || is_header_suffix(target)) && ss_add(&source_set, target)) {
                if (sources.n + 1 > max_files) {
                    free(target);
                    na_seterr("dependency_scan_limit_exceeded");
                    goto out;
                }
                sl_push(&sources, target);
            }
            free(target);
        }
    }
    for (size_t si = 0; si < sources.n; si++) {
        const char *source = sources.v[si];
        if (!is_source_suffix(source) || ss_has(&changed_set, source)) continue;
        const char *name = rel_of(root, source);
        const StrList *dirs = opt->include_dirs;
        if (opt->by_source) {
            const StrList *specific = comp_dirs(opt->by_source, name);
            dirs = specific; /* Python: .get(name, include_dirs) with an empty default for by-source */
        }
        StrList pending, no_dirs;
        sl_init(&pending);
        sl_init(&no_dirs);
        if (!dirs) dirs = &no_dirs;
        for (size_t k = 0; k < dirs->n; k++) {
            if (!path_inside_or_equal(root, dirs->v[k])) {
                sl_free(&pending);
                na_seterr("include_directory_escapes_workspace");
                goto out;
            }
        }
        sl_push(&pending, source);
        StrSet visited;
        ss_init(&visited);
        bool hit = false;
        while (pending.n > 0 && !hit) {
            char *current = pending.v[--pending.n];
            if (!ss_add(&visited, current)) {
                free(current);
                continue;
            }
            if (visited.n > max_files) {
                free(current);
                ss_free(&visited);
                sl_free(&pending);
                na_seterr("dependency_scan_limit_exceeded");
                goto out;
            }
            if (ss_has(&changed_set, current)) {
                free(current);
                hit = true;
                break;
            }
            if (!pf_is_file(current) && !ss_has(&source_set, current)) {
                free(current);
                continue;
            }
            char *text = NULL;
            size_t tlen = 0;
            int trc;
            if (opt->text) {
                trc = opt->text(opt->text_ctx, rel_of(root, current), &text, &tlen);
            } else {
                Buf b;
                buf_init(&b);
                trc = pf_read_file(current, &b);
                if (trc == 1) trc = na_fail("cannot read %s", current);
                tlen = b.len;
                text = buf_take(&b);
            }
            if (trc != 0) {
                free(current);
                ss_free(&visited);
                sl_free(&pending);
                goto out;
            }
            StrList incs;
            sl_init(&incs);
            find_includes(text, tlen, &incs);
            free(text);
            char *cdir = dir_of(current);
            for (size_t ii = 0; ii < incs.n; ii++) {
                size_t ncand = 1 + dirs->n;
                for (size_t ci = 0; ci < ncand; ci++) {
                    char *cand = resolve_in(ci == 0 ? cdir : dirs->v[ci - 1], incs.v[ii]);
                    if (!path_inside(root, cand)) {
                        free(cand);
                        free(cdir);
                        free(current);
                        sl_free(&incs);
                        ss_free(&visited);
                        sl_free(&pending);
                        na_seterr("include escapes workspace");
                        goto out;
                    }
                    if (pf_is_file(cand) || (opt->text && (ss_has(&changed_set, cand) || ss_has(&source_set, cand)))) {
                        sl_push(&pending, cand);
                        free(cand);
                        break;
                    }
                    free(cand);
                }
            }
            free(cdir);
            sl_free(&incs);
            free(current);
        }
        if (hit) ss_add(&found, name);
        ss_free(&visited);
        sl_free(&pending);
        sl_free(&no_dirs);
    }
    for (size_t i = 0; i < found.cap; i++)
        if (found.slots[i]) sl_push(out, found.slots[i]);
    sl_sort(out);
    rc = 0;
out:
    ss_free(&changed_set);
    ss_free(&source_set);
    ss_free(&found);
    sl_free(&sources);
    return rc;
}
