#include <string.h>
#include <errno.h>
#include "libc.h"

static const struct { int num; const char *msg; } table[] = {
    { 0, "No error" },
    { EPERM, "Operation not permitted" },
    { ENOENT, "No such file or directory" },
    { ESRCH, "No such process" },
    { EINTR, "Interrupted function call" },
    { EIO, "Input/output error" },
    { ENXIO, "No such device or address" },
    { E2BIG, "Argument list too long" },
    { ENOEXEC, "Exec format error" },
    { EBADF, "Bad file descriptor" },
    { ECHILD, "No child processes" },
    { EAGAIN, "Resource temporarily unavailable" },
    { ENOMEM, "Cannot allocate memory" },
    { EACCES, "Permission denied" },
    { EFAULT, "Bad address" },
    { ENOTBLK, "Block device required" },
    { EBUSY, "Device or resource busy" },
    { EEXIST, "File exists" },
    { EXDEV, "Invalid cross-device link" },
    { ENODEV, "No such device" },
    { ENOTDIR, "Not a directory" },
    { EISDIR, "Is a directory" },
    { EINVAL, "Invalid argument" },
    { ENFILE, "Too many open files in system" },
    { EMFILE, "Too many open files" },
    { ENOTTY, "Inappropriate I/O control operation" },
    { ETXTBSY, "Text file busy" },
    { EFBIG, "File too large" },
    { ENOSPC, "No space left on device" },
    { ESPIPE, "Invalid seek" },
    { EROFS, "Read-only file system" },
    { EMLINK, "Too many links" },
    { EPIPE, "Broken pipe" },
    { EDOM, "Numerical argument out of domain" },
    { ERANGE, "Numerical result out of range" },
    { EDEADLK, "Resource deadlock avoided" },
    { ENAMETOOLONG, "File name too long" },
    { ENOLCK, "No locks available" },
    { ENOSYS, "Function not implemented" },
    { ENOTEMPTY, "Directory not empty" },
    { ELOOP, "Too many levels of symbolic links" },
    { ENOMSG, "No message of desired type" },
    { EIDRM, "Identifier removed" },
    { ENOSTR, "Device not a stream" },
    { ENODATA, "No data available" },
    { ETIME, "Timer expired" },
    { ENOSR, "Out of streams resources" },
    { ENOLINK, "Link has been severed" },
    { EPROTO, "Protocol error" },
    { EMULTIHOP, "Multihop attempted" },
    { EBADMSG, "Bad message" },
    { EOVERFLOW, "Value too large to be stored in data type" },
    { EILSEQ, "Invalid or incomplete multibyte or wide character" },
    { ENOTSOCK, "Socket operation on non-socket" },
    { EDESTADDRREQ, "Destination address required" },
    { EMSGSIZE, "Message too long" },
    { EPROTOTYPE, "Protocol wrong type for socket" },
    { ENOPROTOOPT, "Protocol not available" },
    { EPROTONOSUPPORT, "Protocol not supported" },
    { EOPNOTSUPP, "Operation not supported" },
    { EAFNOSUPPORT, "Address family not supported by protocol" },
    { EADDRINUSE, "Address already in use" },
    { EADDRNOTAVAIL, "Cannot assign requested address" },
    { ENETDOWN, "Network is down" },
    { ENETUNREACH, "Network is unreachable" },
    { ENETRESET, "Connection aborted by network" },
    { ECONNABORTED, "Software caused connection abort" },
    { ECONNRESET, "Connection reset by peer" },
    { ENOBUFS, "No buffer space available" },
    { EISCONN, "Transport endpoint is already connected" },
    { ENOTCONN, "Transport endpoint is not connected" },
    { ETIMEDOUT, "Connection timed out" },
    { ECONNREFUSED, "Connection refused" },
    { EHOSTUNREACH, "No route to host" },
    { EALREADY, "Operation already in progress" },
    { EINPROGRESS, "Operation now in progress" },
    { ESTALE, "Stale file handle" },
    { EDQUOT, "Disk quota exceeded" },
    { ECANCELED, "Operation canceled" },
    { EOWNERDEAD, "Owner died" },
    { ENOTRECOVERABLE, "State not recoverable" },
};

char *__libc_strerror_message(int e, char *b) LIBC_HIDDEN;
char *__libc_strerror_message(int e, char *b) {
    size_t lo = 0, hi = LIBC_ARRAY_LEN(table);
    /* the table is not sorted by value in all places; linear scan is fine (cold path) */
    for (lo = 0; lo < hi; lo++)
        if (table[lo].num == e) return (char *)table[lo].msg;

    static const char prefix[] = "Unknown error ";
    size_t i = 0;
    for (; prefix[i]; i++) b[i] = prefix[i];
    unsigned u = e < 0 ? 0u - (unsigned)e : (unsigned)e;
    char tmp[12]; int t = 0;
    do { tmp[t++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (e < 0) b[i++] = '-';
    while (t) b[i++] = tmp[--t];
    b[i] = 0;
    return b;
}

char *strerror(int e) {
    return __libc_strerror_message(e, libc_tls()->strerror_buf);
}


