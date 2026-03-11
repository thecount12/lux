/*
 * luxtest_plan9 - test runner for Lux (.lux) files  [Plan 9]
 *
 * Usage: luxtest [-lux path] [-t timeout] [-v] [dir|file]
 *   -lux <path>   path to lux interpreter (default: ./lux)
 *   -t <secs>     per-test timeout in seconds (default: 10, 0 = none)
 *   -v            verbose: show output even for passing tests
 *   dir|file      directory of .lux files or single file (default: ./tests)
 *
 * PASS if lux exits with exits(nil).
 * FAIL if lux exits with exits("...").
 * TIMEOUT if the child is killed by alarm() -> exits("alarm").
 */

#include <u.h>
#include <libc.h>

#define DEFAULT_TIMEOUT 10
#define MAX_TESTS       512
#define ERR_BUF_SIZE    2048
#define PATH_BUF        512

static char *progname = "luxtest";

static void
usage(void)
{
	fprint(2,
		"usage: %s [-lux path] [-t timeout] [-v] [dir|file]\n"
		"  -lux <path>  path to lux interpreter (default: ./lux)\n"
		"  -t <secs>    per-test timeout in seconds (default: %d, 0 = none)\n"
		"  -v           verbose: show stderr even for passing tests\n"
		"  dir|file     test directory or single .lux file (default: ./tests)\n",
		progname, DEFAULT_TIMEOUT);
	exits("usage");
}

typedef struct TestFile TestFile;
struct TestFile {
	char path[PATH_BUF];
};

static int
endsWith(char *s, char *suffix)
{
	int slen   = strlen(s);
	int sfxlen = strlen(suffix);
	if (slen < sfxlen) return 0;
	return strcmp(s + slen - sfxlen, suffix) == 0;
}

static int
cmpTestFile(void *a, void *b)
{
	return strcmp(((TestFile*)a)->path, ((TestFile*)b)->path);
}

/* Collect *.lux files from dir into tests[], sorted by name. */
static int
collectTests(char *dir, TestFile *tests, int maxTests)
{
	int fd;
	Dir *d;
	long n, i;
	int count = 0;

	fd = open(dir, OREAD);
	if (fd < 0) {
		fprint(2, "%s: cannot open directory '%s'\n", progname, dir);
		return -1;
	}
	n = dirreadall(fd, &d);
	close(fd);
	if (n < 0) {
		fprint(2, "%s: dirreadall('%s') failed\n", progname, dir);
		return -1;
	}
	for (i = 0; i < n && count < maxTests; i++) {
		if (!endsWith(d[i].name, ".lux")) continue;
		snprint(tests[count].path, PATH_BUF, "%s/%s", dir, d[i].name);
		count++;
	}
	free(d);
	qsort(tests, count, sizeof(TestFile), cmpTestFile);
	return count;
}

/*
 * Fork and exec luxBin on file.  Child's stderr is captured into errBuf.
 * Stdout is discarded.  Returns:
 *   0  = pass (exits(nil))
 *   1  = fail (exits("...") non-empty)
 *  -1  = timed out (exits("alarm")) or exec error
 */
static int
runTest(char *luxBin, char *file, int timeoutSecs,
        char *errBuf, int errBufSize)
{
	int pipeFd[2];
	int pid;
	int n, total;
	Waitmsg *w;

	if (pipe(pipeFd) < 0) {
		fprint(2, "%s: pipe() failed\n", progname);
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		fprint(2, "%s: fork() failed\n", progname);
		close(pipeFd[0]);
		close(pipeFd[1]);
		return -1;
	}

	if (pid == 0) {
		char *args[3];

		/* Child: redirect stderr to pipe write end, discard stdout */
		close(pipeFd[0]);
		dup(pipeFd[1], 2);
		close(pipeFd[1]);
		dup(open("/dev/null", OWRITE), 1);

		if (timeoutSecs > 0)
			alarm(timeoutSecs * 1000);

		args[0] = luxBin;
		args[1] = file;
		args[2] = nil;
		exec(luxBin, args);
		exits("exec failed");
	}

	/* Parent: drain stderr pipe, then wait for child */
	close(pipeFd[1]);
	total = 0;
	while (total < errBufSize - 1) {
		n = read(pipeFd[0], errBuf + total, errBufSize - 1 - total);
		if (n <= 0) break;
		total += n;
	}
	errBuf[total] = '\0';
	close(pipeFd[0]);

	/* Wait for our child specifically */
	for (;;) {
		w = wait();
		if (w == nil) break;
		if (w->pid == pid) {
			int rc;
			if (w->msg[0] == '\0')
				rc = 0;  /* pass */
			else if (strcmp(w->msg, "alarm") == 0)
				rc = -1; /* timeout */
			else
				rc = 1;  /* fail */
			free(w);
			return rc;
		}
		free(w);
	}
	return -1;
}

static void
printIndented(char *buf)
{
	char *p = buf;
	char *nl;
	int len;

	while (*p) {
		nl = strchr(p, '\n');
		len = nl ? (int)(nl - p) : strlen(p);
		if (len > 0)
			print("      %.*s\n", len, p);
		if (nl == nil) break;
		p = nl + 1;
	}
}

void
main(int argc, char *argv[])
{
	char *luxBin   = "./lux";
	int timeoutSecs = DEFAULT_TIMEOUT;
	int verbose    = 0;
	char *testPath = "./tests";
	TestFile tests[MAX_TESTS];
	int count, i, passed, failed, timedout;
	char errBuf[ERR_BUF_SIZE];
	Dir *d;

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

	/* Is testPath a file or directory? */
	d = dirstat(testPath);
	if (d == nil) {
		fprint(2, "%s: cannot stat '%s'\n", progname, testPath);
		exits("stat");
	}

	count = 0;
	if (d->mode & DMDIR) {
		free(d);
		count = collectTests(testPath, tests, MAX_TESTS);
		if (count < 0) exits("error");
		if (count == 0) {
			fprint(2, "%s: no .lux files found in '%s'\n", progname, testPath);
			exits("no tests");
		}
	} else {
		free(d);
		if (!endsWith(testPath, ".lux")) {
			fprint(2, "%s: '%s' is not a .lux file\n", progname, testPath);
			exits("error");
		}
		strncpy(tests[0].path, testPath, PATH_BUF - 1);
		tests[0].path[PATH_BUF - 1] = '\0';
		count = 1;
	}

	if (timeoutSecs > 0)
		print("luxtest: running %d test%s  (interpreter: %s, timeout: %ds)\n\n",
		      count, count == 1 ? "" : "s", luxBin, timeoutSecs);
	else
		print("luxtest: running %d test%s  (interpreter: %s, no timeout)\n\n",
		      count, count == 1 ? "" : "s", luxBin);

	passed = failed = timedout = 0;

	for (i = 0; i < count; i++) {
		int rc = runTest(luxBin, tests[i].path, timeoutSecs,
		                 errBuf, ERR_BUF_SIZE);
		if (rc == 0) {
			passed++;
			print("PASS  %s\n", tests[i].path);
			if (verbose && errBuf[0])
				printIndented(errBuf);
		} else if (rc == -1) {
			timedout++;
			print("TIMEOUT  %s  (>%ds)\n", tests[i].path, timeoutSecs);
		} else {
			failed++;
			print("FAIL  %s\n", tests[i].path);
			if (errBuf[0])
				printIndented(errBuf);
		}
	}

	print("\n%d/%d passed", passed, passed + failed + timedout);
	if (failed)   print(", %d failed", failed);
	if (timedout) print(", %d timed out", timedout);
	print("\n");

	if (failed > 0 || timedout > 0)
		exits("tests failed");
	exits(nil);
}
