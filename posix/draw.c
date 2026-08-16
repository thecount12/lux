#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>

#include "luxdraw.h"

#ifdef __APPLE__
#define SNARF_GET_CMD "pbpaste"
#define SNARF_PUT_CMD "pbcopy"
#else
#define SNARF_GET_CMD "xclip -selection clipboard -o"
#define SNARF_PUT_CMD "xclip -selection clipboard -i"
#endif

#ifdef LUX_P9P
static int helper_fd = -1;
static pid_t helper_pid = -1;
static int last_w;
static int last_h;
static int last_fh;
static int last_fw;
#endif
static int window_open = 0;
static char helper_path[1024];

void
luxdraw_set_helper_path(const char* argv0)
{
	const char* slash;
	size_t n;

	if (argv0 == NULL || argv0[0] == '\0') {
		helper_path[0] = '\0';
		return;
	}
	slash = strrchr(argv0, '/');
	if (slash == NULL) {
		snprintf(helper_path, sizeof(helper_path), "luxp9");
		return;
	}
	n = (size_t)(slash - argv0 + 1);
	if (n + 6 >= sizeof(helper_path)) {
		helper_path[0] = '\0';
		return;
	}
	memcpy(helper_path, argv0, n);
	helper_path[n] = '\0';
	strcat(helper_path, "luxp9");
}

#ifdef LUX_P9P
static int
readn(int fd, void* buf, int n)
{
	char* p = (char*)buf;
	int got = 0;
	while (got < n) {
		int r = (int)read(fd, p + got, (size_t)(n - got));
		if (r <= 0)
			return -1;
		got += r;
	}
	return got;
}

static int
writen(int fd, const void* buf, int n)
{
	const char* p = (const char*)buf;
	int put = 0;
	while (put < n) {
		int w = (int)write(fd, p + put, (size_t)(n - put));
		if (w <= 0)
			return -1;
		put += w;
	}
	return put;
}

static int
wrframe(int fd, const char* p, int n)
{
	unsigned char hdr[4];
	if (n < 0 || n > 1024 * 1024)
		return -1;
	hdr[0] = (unsigned char)(n & 0xff);
	hdr[1] = (unsigned char)((n >> 8) & 0xff);
	hdr[2] = (unsigned char)((n >> 16) & 0xff);
	hdr[3] = (unsigned char)((n >> 24) & 0xff);
	if (writen(fd, hdr, 4) != 4)
		return -1;
	if (n > 0 && writen(fd, p, n) != n)
		return -1;
	return 0;
}

static int
rdframe(int fd, char** out, int* n)
{
	unsigned char hdr[4];
	int len;
	char* buf;

	if (readn(fd, hdr, 4) != 4)
		return -1;
	len = (int)hdr[0] | ((int)hdr[1] << 8) | ((int)hdr[2] << 16) | ((int)hdr[3] << 24);
	if (len < 0 || len > 1024 * 1024)
		return -1;
	buf = (char*)malloc((size_t)len + 1);
	if (buf == NULL)
		return -1;
	if (len > 0 && readn(fd, buf, len) != len) {
		free(buf);
		return -1;
	}
	buf[len] = '\0';
	*out = buf;
	*n = len;
	return 0;
}

static int
start_helper(void)
{
	int sp[2];
	pid_t pid;
	const char* envp;
	const char* path;

	if (helper_fd >= 0)
		return 0;

	envp = getenv("LUXP9");
	path = (envp != NULL && envp[0] != '\0') ? envp :
		(helper_path[0] != '\0') ? helper_path : "luxp9";

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0)
		return -1;

	pid = fork();
	if (pid < 0) {
		close(sp[0]);
		close(sp[1]);
		return -1;
	}
	if (pid == 0) {
		close(sp[0]);
		if (dup2(sp[1], 0) < 0 || dup2(sp[1], 1) < 0)
			_exit(127);
		if (sp[1] > 1)
			close(sp[1]);
		execl(path, "luxp9", (char*)NULL);
		if (path[0] != '/' && strchr(path, '/') == NULL)
			execlp("luxp9", "luxp9", (char*)NULL);
		_exit(127);
	}
	close(sp[1]);
	helper_fd = sp[0];
	helper_pid = pid;
	return 0;
}

static int
helper_transact(const char* req, int reqn, char** resp, int* respn)
{
	if (start_helper() < 0)
		return -1;
	if (wrframe(helper_fd, req, reqn) < 0)
		return -1;
	return rdframe(helper_fd, resp, respn);
}
#endif

static int
clipboard_get(char** out, int* len)
{
	FILE* fp;
	char* buf;
	size_t cap, n;
	int c;

	fp = popen(SNARF_GET_CMD, "r");
	if (fp == NULL)
		return -1;
	cap = 256;
	n = 0;
	buf = (char*)malloc(cap);
	if (buf == NULL) {
		pclose(fp);
		return -1;
	}
	while ((c = fgetc(fp)) != EOF) {
		if (n + 1 >= cap) {
			char* nb;
			cap *= 2;
			nb = (char*)realloc(buf, cap);
			if (nb == NULL) {
				free(buf);
				pclose(fp);
				return -1;
			}
			buf = nb;
		}
		buf[n++] = (char)c;
	}
	pclose(fp);
	buf[n] = '\0';
	*out = buf;
	*len = (int)n;
	return 0;
}

static int
clipboard_put(const char* s, int n)
{
	FILE* fp;
	size_t w;

	fp = popen(SNARF_PUT_CMD, "w");
	if (fp == NULL)
		return -1;
	w = fwrite(s, 1, (size_t)n, fp);
	if (pclose(fp) != 0 || w != (size_t)n)
		return -1;
	return 0;
}

int
luxdraw_available(void)
{
#ifdef LUX_P9P
	return 1;
#else
	return 0;
#endif
}

int
luxdraw_is_open(void)
{
	return window_open;
}

int
luxsnarf_get(char** out, int* len)
{
#ifdef LUX_P9P
	if (window_open) {
		char* resp;
		int n;
		if (helper_transact("SNARFGET", 8, &resp, &n) == 0) {
			if (n >= 5 && memcmp(resp, "DATA\n", 5) == 0) {
				int dlen = n - 5;
				char* d = (char*)malloc((size_t)dlen + 1);
				if (d == NULL) {
					free(resp);
					return -1;
				}
				memcpy(d, resp + 5, (size_t)dlen);
				d[dlen] = '\0';
				free(resp);
				*out = d;
				*len = dlen;
				return 0;
			}
			free(resp);
		}
	}
#endif
	return clipboard_get(out, len);
}

int
luxsnarf_put(const char* s, int n)
{
#ifdef LUX_P9P
	if (window_open) {
		char* req;
		char* resp;
		int rn, nresp;
		req = (char*)malloc((size_t)n + 16);
		if (req == NULL)
			return -1;
		memcpy(req, "SNARFPUT\n", 9);
		memcpy(req + 9, s, (size_t)n);
		if (helper_transact(req, n + 9, &resp, &nresp) == 0) {
			rn = (nresp >= 2 && memcmp(resp, "OK", 2) == 0) ? 0 : -1;
			free(resp);
			free(req);
			if (rn == 0)
				return 0;
		} else {
			free(req);
		}
	}
#endif
	return clipboard_put(s, n);
}

int
luxdraw_open(const char* title, int w, int h)
{
#ifdef LUX_P9P
	char buf[512];
	char* resp;
	int n, tn, aw, ah, fh, fw;

	if (window_open)
		return -1;
	if (title == NULL)
		title = "lux";
	tn = snprintf(buf, sizeof(buf), "OPEN %d %d %s", w, h, title);
	if (tn < 0 || tn >= (int)sizeof(buf))
		return -1;
	if (helper_transact(buf, tn, &resp, &n) < 0)
		return -1;
	if (n >= 2 && memcmp(resp, "OK", 2) == 0) {
		last_w = w;
		last_h = h;
		last_fh = 16;
		last_fw = 8;
		aw = ah = fh = fw = 0;
		if (sscanf(resp + 2, "%d %d %d %d", &aw, &ah, &fh, &fw) >= 3) {
			if (aw > 0)
				last_w = aw;
			if (ah > 0)
				last_h = ah;
			if (fh > 0)
				last_fh = fh;
			if (fw > 0)
				last_fw = fw;
		}
		window_open = 1;
		free(resp);
		return 0;
	}
	free(resp);
	return -1;
#else
	(void)title;
	(void)w;
	(void)h;
	return -1;
#endif
}

int
luxdraw_info(int* w, int* h, int* fh, int* fw)
{
#ifdef LUX_P9P
	if (!window_open)
		return -1;
	if (w != NULL)
		*w = last_w;
	if (h != NULL)
		*h = last_h;
	if (fh != NULL)
		*fh = last_fh;
	if (fw != NULL)
		*fw = last_fw;
	return 0;
#else
	(void)w;
	(void)h;
	(void)fh;
	(void)fw;
	return -1;
#endif
}

int
luxdraw_fill(int x, int y, int w, int h, int r, int g, int b)
{
#ifdef LUX_P9P
	char buf[128];
	char* resp;
	int n, tn;

	if (!window_open)
		return -1;
	tn = snprintf(buf, sizeof(buf), "FILL %d %d %d %d %d %d %d", x, y, w, h, r, g, b);
	if (helper_transact(buf, tn, &resp, &n) < 0)
		return -1;
	if (n >= 2 && memcmp(resp, "OK", 2) == 0) {
		free(resp);
		return 0;
	}
	free(resp);
	return -1;
#else
	(void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b;
	return -1;
#endif
}

int
luxdraw_string(int x, int y, const char* s, int r, int g, int b)
{
#ifdef LUX_P9P
	char hdr[64];
	char* req;
	char* resp;
	int n, hn, slen, nresp;

	if (!window_open || s == NULL)
		return -1;
	slen = (int)strlen(s);
	hn = snprintf(hdr, sizeof(hdr), "STRING %d %d %d %d %d\n", x, y, r, g, b);
	req = (char*)malloc((size_t)hn + (size_t)slen);
	if (req == NULL)
		return -1;
	memcpy(req, hdr, (size_t)hn);
	memcpy(req + hn, s, (size_t)slen);
	if (helper_transact(req, hn + slen, &resp, &nresp) < 0) {
		free(req);
		return -1;
	}
	free(req);
	n = (nresp >= 2 && memcmp(resp, "OK", 2) == 0) ? 0 : -1;
	free(resp);
	return n;
#else
	(void)x; (void)y; (void)s; (void)r; (void)g; (void)b;
	return -1;
#endif
}

int
luxdraw_flush(void)
{
#ifdef LUX_P9P
	char* resp;
	int n;

	if (!window_open)
		return -1;
	if (helper_transact("FLUSH", 5, &resp, &n) < 0)
		return -1;
	if (n >= 2 && memcmp(resp, "OK", 2) == 0) {
		free(resp);
		return 0;
	}
	free(resp);
	return -1;
#else
	return -1;
#endif
}

int
luxdraw_event(LuxDrawEvent* e)
{
#ifdef LUX_P9P
	char* resp;
	int n;

	if (!window_open || e == NULL)
		return -1;
	if (helper_transact("EVENT", 5, &resp, &n) < 0)
		return -1;
	e->kind = e->x = e->y = e->button = e->r = 0;
	if (n >= 4 && memcmp(resp, "QUIT", 4) == 0) {
		e->kind = 3;
		window_open = 0;
		free(resp);
		return 0;
	}
	if (n >= 6 && memcmp(resp, "MOUSE ", 6) == 0) {
		e->kind = 0;
		sscanf(resp + 6, "%d %d %d", &e->x, &e->y, &e->button);
		free(resp);
		return 0;
	}
	if (n >= 4 && memcmp(resp, "KBD ", 4) == 0) {
		e->kind = 1;
		sscanf(resp + 4, "%d", &e->r);
		free(resp);
		return 0;
	}
	if (n >= 7 && memcmp(resp, "RESIZE ", 7) == 0) {
		e->kind = 2;
		sscanf(resp + 7, "%d %d", &e->x, &e->y);
		if (e->x > 0)
			last_w = e->x;
		if (e->y > 0)
			last_h = e->y;
		free(resp);
		return 0;
	}
	free(resp);
	return -1;
#else
	(void)e;
	return -1;
#endif
}

int
luxdraw_close(void)
{
#ifdef LUX_P9P
	char* resp;
	int n;

	if (!window_open)
		return 0;
	if (helper_transact("CLOSE", 5, &resp, &n) == 0)
		free(resp);
	window_open = 0;
	return 0;
#else
	return 0;
#endif
}

int
luxplumb_send(const char* dst, const char* data, const char* wdir)
{
#ifdef LUX_P9P
	int dstn, wdirn, datan, total;
	char* req;
	char* resp;
	int nresp, ok;
	char* p;

	if (dst == NULL || data == NULL)
		return -1;
	if (wdir == NULL)
		wdir = ".";
	dstn = (int)strlen(dst);
	wdirn = (int)strlen(wdir);
	datan = (int)strlen(data);
	total = 64 + dstn + wdirn + datan;
	req = (char*)malloc((size_t)total);
	if (req == NULL)
		return -1;
	p = req;
	p += sprintf(p, "PLUMB %d %d %d\n", dstn, wdirn, datan);
	memcpy(p, dst, (size_t)dstn);
	p += dstn;
	memcpy(p, wdir, (size_t)wdirn);
	p += wdirn;
	memcpy(p, data, (size_t)datan);
	p += datan;
	if (helper_transact(req, (int)(p - req), &resp, &nresp) < 0) {
		free(req);
		return -1;
	}
	free(req);
	ok = (nresp >= 2 && memcmp(resp, "OK", 2) == 0) ? 0 : -1;
	free(resp);
	return ok;
#else
	(void)dst;
	(void)data;
	(void)wdir;
	return -1;
#endif
}

int
luxplumb_recv(LuxPlumbMsg* msg)
{
#ifdef LUX_P9P
	char* resp;
	int n, dstn, wdirn, srcn, typen, datan;
	char* p;
	char* end;

	if (msg == NULL)
		return -1;
	memset(msg, 0, sizeof(*msg));
	if (helper_transact("PLUMBRECV", 9, &resp, &n) < 0)
		return -1;
	if (n < 6 || memcmp(resp, "PLUMB ", 6) != 0) {
		free(resp);
		return -1;
	}
	srcn = dstn = wdirn = typen = datan = 0;
	if (sscanf(resp + 6, "%d %d %d %d %d", &srcn, &dstn, &wdirn, &typen, &datan) != 5) {
		free(resp);
		return -1;
	}
	p = strchr(resp, '\n');
	if (p == NULL) {
		free(resp);
		return -1;
	}
	p++;
	end = resp + n;
	if (p + srcn + dstn + wdirn + typen + datan > end) {
		free(resp);
		return -1;
	}
	msg->src = (char*)malloc((size_t)srcn + 1);
	msg->dst = (char*)malloc((size_t)dstn + 1);
	msg->wdir = (char*)malloc((size_t)wdirn + 1);
	msg->type = (char*)malloc((size_t)typen + 1);
	msg->data = (char*)malloc((size_t)datan + 1);
	if (msg->src == NULL || msg->dst == NULL || msg->wdir == NULL ||
	    msg->type == NULL || msg->data == NULL) {
		luxplumb_msg_free(msg);
		free(resp);
		return -1;
	}
	memcpy(msg->src, p, (size_t)srcn); msg->src[srcn] = '\0'; p += srcn;
	memcpy(msg->dst, p, (size_t)dstn); msg->dst[dstn] = '\0'; p += dstn;
	memcpy(msg->wdir, p, (size_t)wdirn); msg->wdir[wdirn] = '\0'; p += wdirn;
	memcpy(msg->type, p, (size_t)typen); msg->type[typen] = '\0'; p += typen;
	memcpy(msg->data, p, (size_t)datan); msg->data[datan] = '\0';
	free(resp);
	return 0;
#else
	(void)msg;
	return -1;
#endif
}

void
luxplumb_msg_free(LuxPlumbMsg* msg)
{
	if (msg == NULL)
		return;
	free(msg->src);
	free(msg->dst);
	free(msg->wdir);
	free(msg->type);
	free(msg->data);
	msg->src = msg->dst = msg->wdir = msg->type = msg->data = NULL;
}

void
luxdraw_shutdown(void)
{
#ifdef LUX_P9P
	if (helper_fd >= 0) {
		wrframe(helper_fd, "SHUTDOWN", 8);
		close(helper_fd);
		helper_fd = -1;
	}
	if (helper_pid > 0) {
		kill(helper_pid, SIGTERM);
		waitpid(helper_pid, NULL, 0);
		helper_pid = -1;
	}
#endif
	window_open = 0;
}
