#ifndef INDI_WIN_COMPAT_H
#define INDI_WIN_COMPAT_H

#ifdef _WIN32

#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#include <time.h>
#include <shlobj.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <algorithm>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "ws2_32.lib")

// Directory functions
#define mkdir(path, mode) _mkdir(path)
#define getuid() 0
#define getgid() 0

// User/group types
typedef int uid_t;
typedef int gid_t;

struct passwd {
        char *pw_name;
        char *pw_passwd;
        uid_t pw_uid;
        gid_t pw_gid;
        char *pw_gecos;
        char *pw_dir;
        char *pw_shell;
};

struct passwd *getpwuid(uid_t uid) {
    static struct passwd pw = { "", "", 0, 0, "", "", "" };
    return &pw;
}

// Terminal I/O constants and functions
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

static inline int tcflush(int fd, int queue_selector) {
    HANDLE handle = (HANDLE)_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE)
        return -1;

    if (!PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR))
        return -1;

    return 0;
}

// File scanning functions
struct dirent {
    char d_name[MAX_PATH];
};

static inline int alphasort(const struct dirent **a, const struct dirent **b) {
    return _stricmp((*a)->d_name, (*b)->d_name);
}

typedef int (*filter_fn)(const struct dirent *);

static int scandir(const char *dirp, struct dirent ***namelist,
            filter_fn filter, int (*compar)(const struct dirent **, const struct dirent **)) {
    WIN32_FIND_DATA ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    char searchPath[MAX_PATH];
    int count = 0;
    int allocated = 16;
    struct dirent **results = (struct dirent **)malloc(allocated * sizeof(struct dirent *));

    if (!results)
        return -1;

    // Create search path
    snprintf(searchPath, MAX_PATH, "%s\\*", dirp);

    hFind = FindFirstFileA(searchPath, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        free(results);
        return -1;
    }

    do {
        struct dirent d, *dup;
        strncpy(d.d_name, ffd.cFileName, MAX_PATH);

        if (filter && !filter(&d))
            continue;

        dup = (struct dirent *)malloc(sizeof(struct dirent));
        if (!dup) {
            for (int i = 0; i < count; i++)
                free(results[i]);
            free(results);
            FindClose(hFind);
            return -1;
        }

        memcpy(dup, &d, sizeof(struct dirent));

        if (count >= allocated) {
            allocated *= 2;
            struct dirent **newResults = (struct dirent **)realloc(results, allocated * sizeof(struct dirent *));
            if (!newResults) {
                for (int i = 0; i < count; i++)
                    free(results[i]);
                free(results);
                free(dup);
                FindClose(hFind);
                return -1;
            }
            results = newResults;
        }

        results[count++] = dup;
    } while (FindNextFileA(hFind, &ffd) != 0);

    FindClose(hFind);

    if (compar)
        qsort(results, count, sizeof(struct dirent *),
              (int (*)(const void *, const void *))compar);

    *namelist = results;
    return count;
}

// Time functions
#ifndef timersub
#define timersub(a, b, result) \
    do { \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
        (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
        if ((result)->tv_usec < 0) { \
            --(result)->tv_sec; \
            (result)->tv_usec += 1000000; \
        } \
    } while (0)
#endif

static inline char* strptime(const char* s, const char* format, struct tm* tm) {
    std::istringstream input(s);
    input.imbue(std::locale(setlocale(LC_ALL, nullptr)));
    input >> std::get_time(tm, format);
    if (input.fail()) {
        return nullptr;
    }
    return (char*)(s + input.tellg());
}

static inline struct tm* localtime_r(const time_t* timep, struct tm* result) {
    errno_t err = localtime_s(result, timep);
    return (err == 0) ? result : nullptr;
}

// Simulate POSIX home directory
static inline const char* indi_get_home_dir() {
    static char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path)))
        return path;
    return "C:\\Users\\Public";
}

// wordexp/wordfree improved implementation
typedef struct {
    size_t we_wordc;
    char **we_wordv;
    size_t we_offs;
} wordexp_t;

static inline int wordexp(const char* words, wordexp_t* pwordexp, int flags) {
    // Initialize the structure
    pwordexp->we_wordc = 1;
    pwordexp->we_wordv = (char **)malloc(sizeof(char *) * 2);
    if (!pwordexp->we_wordv)
        return -1;

    pwordexp->we_wordv[1] = nullptr;  // NULL terminate the array

    // Expand ~ to user home directory
    if (words[0] == '~' && (words[1] == '/' || words[1] == '\\' || words[1] == '\0')) {
        const char *homeDir = indi_get_home_dir();

        size_t homeLen = strlen(homeDir);
        size_t wordLen = strlen(words + 1);  // Skip the ~ character

        pwordexp->we_wordv[0] = (char *)malloc(homeLen + wordLen + 1);
        if (!pwordexp->we_wordv[0]) {
            free(pwordexp->we_wordv);
            return -1;
        }

        strcpy(pwordexp->we_wordv[0], homeDir);
        strcat(pwordexp->we_wordv[0], words + 1);
    } else {
        // Just copy the string
        pwordexp->we_wordv[0] = _strdup(words);
        if (!pwordexp->we_wordv[0]) {
            free(pwordexp->we_wordv);
            return -1;
        }
    }

    // Convert forward slashes to backslashes
    char *p = pwordexp->we_wordv[0];
    while (*p) {
        if (*p == '/')
            *p = '\\';
        p++;
    }

    return 0;
}

static inline void wordfree(wordexp_t* pwordexp) {
    if (pwordexp && pwordexp->we_wordv) {
        for (size_t i = 0; i < pwordexp->we_wordc && pwordexp->we_wordv[i]; i++) {
            free(pwordexp->we_wordv[i]);
        }
        free(pwordexp->we_wordv);
        pwordexp->we_wordv = nullptr;
        pwordexp->we_wordc = 0;
    }
}

// Create directory with parents (like mkdir -p)
static inline int mkpath(const char *path, mode_t mode) {
    char tmp[MAX_PATH];
    char *p = nullptr;
    size_t len;

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    len = strlen(tmp);

    if (tmp[len - 1] == '\\')
        tmp[len - 1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '\\') {
            *p = '\0';
            _mkdir(tmp);
            *p = '\\';
        }
    }

    return _mkdir(tmp);
}

// Define Windows equivalent of some basic UNIX functions
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

#endif // _WIN32
#endif // INDI_WIN_COMPAT_H

