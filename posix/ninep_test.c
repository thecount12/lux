/* Tiny 9P2000 client against ninep.c (no Lux). Usage: ./ninep_test */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdio.h>

#include "ninep.h"

static char ctlbuf[256];
static int ctllen;

static int
onread(void* aux, char** data, int* len)
{
	const char* name = (const char*)aux;
	const char* s;
	if (strcmp(name, "status") == 0)
		s = "ok\n";
	else
		s = "";
	*len = (int)strlen(s);
	*data = (char*)malloc((size_t)(*len) + 1);
	if (*data == NULL)
		return -1;
	memcpy(*data, s, (size_t)(*len) + 1);
	return 0;
}

static int
onwrite(void* aux, const char* data, int len)
{
	FILE* fp;
	(void)aux;
	if (len > (int)sizeof(ctlbuf) - 1)
		len = (int)sizeof(ctlbuf) - 1;
	memcpy(ctlbuf, data, (size_t)len);
	ctlbuf[len] = '\0';
	ctllen = len;
	fp = fopen("/tmp/lux-ninep-test-ctl", "w");
	if (fp != NULL) {
		fwrite(ctlbuf, 1, (size_t)ctllen, fp);
		fclose(fp);
	}
	return 0;
}

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

static void
p8(unsigned char** pp, unsigned int v) { *(*pp)++ = (unsigned char)v; }
static void
p16(unsigned char** pp, unsigned int v) { p8(pp, v & 0xff); p8(pp, (v >> 8) & 0xff); }
static void
p32(unsigned char** pp, unsigned int v) {
	p8(pp, v & 0xff); p8(pp, (v >> 8) & 0xff);
	p8(pp, (v >> 16) & 0xff); p8(pp, (v >> 24) & 0xff);
}
static void
pstr(unsigned char** pp, const char* s) {
	int n = (int)strlen(s);
	p16(pp, (unsigned int)n);
	memcpy(*pp, s, (size_t)n);
	*pp += n;
}

static uint32_t g32(const unsigned char* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int
rpc(int fd, unsigned char* body, int n, unsigned char* out, int* outn)
{
	unsigned char hdr[4];
	uint32_t size = (uint32_t)(n + 4);
	hdr[0] = size & 0xff; hdr[1] = (size >> 8) & 0xff;
	hdr[2] = (size >> 16) & 0xff; hdr[3] = (size >> 24) & 0xff;
	if (writen(fd, hdr, 4) != 4) return -1;
	if (writen(fd, body, n) != n) return -1;
	if (readn(fd, hdr, 4) != 4) return -1;
	size = g32(hdr);
	if (size < 7 || size > 8192) return -1;
	if (readn(fd, out, (int)(size - 4)) != (int)(size - 4)) return -1;
	*outn = (int)(size - 4);
	return 0;
}

int
main(void)
{
	int sp[2];
	pid_t pid;
	unsigned char buf[1024];
	unsigned char* p;
	unsigned char resp[8192];
	int nresp;
	uint32_t count;
	NinePFile files[2];
	NinePOps ops;

	memset(files, 0, sizeof(files));
	strcpy(files[0].name, "status");
	files[0].readable = 1;
	files[0].aux = (void*)"status";
	strcpy(files[1].name, "ctl");
	files[1].writable = 1;
	files[1].readable = 0;
	files[1].aux = (void*)"ctl";
	ops.onread = onread;
	ops.onwrite = onwrite;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) {
		perror("socketpair");
		return 1;
	}
	pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}
	if (pid == 0) {
		close(sp[0]);
		ninep_serve(sp[1], files, 2, &ops);
		close(sp[1]);
		_exit(0);
	}
	close(sp[1]);

	/* Tversion */
	p = buf;
	p8(&p, 100); p16(&p, 1); p32(&p, 8192); pstr(&p, "9P2000");
	if (rpc(sp[0], buf, (int)(p - buf), resp, &nresp) < 0 || resp[0] != 101) {
		fprintf(stderr, "Tversion failed\n");
		return 1;
	}

	/* Tattach fid 0 */
	p = buf;
	p8(&p, 104); p16(&p, 1); p32(&p, 0); p32(&p, (uint32_t)~0);
	pstr(&p, "user"); pstr(&p, "");
	if (rpc(sp[0], buf, (int)(p - buf), resp, &nresp) < 0 || resp[0] != 105) {
		fprintf(stderr, "Tattach failed\n");
		return 1;
	}

	/* Twalk 0 -> 1  "status" */
	p = buf;
	p8(&p, 110); p16(&p, 1); p32(&p, 0); p32(&p, 1); p16(&p, 1); pstr(&p, "status");
	if (rpc(sp[0], buf, (int)(p - buf), resp, &nresp) < 0 || resp[0] != 111) {
		fprintf(stderr, "Twalk status failed\n");
		return 1;
	}

	/* Topen fid 1 OREAD */
	p = buf;
	p8(&p, 112); p16(&p, 1); p32(&p, 1); p8(&p, 0);
	if (rpc(sp[0], buf, (int)(p - buf), resp, &nresp) < 0 || resp[0] != 113) {
		fprintf(stderr, "Topen failed\n");
		return 1;
	}

	/* Tread fid 1 */
	p = buf;
	p8(&p, 116); p16(&p, 1); p32(&p, 1);
	p32(&p, 0); p32(&p, 0); /* offset */
	p32(&p, 100);
	if (rpc(sp[0], buf, (int)(p - buf), resp, &nresp) < 0 || resp[0] != 117) {
		fprintf(stderr, "Tread failed\n");
		return 1;
	}
	count = g32(resp + 3);
	if (count != 3 || memcmp(resp + 7, "ok\n", 3) != 0) {
		fprintf(stderr, "Tread data mismatch\n");
		return 1;
	}

	/* Twalk 0 -> 2 "ctl" */
	p = buf;
	p8(&p, 110); p16(&p, 1); p32(&p, 0); p32(&p, 2); p16(&p, 1); pstr(&p, "ctl");
	if (rpc(sp[0], buf, (int)(p - buf), resp, &nresp) < 0 || resp[0] != 111) {
		fprintf(stderr, "Twalk ctl failed\n");
		return 1;
	}
	p = buf;
	p8(&p, 112); p16(&p, 1); p32(&p, 2); p8(&p, 1); /* OWRITE */
	if (rpc(sp[0], buf, (int)(p - buf), resp, &nresp) < 0 || resp[0] != 113) {
		fprintf(stderr, "Topen ctl failed\n");
		return 1;
	}
	p = buf;
	p8(&p, 118); p16(&p, 1); p32(&p, 2);
	p32(&p, 0); p32(&p, 0);
	p32(&p, 5);
	memcpy(p, "hello", 5); p += 5;
	if (rpc(sp[0], buf, (int)(p - buf), resp, &nresp) < 0 || resp[0] != 119) {
		fprintf(stderr, "Twrite failed\n");
		return 1;
	}

	close(sp[0]);
	waitpid(pid, NULL, 0);
	{
		FILE* fp = fopen("/tmp/lux-ninep-test-ctl", "r");
		if (fp == NULL || fread(ctlbuf, 1, sizeof(ctlbuf) - 1, fp) < 5) {
			if (fp) fclose(fp);
			fprintf(stderr, "write data missing\n");
			return 1;
		}
		fclose(fp);
		ctlbuf[5] = '\0';
		unlink("/tmp/lux-ninep-test-ctl");
		if (strcmp(ctlbuf, "hello") != 0) {
			fprintf(stderr, "write data mismatch: %s\n", ctlbuf);
			return 1;
		}
	}
	printf("ninep_test ok\n");
	return 0;
}
