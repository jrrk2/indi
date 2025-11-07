#ifndef INDI_WIN_COMPAT_H
#define INDI_WIN_COMPAT_H

#ifdef _WIN32

#include <windows.h>
#include <winsock2.h>  /* Must come before windows.h in actual usage */
#include <ws2tcpip.h>
#include <direct.h>
#include <dirent.h>
#include <io.h>
#include <process.h>
#include <time.h>
#include <shlobj.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

/* Directory & file functions */
#define mkdir(path, mode) _mkdir(path)
#define getuid() 0
#define getgid() 0

typedef int uid_t;
typedef int gid_t;
// typedef int pid_t;

struct passwd {
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

static struct passwd *getpwuid(uid_t uid) {
    static struct passwd pw = { "", "", 0, 0, "", "", "" };
    return &pw;
}

/* Network-related definitions */
#ifndef SHUT_WR
#define SHUT_WR SD_SEND
#endif

/* Signal-related definitions for Windows */
#define SIGHUP  1
#define SIGQUIT 3
#define SIGKILL 9
#define SIGALRM 14
#define SIGPIPE 13

typedef void (*sighandler_t)(int);

static sighandler_t signal_handlers[32] = {NULL};

static void win_signal_handler(int sig) {
    if (signal_handlers[sig])
        signal_handlers[sig](sig);
}

static sighandler_t signal(int sig, sighandler_t handler) {
    sighandler_t old = signal_handlers[sig];
    signal_handlers[sig] = handler;
    return old;
}

static unsigned int alarm(unsigned int seconds) {
    /* Very basic implementation - doesn't handle many real alarm features */
    Sleep(seconds * 1000);
    if (signal_handlers[SIGALRM])
        signal_handlers[SIGALRM](SIGALRM);
    return 0;
}

/* Terminal I/O constants and functions */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

static int tcflush(int fd, int queue_selector) {
    HANDLE handle = (HANDLE)_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE)
        return -1;

    if (!PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR))
        return -1;

    return 0;
}

/* Time and date functions */
#ifndef timersub
#define timersub(a, b, result)                          \
    do {                                                \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;   \
        (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
        if ((result)->tv_usec < 0) {                    \
            --(result)->tv_sec;                         \
            (result)->tv_usec += 1000000;               \
        }                                               \
    } while (0)
#endif

/* strptime implementation for Windows (C-compatible) */
static char* strptime(const char* s, const char* format, struct tm* tm) {
    char* result = NULL;
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;

    /* Handle common formats */
    if (strcmp(format, "%FT%T") == 0) {
        /* ISO 8601 format: YYYY-MM-DDThh:mm:ss */
        if (sscanf(s, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) == 6) {
            tm->tm_year = year - 1900;
            tm->tm_mon = month - 1;
            tm->tm_mday = day;
            tm->tm_hour = hour;
            tm->tm_min = min;
            tm->tm_sec = sec;
            tm->tm_isdst = -1;

            /* Find position after the parsed string */
            char format_buf[64];
            sprintf(format_buf, "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour, min, sec);
            result = (char*)s + strlen(format_buf);
        }
    }

    return result;
}

/* localtime_r replacement */
static struct tm* localtime_r(const time_t* timep, struct tm* result) {
    errno_t err = localtime_s(result, timep);
    return (err == 0) ? result : NULL;
}

/* Implement ln_date functions if needed */
#ifndef _LN_TYPES_H
struct ln_date {
    int years;
    int months;
    int days;
    int hours;
    int minutes;
    double seconds;
};
#endif

/* Function to parse ISO time format */
static int extractISOTime(const char* timestr, struct ln_date* iso_date) {
    /* ISO format: YYYY-MM-DDTHH:MM:SS.sss */
    int year, month, day, hour = 0, minute = 0;
    double second = 0;
    char c1, c2;

    /* Try full format with T separator */
    if (sscanf(timestr, "%d%c%d%c%dT%d:%d:%lf",
               &year, &c1, &month, &c2, &day, &hour, &minute, &second) >= 5) {
        iso_date->years = year;
        iso_date->months = month;
        iso_date->days = day;
        iso_date->hours = hour;
        iso_date->minutes = minute;
        iso_date->seconds = second;
        return 0;
    }

    /* Try alternative format with space separator */
    if (sscanf(timestr, "%d%c%d%c%d %d:%d:%lf",
               &year, &c1, &month, &c2, &day, &hour, &minute, &second) >= 5) {
        iso_date->years = year;
        iso_date->months = month;
        iso_date->days = day;
        iso_date->hours = hour;
        iso_date->minutes = minute;
        iso_date->seconds = second;
        return 0;
    }

    /* Try date-only format */
    if (sscanf(timestr, "%d%c%d%c%d", &year, &c1, &month, &c2, &day) == 5) {
        iso_date->years = year;
        iso_date->months = month;
        iso_date->days = day;
        iso_date->hours = 0;
        iso_date->minutes = 0;
        iso_date->seconds = 0;
        return 0;
    }

    return -1;  /* Failed to parse */
}

static int alphasort(const struct dirent **a, const struct dirent **b) {
    return _stricmp((*a)->d_name, (*b)->d_name);
}

typedef int (*filter_fn)(const struct dirent *);

static int scandir(const char *dirp, struct dirent ***namelist,
            filter_fn filter, int (*compar)(const struct dirent **, const struct dirent **)) {
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    char searchPath[MAX_PATH];
    int count = 0;
    int allocated = 16;
    struct dirent **results = (struct dirent **)malloc(allocated * sizeof(struct dirent *));

    if (!results)
        return -1;

    /* Create search path */
    _snprintf(searchPath, MAX_PATH, "%s\\*", dirp);

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
            int i;
            for (i = 0; i < count; i++)
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
                int i;
                for (i = 0; i < count; i++)
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

/* Simulate POSIX home directory */
static const char* indi_get_home_dir(void) {
    static char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path)))
        return path;
    return "C:\\Users\\Public";
}

/* Enhanced wordexp/wordfree implementation */
typedef struct {
    size_t we_wordc;
    char **we_wordv;
    size_t we_offs;
} wordexp_t;

static int wordexp(const char* words, wordexp_t* pwordexp, int flags) {
    /* Initialize the structure */
    pwordexp->we_wordc = 1;
    pwordexp->we_wordv = (char **)malloc(sizeof(char *) * 2);
    if (!pwordexp->we_wordv)
        return -1;

    pwordexp->we_wordv[1] = NULL;  /* NULL terminate the array */

    /* Expand ~ to user home directory */
    if (words[0] == '~' && (words[1] == '/' || words[1] == '\\' || words[1] == '\0')) {
        const char *homeDir = indi_get_home_dir();

        size_t homeLen = strlen(homeDir);
        size_t wordLen = strlen(words + 1);  /* Skip the ~ character */

        pwordexp->we_wordv[0] = (char *)malloc(homeLen + wordLen + 1);
        if (!pwordexp->we_wordv[0]) {
            free(pwordexp->we_wordv);
            return -1;
        }

        strcpy(pwordexp->we_wordv[0], homeDir);
        strcat(pwordexp->we_wordv[0], words + 1);
    } else {
        /* Just copy the string */
        pwordexp->we_wordv[0] = _strdup(words);
        if (!pwordexp->we_wordv[0]) {
            free(pwordexp->we_wordv);
            return -1;
        }
    }

    /* Convert forward slashes to backslashes */
    {
        char *p = pwordexp->we_wordv[0];
        while (*p) {
            if (*p == '/')
                *p = '\\';
            p++;
        }
    }

    return 0;
}

static void wordfree(wordexp_t* pwordexp) {
    if (pwordexp && pwordexp->we_wordv) {
        size_t i;
        for (i = 0; i < pwordexp->we_wordc && pwordexp->we_wordv[i]; i++) {
            free(pwordexp->we_wordv[i]);
        }
        free(pwordexp->we_wordv);
        pwordexp->we_wordv = NULL;
        pwordexp->we_wordc = 0;
    }
}

/* Create directory with parents (like mkdir -p) */
static int mkpath(const char *path, int mode) {
    char tmp[MAX_PATH];
    char *p = NULL;
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

/* Define Windows equivalent of some basic UNIX functions */
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

/* Add socket initialization for Windows */
static int win_socket_init(void) {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
}

/* And cleanup at application exit */
static void win_socket_cleanup(void) {
    WSACleanup();
}

/* Replacement for herror */
static void herror(const char *s) {
    fprintf(stderr, "%s: %d\n", s, WSAGetLastError());
}

#endif /* _WIN32 */
#endif /* INDI_WIN_COMPAT_H */

