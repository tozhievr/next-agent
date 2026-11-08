/* <ctype.h>: ASCII-only classification via a 128-entry table. */
#include <ctype.h>

#define CU 1   /* upper */
#define CL 2   /* lower */
#define CD 4   /* digit */
#define CX 8   /* xdigit */
#define CS 16  /* space */
#define CB 32  /* blank */
#define CC 64  /* cntrl */
#define CP 128 /* punct */

static const unsigned char tbl[128] = {
    /* 00-08 */ CC,CC,CC,CC,CC,CC,CC,CC,CC,
    /* 09 */ CC|CS|CB,
    /* 0A-0D */ CC|CS,CC|CS,CC|CS,CC|CS,
    /* 0E-1F */ CC,CC,CC,CC,CC,CC,CC,CC,CC,CC,CC,CC,CC,CC,CC,CC,CC,CC,
    /* 20 */ CS|CB,
    /* 21-2F */ CP,CP,CP,CP,CP,CP,CP,CP,CP,CP,CP,CP,CP,CP,CP,
    /* 30-39 */ CD|CX,CD|CX,CD|CX,CD|CX,CD|CX,CD|CX,CD|CX,CD|CX,CD|CX,CD|CX,
    /* 3A-40 */ CP,CP,CP,CP,CP,CP,CP,
    /* 41-46 */ CU|CX,CU|CX,CU|CX,CU|CX,CU|CX,CU|CX,
    /* 47-5A */ CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,CU,
    /* 5B-60 */ CP,CP,CP,CP,CP,CP,
    /* 61-66 */ CL|CX,CL|CX,CL|CX,CL|CX,CL|CX,CL|CX,
    /* 67-7A */ CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,CL,
    /* 7B-7E */ CP,CP,CP,CP,
    /* 7F */ CC,
};

static inline int cls(int c) { return (unsigned)c < 128u ? tbl[c] : 0; }

int isalnum(int c)  { return cls(c) & (CU | CL | CD); }
int isalpha(int c)  { return cls(c) & (CU | CL); }
int isblank(int c)  { return cls(c) & CB; }
int iscntrl(int c)  { return cls(c) & CC; }
int isdigit(int c)  { return cls(c) & CD; }
int isgraph(int c)  { return (unsigned)c - 0x21u < 0x5Eu; }
int islower(int c)  { return cls(c) & CL; }
int isprint(int c)  { return (unsigned)c - 0x20u < 0x5Fu; }
int ispunct(int c)  { return cls(c) & CP; }
int isspace(int c)  { return cls(c) & CS; }
int isupper(int c)  { return cls(c) & CU; }
int isxdigit(int c) { return cls(c) & CX; }
int tolower(int c)  { return (unsigned)c - 'A' < 26u ? c + 32 : c; }
int toupper(int c)  { return (unsigned)c - 'a' < 26u ? c - 32 : c; }
