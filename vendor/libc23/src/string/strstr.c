/* strstr: Crochemore-Perrin two-way algorithm, linear time. */
#include <string.h>

#define BITOP(a, b, op) ((a)[(size_t)(b) / (8 * sizeof *(a))] op (size_t)1 << ((size_t)(b) % (8 * sizeof *(a))))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static char *twoway(const unsigned char *h, const unsigned char *n, size_t l, size_t hlen) {
    size_t ip, jp, k, p, ms, p0, mem, mem0;
    size_t byteset[32 / sizeof(size_t)] = { 0 };
    size_t shift[256];

    /* Initialize the byte set and bad-character shifts within the known needle. */
    for (size_t i = 0; i < l; i++) {
        BITOP(byteset, n[i], |=);
        shift[n[i]] = i + 1;
    }

    /* maximal suffix, normal order */
    ip = (size_t)-1; jp = 0; k = p = 1;
    while (jp + k < l) {
        if (n[ip + k] == n[jp + k]) {
            if (k == p) { jp += p; k = 1; } else k++;
        } else if (n[ip + k] > n[jp + k]) { jp += k; k = 1; p = jp - ip; }
        else { ip = jp++; k = p = 1; }
    }
    ms = ip; p0 = p;

    /* maximal suffix, reversed order */
    ip = (size_t)-1; jp = 0; k = p = 1;
    while (jp + k < l) {
        if (n[ip + k] == n[jp + k]) {
            if (k == p) { jp += p; k = 1; } else k++;
        } else if (n[ip + k] < n[jp + k]) { jp += k; k = 1; p = jp - ip; }
        else { ip = jp++; k = p = 1; }
    }
    if (ip + 1 > ms + 1) ms = ip;
    else p = p0;

    /* periodic needle? */
    if (memcmp(n, n + p, ms + 1)) {
        mem0 = 0;
        p = MAX(ms, l - ms - 1) + 1;
    } else mem0 = l - p;
    mem = 0;

    size_t pos = 0;
    for (;;) {
        if (pos > hlen || hlen - pos < l) return NULL;
        /* check last byte first */
        if (BITOP(byteset, h[l - 1], &)) {
            k = l - shift[h[l - 1]];
            if (k) {
                if (k < mem) k = mem;
                h += k;
                pos += k;
                mem = 0;
                continue;
            }
        } else {
            h += l;
            pos += l;
            mem = 0;
            continue;
        }
        /* right half */
        for (k = MAX(ms + 1, mem); n[k] && n[k] == h[k]; k++) {}
        if (n[k]) { h += k - ms; pos += k - ms; mem = 0; continue; }
        /* left half */
        for (k = ms + 1; k > mem && n[k - 1] == h[k - 1]; k--) {}
        if (k <= mem) return (char *)h;
        h += p;
        pos += p;
        mem = mem0;
    }
}

char *strstr(const char *h, const char *n) {
    if (!n[0]) return (char *)h;
    size_t nlen = 0;
    while (n[nlen]) nlen++;
    size_t hlen = strlen(h);
    if (hlen < nlen) return NULL;
    return twoway((const unsigned char *)h, (const unsigned char *)n, nlen, hlen);
}
