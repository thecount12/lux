/*
 * luxtest - test runner for Lux (.lux) files
 *
 * Usage: luxtest [-lux path] [-t timeout] [-v] [dir|file]
 *   -lux <path>   path to lux interpreter (default: ./lux)
 *   -t <secs>     per-test timeout in seconds (default: 10)
 *   -v            verbose: show stderr output even for passing tests
 *   dir|file      directory of .lux files or single .lux file (default: ./tests)
 *
 * A test PASSES if the interpreter exits with code 0.
 * A test FAILS  if the interpreter exits with a non-zero code.
 * A test TIMES OUT if it exceeds the timeout (killed by SIGALRM in child).
 * Directory scans skip *_child.lux helpers (spawned by another test).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEFAULT_TIMEOUT 10
#define MAX_TESTS       512
#define ERR_BUF_SIZE    2048
#define PATH_BUF        512

static const char* progname = "luxtest";

static void usage(void) {
    fprintf(stderr,
        "usage: %s [-lux path] [-t timeout] [-v] [dir|file]\n"
        "  -lux <path>  path to lux interpreter (default: ./lux)\n"
        "  -t <secs>    per-test timeout in seconds (default: %d, 0 = none)\n"
        "  -v           verbose: show stderr even for passing tests\n"
        "  dir|file     test directory or single .lux file (default: ./tests)\n",
        progname, DEFAULT_TIMEOUT);
    exit(1);
}

typedef struct {
    char path[PATH_BUF];
} TestFile;

static int endsWith(const char* s, const char* suffix) {
    int slen   = (int)strlen(s);
    int sfxlen = (int)strlen(suffix);
    if (slen < sfxlen) return 0;
    return strcmp(s + slen - sfxlen, suffix) == 0;
}

/* Collect *.lux files from dir into tests[], sorted by name. */
static int collectTests(const char* dir, TestFile* tests, int maxTests) {
    DIR* d = opendir(dir);
    if (!d) {
        fprintf(stderr, "%s: cannot open directory '%s'\n", progname, dir);
        return -1;
    }
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL && count < maxTests) {
        if (!endsWith(entry->d_name, ".lux")) continue;
        if (endsWith(entry->d_name, "_child.lux")) continue;
        snprintf(tests[count].path, PATH_BUF, "%s/%s", dir, entry->d_name);
        count++;
    }
    closedir(d);

    /* Bubble sort by path for stable, consistent ordering */
    int i, j;
    for (i = 0; i < count - 1; i++) {
        for (j = i + 1; j < count; j++) {
            if (strcmp(tests[i].path, tests[j].path) > 0) {
                TestFile tmp = tests[i];
                tests[i] = tests[j];
                tests[j] = tmp;
            }
        }
    }
    return count;
}

/*
 * Fork and exec luxBin on file.  Stderr is captured into errBuf.
 * Stdout is discarded.  Returns exit code, or -1 if killed by signal
 * (including SIGALRM timeout).
 */
static int runTest(const char* luxBin, const char* file, int timeoutSecs,
                   char* errBuf, int errBufSize) {
    int pipeFd[2];
    if (pipe(pipeFd) != 0) {
        fprintf(stderr, "%s: pipe() failed\n", progname);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "%s: fork() failed\n", progname);
        close(pipeFd[0]);
        close(pipeFd[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child: wire stderr → pipe, suppress stdout */
        close(pipeFd[0]);
        dup2(pipeFd[1], STDERR_FILENO);
        close(pipeFd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }

        if (timeoutSecs > 0)
            alarm((unsigned int)timeoutSecs);

        execl(luxBin, luxBin, file, (char*)NULL);
        _exit(127); /* execl failed */
    }

    /* Parent: drain pipe, then wait */
    close(pipeFd[1]);
    int total = 0;
    ssize_t n;
    while (total < errBufSize - 1 &&
           (n = read(pipeFd[0], errBuf + total, (size_t)(errBufSize - 1 - total))) > 0) {
        total += (int)n;
    }
    errBuf[total] = '\0';
    close(pipeFd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -1; /* killed — treat as timeout/crash */
    return -1;
}

/* Print errBuf lines indented under a test result line. */
static void printIndented(const char* buf) {
    const char* p = buf;
    while (*p) {
        const char* nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > 0)
            printf("      %.*s\n", len, p);
        if (!nl) break;
        p = nl + 1;
    }
}

int main(int argc, char* argv[]) {
    const char* luxBin  = "./lux";
    int timeoutSecs     = DEFAULT_TIMEOUT;
    int verbose         = 0;
    const char* testPath = "./tests";
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-lux") == 0) {
            if (++i >= argc) usage();
            luxBin = argv[i];
        } else if (strcmp(argv[i], "-t") == 0) {
            if (++i >= argc) usage();
            timeoutSecs = atoi(argv[i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (argv[i][0] != '-') {
            testPath = argv[i];
        } else {
            usage();
        }
    }

    /* Resolve test path: file or directory? */
    struct stat st;
    if (stat(testPath, &st) != 0) {
        fprintf(stderr, "%s: cannot stat '%s'\n", progname, testPath);
        return 1;
    }

    TestFile tests[MAX_TESTS];
    int count = 0;

    if (S_ISREG(st.st_mode)) {
        if (!endsWith(testPath, ".lux")) {
            fprintf(stderr, "%s: '%s' is not a .lux file\n", progname, testPath);
            return 1;
        }
        strncpy(tests[0].path, testPath, PATH_BUF - 1);
        tests[0].path[PATH_BUF - 1] = '\0';
        count = 1;
    } else if (S_ISDIR(st.st_mode)) {
        count = collectTests(testPath, tests, MAX_TESTS);
        if (count < 0) return 1;
        if (count == 0) {
            fprintf(stderr, "%s: no .lux files found in '%s'\n", progname, testPath);
            return 1;
        }
    } else {
        fprintf(stderr, "%s: '%s' is neither a file nor a directory\n", progname, testPath);
        return 1;
    }

    if (timeoutSecs > 0)
        printf("luxtest: running %d test%s  (interpreter: %s, timeout: %ds)\n\n",
               count, count == 1 ? "" : "s", luxBin, timeoutSecs);
    else
        printf("luxtest: running %d test%s  (interpreter: %s, no timeout)\n\n",
               count, count == 1 ? "" : "s", luxBin);

    int passed = 0, failed = 0, timedout = 0;
    char errBuf[ERR_BUF_SIZE];

    for (i = 0; i < count; i++) {
        int code = runTest(luxBin, tests[i].path, timeoutSecs,
                           errBuf, ERR_BUF_SIZE);
        if (code == 0) {
            passed++;
            printf("PASS  %s\n", tests[i].path);
            if (verbose && errBuf[0])
                printIndented(errBuf);
        } else if (code == -1) {
            timedout++;
            printf("TIMEOUT  %s  (>%ds)\n", tests[i].path, timeoutSecs);
        } else {
            failed++;
            printf("FAIL  %s\n", tests[i].path);
            if (errBuf[0])
                printIndented(errBuf);
        }
    }

    int total = passed + failed + timedout;
    printf("\n%d/%d passed", passed, total);
    if (failed)   printf(", %d failed", failed);
    if (timedout) printf(", %d timed out", timedout);
    printf("\n");

    return (failed > 0 || timedout > 0) ? 1 : 0;
}
