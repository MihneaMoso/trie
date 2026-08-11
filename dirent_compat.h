/**
 * dirent_compat.h — Cross-platform POSIX directory iteration
 *
 * On Linux / macOS this is a transparent pass-through to the system <dirent.h>.
 * On Windows it implements the full opendir / readdir / closedir / rewinddir
 * API on top of Win32's FindFirstFile / FindNextFile / FindClose family.
 *
 * Drop-in usage (identical on every platform):
 *
 *   #include "dirent_compat.h"
 *
 *   DIR *dir = opendir("/some/path");   // or "C:\\some\\path" on Windows
 *   if (!dir) { perror("opendir"); return 1; }
 *
 *   struct dirent *entry;
 *   while ((entry = readdir(dir)) != NULL) {
 *       if (entry->d_type == DT_DIR)
 *           printf("[DIR] %s\n", entry->d_name);
 *       else
 *           printf("      %s\n", entry->d_name);
 *   }
 *   closedir(dir);
 *
 * Supported d_type values:
 *   DT_REG     regular file
 *   DT_DIR     directory
 *   DT_LNK     symbolic link (Windows reparse points are reported as DT_LNK)
 *   DT_UNKNOWN type could not be determined
 *
 * Windows-specific caveats:
 *   * d_ino is always 0 — inodes are not a Windows concept.
 *   * d_name is always narrow (ANSI/UTF-8); no wchar_t variant is exposed.
 *   * rewinddir() is emulated by closing and reopening the search handle,
 *     so directory modifications between opendir() and rewinddir() may be
 *     visible in the rewound scan.
 *   * All functions are static inline — no separate compilation unit needed.
 */

#ifndef DIRENT_COMPAT_H
#define DIRENT_COMPAT_H

/* =========================================================================
 * Non-Windows: just delegate to the system header — zero overhead.
 * ========================================================================= */
#if !defined(_WIN32)

#   include <dirent.h>
#   include <sys/types.h>

/* =========================================================================
 * Windows: full POSIX-compatible emulation on top of Win32.
 * ========================================================================= */
#else /* _WIN32 */

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * d_type constants — values intentionally match Linux's <dirent.h>
 * ---------------------------------------------------------------------- */
#define DT_UNKNOWN   0   /**< type could not be determined          */
#define DT_FIFO      1   /**< named pipe (FIFO)                     */
#define DT_CHR       2   /**< character device                      */
#define DT_DIR       4   /**< directory                             */
#define DT_BLK       6   /**< block device                          */
#define DT_REG       8   /**< regular file                          */
#define DT_LNK      10   /**< symbolic link / reparse point         */
#define DT_SOCK     12   /**< UNIX-domain socket                    */
#define DT_WHT      14   /**< BSD whiteout (unused on Windows)      */

/** Maximum filename length including the NUL terminator. */
#ifndef NAME_MAX
#   define NAME_MAX 260   /* MAX_PATH covers most real-world cases */
#endif

/* -------------------------------------------------------------------------
 * struct dirent
 *
 * Mirrors the most-portable subset of the POSIX struct dirent:
 *   d_ino   inode number       (always 0 on Windows)
 *   d_type  entry type         (DT_REG, DT_DIR, DT_LNK, DT_UNKNOWN)
 *   d_name  null-terminated filename (no directory component)
 * ---------------------------------------------------------------------- */
struct dirent {
    unsigned long  d_ino;            /**< inode number — always 0 on Windows */
    unsigned char  d_type;           /**< entry type (DT_* constant)          */
    char           d_name[NAME_MAX]; /**< null-terminated filename            */
};

/* -------------------------------------------------------------------------
 * DIR — opaque stream handle returned by opendir()
 *
 * Because FindFirstFile returns the *first* entry at the same time it opens
 * the search handle, we buffer that entry and use `at_start` to serve it
 * back on the first readdir() call without advancing the handle.
 * ---------------------------------------------------------------------- */
typedef struct DIR_s {
    HANDLE           handle;                  /**< Win32 search handle             */
    WIN32_FIND_DATAA find_data;               /**< buffered result from Find*File   */
    struct dirent    entry;                   /**< dirent populated by readdir()    */
    int              has_next;                /**< 0 once the listing is exhausted  */
    int              at_start;                /**< 1 before the first readdir()     */
    char             search_path[MAX_PATH];   /**< saved for rewinddir()            */
} DIR;

/* -------------------------------------------------------------------------
 * Internal helpers (not part of the public API)
 * ---------------------------------------------------------------------- */

/** Translate Win32 file-attribute flags to a DT_* constant. */
static __inline unsigned char dirent__type_from_attr(DWORD attr)
{
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return DT_LNK;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)     return DT_DIR;
    return DT_REG;
}

/** Map the current Win32 last-error to the nearest errno value. */
static __inline void dirent__map_error(void)
{
    switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:    errno = ENOENT;  break;
        case ERROR_ACCESS_DENIED:     errno = EACCES;  break;
        case ERROR_NOT_ENOUGH_MEMORY: errno = ENOMEM;  break;
        case ERROR_INVALID_HANDLE:    errno = EBADF;   break;
        default:                      errno = EINVAL;  break;
    }
}

/* -------------------------------------------------------------------------
 * opendir(path)
 *
 * Opens the directory named by `path` and returns a DIR stream positioned
 * before the first entry.
 *
 * Returns: pointer to a DIR on success, NULL on failure (errno is set).
 * ---------------------------------------------------------------------- */
static __inline DIR *opendir(const char *path)
{
    DIR   *dir;
    char   glob[MAX_PATH];
    size_t len;

    if (!path) { errno = EINVAL; return NULL; }

    len = strlen(path);
    if (len == 0)            { errno = ENOENT;       return NULL; }
    if (len + 3 >= MAX_PATH) { errno = ENAMETOOLONG; return NULL; }

    /* Win32 requires a "dir\*" glob — append a separator if needed. */
    memcpy(glob, path, len);
    if (glob[len - 1] != '\\' && glob[len - 1] != '/')
        glob[len++] = '\\';
    glob[len++] = '*';
    glob[len]   = '\0';

    dir = (DIR *)malloc(sizeof(DIR));
    if (!dir) { errno = ENOMEM; return NULL; }

    /*
     * FindFirstFile opens the handle AND fetches the first entry.
     * We stash the result in find_data and raise at_start so that the
     * first readdir() returns it without calling FindNextFile.
     */
    dir->handle = FindFirstFileA(glob, &dir->find_data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        dirent__map_error();
        free(dir);
        return NULL;
    }

    dir->has_next = 1;
    dir->at_start = 1;
    strncpy(dir->search_path, glob, MAX_PATH - 1);
    dir->search_path[MAX_PATH - 1] = '\0';

    return dir;
}

/* -------------------------------------------------------------------------
 * readdir(dir)
 *
 * Returns a pointer to the next directory entry, or NULL when the stream
 * is exhausted (errno unchanged) or an error occurs (errno is set).
 *
 * The returned pointer is valid only until the next call to readdir() or
 * closedir() on the same DIR stream (standard POSIX behaviour).
 * ---------------------------------------------------------------------- */
static __inline struct dirent *readdir(DIR *dir)
{
    if (!dir || dir->handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return NULL;
    }

    if (!dir->has_next) return NULL; /* stream exhausted — not an error */

    if (dir->at_start) {
        /*
         * First call: consume the entry already fetched by FindFirstFile.
         * Don't advance the handle yet.
         */
        dir->at_start = 0;
    } else {
        /* Subsequent calls: advance the Win32 cursor. */
        if (!FindNextFileA(dir->handle, &dir->find_data)) {
            dir->has_next = 0;
            if (GetLastError() != ERROR_NO_MORE_FILES)
                dirent__map_error();
            return NULL;
        }
    }

    /* Populate the public dirent from the buffered Win32 data. */
    dir->entry.d_ino  = 0;
    dir->entry.d_type = dirent__type_from_attr(dir->find_data.dwFileAttributes);
    strncpy(dir->entry.d_name, dir->find_data.cFileName, NAME_MAX - 1);
    dir->entry.d_name[NAME_MAX - 1] = '\0';

    return &dir->entry;
}

/* -------------------------------------------------------------------------
 * closedir(dir)
 *
 * Closes the directory stream and frees all associated resources.
 *
 * Returns: 0 on success, -1 on error (errno is set).
 * ---------------------------------------------------------------------- */
static __inline int closedir(DIR *dir)
{
    if (!dir) { errno = EBADF; return -1; }

    if (dir->handle != INVALID_HANDLE_VALUE) {
        if (!FindClose(dir->handle)) {
            dirent__map_error();
            free(dir);
            return -1;
        }
    }

    free(dir);
    return 0;
}

/* -------------------------------------------------------------------------
 * rewinddir(dir)
 *
 * Resets the stream to the beginning so the next readdir() returns the
 * first entry again.
 *
 * Implementation note: Win32 has no native rewind for a search handle, so
 * we close the current handle and reopen it using the glob path saved at
 * opendir() time. Entries added or removed between the original opendir()
 * and this call may be reflected in the subsequent scan.
 * ---------------------------------------------------------------------- */
static __inline void rewinddir(DIR *dir)
{
    if (!dir) return;

    if (dir->handle != INVALID_HANDLE_VALUE)
        FindClose(dir->handle);

    dir->handle = FindFirstFileA(dir->search_path, &dir->find_data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        dirent__map_error();
        dir->has_next = 0;
    } else {
        dir->has_next = 1;
        dir->at_start = 1;
    }
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _WIN32 */
#endif /* DIRENT_COMPAT_H */
