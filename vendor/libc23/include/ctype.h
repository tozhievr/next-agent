/* <ctype.h> - C23 7.4 (libc23).  Only ASCII is classified, in every locale. */
#ifndef _LIBC23_CTYPE_H
#define _LIBC23_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int __c);
int isalpha(int __c);
int isblank(int __c);
int iscntrl(int __c);
int isdigit(int __c);
int isgraph(int __c);
int islower(int __c);
int isprint(int __c);
int ispunct(int __c);
int isspace(int __c);
int isupper(int __c);
int isxdigit(int __c);
int tolower(int __c);
int toupper(int __c);

#ifdef __cplusplus
}
#endif
#endif /* _LIBC23_CTYPE_H */
