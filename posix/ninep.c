#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "ninep.h"

enum {
	Tversion = 100,
	Rversion = 101,
	Tauth = 102,
	Tattach = 104,
	Rattach = 105,
	Rerror = 107,
	Tflush = 108,
	Rflush = 109,
	Twalk = 110,
	Rwalk = 111,
	Topen = 112,
	Ropen = 113,
	Tcreate = 114,
	Tread = 116,
	Rread = 117,
	Twrite = 118,
	Rwrite = 119,
	Tclunk = 120,
	Rclunk = 121,
	Tremove = 122,
	Tstat = 124,
	Rstat = 125,
	Twstat = 126
};

enum {
	QTDIR = 0x80,
	QTFILE = 0x00,
	DMDIR = 0x80000000u
};

#define MAXFID 128

typedef struct {
	int inuse;
	uint32_t fid;
	int fileidx; /* -1 = root */
	int opened;
	char* cached;
	int cachedlen;
} Fid;

static uint32_t
g32(const unsigned char* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t
g16(const unsigned char* p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint64_t
g64(const unsigned char* p)
{
	return (uint64_t)g32(p) | ((uint64_t)g32(p + 4) << 32);
}

static void
p8(unsigned char** pp, unsigned int v)
{
	*(*pp)++ = (unsigned char)v;
}

static void
p16(unsigned char** pp, unsigned int v)
{
	p8(pp, v & 0xff);
	p8(pp, (v >> 8) & 0xff);
}

static void
p32(unsigned char** pp, uint32_t v)
{
	p8(pp, v & 0xff);
	p8(pp, (v >> 8) & 0xff);
	p8(pp, (v >> 16) & 0xff);
	p8(pp, (v >> 24) & 0xff);
}

static void
p64(unsigned char** pp, uint64_t v)
{
	p32(pp, (uint32_t)v);
	p32(pp, (uint32_t)(v >> 32));
}

static void
pstr(unsigned char** pp, const char* s)
{
	int n = s ? (int)strlen(s) : 0;
	p16(pp, (unsigned int)n);
	if (n > 0) {
		memcpy(*pp, s, (size_t)n);
		*pp += n;
	}
}

static void
pqid(unsigned char** pp, unsigned char type, uint32_t vers, uint64_t path)
{
	p8(pp, type);
	p32(pp, vers);
	p64(pp, path);
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

static Fid*
fidfind(Fid* fids, uint32_t fid)
{
	int i;
	for (i = 0; i < MAXFID; i++) {
		if (fids[i].inuse && fids[i].fid == fid)
			return &fids[i];
	}
	return NULL;
}

static Fid*
fidalloc(Fid* fids, uint32_t fid)
{
	int i;
	Fid* f = fidfind(fids, fid);
	if (f != NULL)
		return NULL;
	for (i = 0; i < MAXFID; i++) {
		if (!fids[i].inuse) {
			memset(&fids[i], 0, sizeof(fids[i]));
			fids[i].inuse = 1;
			fids[i].fid = fid;
			fids[i].fileidx = -1;
			return &fids[i];
		}
	}
	return NULL;
}

static void
fidfree(Fid* f)
{
	if (f == NULL)
		return;
	free(f->cached);
	memset(f, 0, sizeof(*f));
}

static NinePFile*
filebyname(NinePFile* files, int nfiles, const char* name, int nlen)
{
	int i;
	for (i = 0; i < nfiles; i++) {
		if ((int)strlen(files[i].name) == nlen &&
		    memcmp(files[i].name, name, (size_t)nlen) == 0)
			return &files[i];
	}
	return NULL;
}

static int
fileindex(NinePFile* files, int nfiles, NinePFile* f)
{
	int i;
	if (f == NULL)
		return -1;
	for (i = 0; i < nfiles; i++) {
		if (&files[i] == f)
			return i;
	}
	return -1;
}

static uint32_t
nowsec(void)
{
	return (uint32_t)time(NULL);
}

static void
packstat(unsigned char** pp, int isdir, int idx, const char* name,
         uint64_t length, int modebits)
{
	unsigned char* sizep;
	unsigned char* start;
	uint32_t mode;
	uint32_t t = nowsec();

	sizep = *pp;
	p16(pp, 0);
	start = *pp;
	p16(pp, 0); /* type */
	p32(pp, 0); /* dev */
	if (isdir)
		pqid(pp, QTDIR, 0, 0);
	else
		pqid(pp, QTFILE, 0, (uint64_t)(idx + 1));
	mode = (uint32_t)modebits;
	if (isdir)
		mode |= DMDIR;
	p32(pp, mode);
	p32(pp, t);
	p32(pp, t);
	p64(pp, length);
	pstr(pp, name);
	pstr(pp, "lux");
	pstr(pp, "lux");
	pstr(pp, "lux");
	{
		int sz = (int)(*pp - start);
		sizep[0] = (unsigned char)(sz & 0xff);
		sizep[1] = (unsigned char)((sz >> 8) & 0xff);
	}
}

static int
send9p(int fd, unsigned char* body, int n)
{
	unsigned char hdr[4];
	int size = n + 4;
	hdr[0] = (unsigned char)(size & 0xff);
	hdr[1] = (unsigned char)((size >> 8) & 0xff);
	hdr[2] = (unsigned char)((size >> 16) & 0xff);
	hdr[3] = (unsigned char)((size >> 24) & 0xff);
	if (writen(fd, hdr, 4) != 4)
		return -1;
	if (n > 0 && writen(fd, body, n) != n)
		return -1;
	return 0;
}

static int
rerror(int fd, uint16_t tag, const char* ename)
{
	unsigned char buf[256];
	unsigned char* p = buf;
	p8(&p, Rerror);
	p16(&p, tag);
	pstr(&p, ename);
	return send9p(fd, buf, (int)(p - buf));
}

static int
ensurecache(Fid* f, NinePFile* files, int nfiles, NinePOps* ops)
{
	NinePFile* file;
	char* data;
	int len;

	(void)nfiles;
	if (f->fileidx < 0)
		return 0;
	if (f->cached != NULL)
		return 0;
	if (ops == NULL || ops->onread == NULL)
		return -1;
	file = &files[f->fileidx];
	data = NULL;
	len = 0;
	if (ops->onread(file->aux, &data, &len) < 0)
		return -1;
	f->cached = data;
	f->cachedlen = len;
	return 0;
}

static int
dirdata(NinePFile* files, int nfiles, uint64_t off, uint32_t count,
        unsigned char* out, int* outn)
{
	unsigned char tmp[NINEP_MSIZE];
	unsigned char* p = tmp;
	int i, total;

	for (i = 0; i < nfiles; i++)
		packstat(&p, 0, i, files[i].name, 0,
		         files[i].writable ? 0666 : 0444);
	total = (int)(p - tmp);
	if (off >= (uint64_t)total) {
		*outn = 0;
		return 0;
	}
	if (off + count > (uint64_t)total)
		count = (uint32_t)(total - (int)off);
	memcpy(out, tmp + (size_t)off, count);
	*outn = (int)count;
	return 0;
}

int
ninep_serve(int fd, NinePFile* files, int nfiles, NinePOps* ops)
{
	Fid fids[MAXFID];
	unsigned char* msg;
	uint32_t msize = NINEP_MSIZE;

	if (nfiles < 0 || nfiles > NINEP_MAXFILES)
		return -1;
	memset(fids, 0, sizeof(fids));

	for (;;) {
		unsigned char szb[4];
		uint32_t size;
		unsigned char* p;
		unsigned char* end;
		uint8_t type;
		uint16_t tag;
		unsigned char out[NINEP_MSIZE];
		unsigned char* op;

		if (readn(fd, szb, 4) != 4)
			break;
		size = g32(szb);
		if (size < 7 || size > msize)
			break;
		msg = (unsigned char*)malloc(size - 4);
		if (msg == NULL)
			break;
		if (readn(fd, msg, (int)(size - 4)) != (int)(size - 4)) {
			free(msg);
			break;
		}
		p = msg;
		end = msg + (size - 4);
		type = *p++;
		tag = g16(p);
		p += 2;
		op = out;

		switch (type) {
		case Tversion: {
			uint32_t cm;
			if (p + 4 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			cm = g32(p);
			p += 4;
			if (cm < 256) {
				rerror(fd, tag, "msize");
				break;
			}
			if (cm < msize)
				msize = cm;
			p8(&op, Rversion);
			p16(&op, tag);
			p32(&op, msize);
			pstr(&op, "9P2000");
			send9p(fd, out, (int)(op - out));
			break;
		}
		case Tauth:
			rerror(fd, tag, "no auth");
			break;
		case Tattach: {
			uint32_t fid;
			Fid* f;
			if (p + 4 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p);
			f = fidalloc(fids, fid);
			if (f == NULL) {
				rerror(fd, tag, "fid in use");
				break;
			}
			f->fileidx = -1;
			p8(&op, Rattach);
			p16(&op, tag);
			pqid(&op, QTDIR, 0, 0);
			send9p(fd, out, (int)(op - out));
			break;
		}
		case Tflush:
			p8(&op, Rflush);
			p16(&op, tag);
			send9p(fd, out, (int)(op - out));
			break;
		case Twalk: {
			uint32_t fid, newfid;
			uint16_t nwname, i;
			Fid* f;
			Fid* nf;
			int idx;
			unsigned char* qidp;
			uint16_t nwqid = 0;

			if (p + 10 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p); p += 4;
			newfid = g32(p); p += 4;
			nwname = g16(p); p += 2;
			f = fidfind(fids, fid);
			if (f == NULL) {
				rerror(fd, tag, "unknown fid");
				break;
			}
			if (fid == newfid)
				nf = f;
			else {
				nf = fidalloc(fids, newfid);
				if (nf == NULL) {
					rerror(fd, tag, "fid in use");
					break;
				}
				nf->fileidx = f->fileidx;
			}
			idx = f->fileidx;
			p8(&op, Rwalk);
			p16(&op, tag);
			qidp = op;
			p16(&op, 0);
			for (i = 0; i < nwname; i++) {
				uint16_t nlen;
				NinePFile* file;
				if (p + 2 > end)
					break;
				nlen = g16(p); p += 2;
				if (p + nlen > end)
					break;
				if (nlen == 2 && memcmp(p, "..", 2) == 0) {
					idx = -1;
					pqid(&op, QTDIR, 0, 0);
					nwqid++;
					p += nlen;
					continue;
				}
				if (idx != -1) {
					/* only one level */
					break;
				}
				file = filebyname(files, nfiles, (char*)p, nlen);
				if (file == NULL)
					break;
				idx = fileindex(files, nfiles, file);
				pqid(&op, QTFILE, 0, (uint64_t)(idx + 1));
				nwqid++;
				p += nlen;
			}
			if (nwname > 0 && nwqid == 0) {
				if (nf != f)
					fidfree(nf);
				rerror(fd, tag, "file not found");
				break;
			}
			if (nwqid == nwname)
				nf->fileidx = idx;
			qidp[0] = (unsigned char)(nwqid & 0xff);
			qidp[1] = (unsigned char)((nwqid >> 8) & 0xff);
			send9p(fd, out, (int)(op - out));
			break;
		}
		case Topen: {
			uint32_t fid;
			uint8_t mode;
			Fid* f;
			int isdir;
			int omode;

			if (p + 5 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p); p += 4;
			mode = *p++;
			f = fidfind(fids, fid);
			if (f == NULL) {
				rerror(fd, tag, "unknown fid");
				break;
			}
			isdir = (f->fileidx < 0);
			omode = mode & 0x3;
			if (!isdir) {
				NinePFile* file = &files[f->fileidx];
				if ((omode == 0 || omode == 2) && !file->readable) {
					rerror(fd, tag, "permission denied");
					break;
				}
				if ((omode == 1 || omode == 2) && !file->writable) {
					rerror(fd, tag, "permission denied");
					break;
				}
				free(f->cached);
				f->cached = NULL;
				f->cachedlen = 0;
			}
			f->opened = 1;
			p8(&op, Ropen);
			p16(&op, tag);
			if (isdir)
				pqid(&op, QTDIR, 0, 0);
			else
				pqid(&op, QTFILE, 0, (uint64_t)(f->fileidx + 1));
			p32(&op, 0); /* iounit */
			send9p(fd, out, (int)(op - out));
			break;
		}
		case Tread: {
			uint32_t fid, count;
			uint64_t off;
			Fid* f;
			int nout = 0;
			unsigned char data[NINEP_MSIZE];

			if (p + 16 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p); p += 4;
			off = g64(p); p += 8;
			count = g32(p);
			f = fidfind(fids, fid);
			if (f == NULL || !f->opened) {
				rerror(fd, tag, "bad fid");
				break;
			}
			if (count > msize - 32)
				count = msize - 32;
			if (f->fileidx < 0) {
				dirdata(files, nfiles, off, count, data, &nout);
			} else {
				if (ensurecache(f, files, nfiles, ops) < 0) {
					rerror(fd, tag, "read error");
					break;
				}
				if (off >= (uint64_t)f->cachedlen)
					nout = 0;
				else {
					if (off + count > (uint64_t)f->cachedlen)
						count = (uint32_t)(f->cachedlen - (int)off);
					memcpy(data, f->cached + (size_t)off, count);
					nout = (int)count;
				}
			}
			p8(&op, Rread);
			p16(&op, tag);
			p32(&op, (uint32_t)nout);
			memcpy(op, data, (size_t)nout);
			op += nout;
			send9p(fd, out, (int)(op - out));
			break;
		}
		case Twrite: {
			uint32_t fid, count;
			uint64_t off;
			Fid* f;
			NinePFile* file;

			if (p + 16 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p); p += 4;
			off = g64(p); p += 8;
			count = g32(p); p += 4;
			(void)off;
			f = fidfind(fids, fid);
			if (f == NULL || f->fileidx < 0 || !f->opened) {
				rerror(fd, tag, "bad fid");
				break;
			}
			file = &files[f->fileidx];
			if (!file->writable || ops == NULL || ops->onwrite == NULL) {
				rerror(fd, tag, "permission denied");
				break;
			}
			if (p + count > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			if (ops->onwrite(file->aux, (char*)p, (int)count) < 0) {
				rerror(fd, tag, "write error");
				break;
			}
			free(f->cached);
			f->cached = NULL;
			f->cachedlen = 0;
			p8(&op, Rwrite);
			p16(&op, tag);
			p32(&op, count);
			send9p(fd, out, (int)(op - out));
			break;
		}
		case Tclunk: {
			uint32_t fid;
			Fid* f;
			if (p + 4 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p);
			f = fidfind(fids, fid);
			if (f == NULL) {
				rerror(fd, tag, "unknown fid");
				break;
			}
			fidfree(f);
			p8(&op, Rclunk);
			p16(&op, tag);
			send9p(fd, out, (int)(op - out));
			break;
		}
		case Tstat: {
			uint32_t fid;
			Fid* f;
			unsigned char* sizep;
			unsigned char* statstart;
			const char* name;
			int isdir;
			int modebits;
			uint64_t length = 0;

			if (p + 4 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p);
			f = fidfind(fids, fid);
			if (f == NULL) {
				rerror(fd, tag, "unknown fid");
				break;
			}
			isdir = (f->fileidx < 0);
			name = isdir ? "/" : files[f->fileidx].name;
			modebits = isdir ? 0555 :
				(files[f->fileidx].writable ? 0666 : 0444);
			if (!isdir && files[f->fileidx].readable)
				ensurecache(f, files, nfiles, ops);
			if (!isdir && f->cached != NULL)
				length = (uint64_t)f->cachedlen;
			p8(&op, Rstat);
			p16(&op, tag);
			sizep = op;
			p16(&op, 0);
			statstart = op;
			packstat(&op, isdir, f->fileidx, name, length, modebits);
			{
				int sz = (int)(op - statstart);
				sizep[0] = (unsigned char)(sz & 0xff);
				sizep[1] = (unsigned char)((sz >> 8) & 0xff);
			}
			send9p(fd, out, (int)(op - out));
			break;
		}
		case Tcreate:
		case Tremove:
		case Twstat:
			rerror(fd, tag, "not supported");
			break;
		default:
			rerror(fd, tag, "unknown message");
			break;
		}
		free(msg);
	}

	{
		int i;
		for (i = 0; i < MAXFID; i++)
			fidfree(&fids[i]);
	}
	return 0;
}

enum {
	ADDR_UNIX = 0,
	ADDR_TCP = 1,
	OREAD = 0,
	OWRITE = 1,
	NOFID = (int)0xffffffffu,
	NINEP_MAXREAD = 8 * 1024 * 1024,
	NINEP_MAXWALK = 16
};

typedef struct {
	int kind;
	char host[256];
	char path[108];
	int port;
	int any;
} Addr;

struct NinePClient {
	int fd;
	uint32_t msize;
	uint16_t tag;
	uint32_t nextfid;
	char err[NINEP_ERRMAX];
};

static int
parseaddr(const char* s, Addr* a)
{
	const char* p;
	const char* bang;
	const char* colon;
	size_t n;

	memset(a, 0, sizeof(*a));
	if (s == NULL || s[0] == '\0')
		return -1;
	if (strncmp(s, "unix!", 5) == 0) {
		a->kind = ADDR_UNIX;
		if (strlen(s + 5) >= sizeof(a->path) || s[5] == '\0')
			return -1;
		strcpy(a->path, s + 5);
		return 0;
	}
	if (strncmp(s, "tcp!", 4) == 0) {
		a->kind = ADDR_TCP;
		p = s + 4;
		bang = strrchr(p, '!');
		if (bang == NULL || bang == p || bang[1] == '\0')
			return -1;
		n = (size_t)(bang - p);
		if (n >= sizeof(a->host))
			return -1;
		memcpy(a->host, p, n);
		a->host[n] = '\0';
		a->port = atoi(bang + 1);
		if (a->port < 0 || a->port > 65535)
			return -1;
		if (a->host[0] == '*' || strcmp(a->host, "0.0.0.0") == 0)
			a->any = 1;
		return 0;
	}
	if (s[0] == ':') {
		a->kind = ADDR_TCP;
		a->any = 1;
		strcpy(a->host, "*");
		a->port = atoi(s + 1);
		if (a->port <= 0 || a->port > 65535)
			return -1;
		return 0;
	}
	if (strchr(s, '/') == NULL) {
		colon = strrchr(s, ':');
		if (colon != NULL && colon != s &&
		    colon[1] >= '0' && colon[1] <= '9') {
			a->kind = ADDR_TCP;
			n = (size_t)(colon - s);
			if (n >= sizeof(a->host))
				return -1;
			memcpy(a->host, s, n);
			a->host[n] = '\0';
			a->port = atoi(colon + 1);
			if (a->port < 0 || a->port > 65535)
				return -1;
			if (a->host[0] == '*' || strcmp(a->host, "0.0.0.0") == 0)
				a->any = 1;
			return 0;
		}
	}
	a->kind = ADDR_UNIX;
	if (strlen(s) >= sizeof(a->path))
		return -1;
	strcpy(a->path, s);
	return 0;
}

static int
tcpconnect(const char* host, int port)
{
	struct addrinfo hints;
	struct addrinfo* res;
	struct addrinfo* rp;
	char portstr[16];
	int fd = -1;
	int rc;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(portstr, sizeof(portstr), "%d", port);
	rc = getaddrinfo(host, portstr, &hints, &res);
	if (rc != 0)
		return -1;
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

int
ninep_listen(const char* addr, NinePListener* lis)
{
	Addr a;
	int fd;

	if (lis == NULL || parseaddr(addr, &a) < 0)
		return -1;
	memset(lis, 0, sizeof(*lis));
	lis->fd = -1;
	if (a.kind == ADDR_UNIX) {
		struct sockaddr_un un;
		if (strlen(a.path) >= sizeof(un.sun_path))
			return -1;
		fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
			return -1;
		memset(&un, 0, sizeof(un));
		un.sun_family = AF_UNIX;
		strcpy(un.sun_path, a.path);
		unlink(a.path);
		if (bind(fd, (struct sockaddr*)&un, sizeof(un)) < 0) {
			close(fd);
			return -1;
		}
		chmod(a.path, 0600);
		if (listen(fd, 8) < 0) {
			close(fd);
			unlink(a.path);
			return -1;
		}
		lis->fd = fd;
		lis->tcp = 0;
		strcpy(lis->path, a.path);
		return 0;
	}
	{
		struct sockaddr_in in;
		struct addrinfo hints;
		struct addrinfo* res = NULL;
		char portstr[16];
		int resolved = 0;
		int opt = 1;

		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0)
			return -1;
		opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		memset(&in, 0, sizeof(in));
		in.sin_family = AF_INET;
		in.sin_port = htons((uint16_t)a.port);
		if (a.any)
			in.sin_addr.s_addr = htonl(INADDR_ANY);
		else if (inet_pton(AF_INET, a.host, &in.sin_addr) == 1)
			resolved = 1;
		else {
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_flags = AI_PASSIVE;
			snprintf(portstr, sizeof(portstr), "%d", a.port);
			if (getaddrinfo(a.host, portstr, &hints, &res) != 0) {
				close(fd);
				return -1;
			}
			if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
				freeaddrinfo(res);
				close(fd);
				return -1;
			}
			freeaddrinfo(res);
			resolved = 2;
		}
		if (resolved != 2 && bind(fd, (struct sockaddr*)&in, sizeof(in)) < 0) {
			close(fd);
			return -1;
		}
		if (listen(fd, 8) < 0) {
			close(fd);
			return -1;
		}
		lis->fd = fd;
		lis->tcp = 1;
		return 0;
	}
}

int
ninep_accept(NinePListener* lis)
{
	int fd;

	if (lis == NULL || lis->fd < 0)
		return -1;
	for (;;) {
		fd = accept(lis->fd, NULL, NULL);
		if (fd >= 0)
			return fd;
		if (errno != EINTR)
			return -1;
	}
}

void
ninep_unlisten(NinePListener* lis)
{
	if (lis == NULL)
		return;
	if (lis->fd >= 0) {
		close(lis->fd);
		lis->fd = -1;
	}
	if (!lis->tcp && lis->path[0] != '\0')
		unlink(lis->path);
}

int
ninep_bound_port(NinePListener* lis)
{
	struct sockaddr_in in;
	socklen_t n;

	if (lis == NULL || lis->fd < 0 || !lis->tcp)
		return -1;
	n = sizeof(in);
	if (getsockname(lis->fd, (struct sockaddr*)&in, &n) < 0)
		return -1;
	return (int)ntohs(in.sin_port);
}

int
ninep_dial(const char* addr)
{
	Addr a;
	int fd;
	struct sockaddr_un un;

	if (parseaddr(addr, &a) < 0)
		return -1;
	if (a.kind == ADDR_UNIX) {
		if (strlen(a.path) >= sizeof(un.sun_path))
			return -1;
		fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
			return -1;
		memset(&un, 0, sizeof(un));
		un.sun_family = AF_UNIX;
		strcpy(un.sun_path, a.path);
		if (connect(fd, (struct sockaddr*)&un, sizeof(un)) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
	if (a.any)
		return tcpconnect("127.0.0.1", a.port);
	return tcpconnect(a.host, a.port);
}

static void
seterr(NinePClient* c, const char* s)
{
	size_t n;

	if (c == NULL)
		return;
	if (s == NULL)
		s = "9p error";
	n = strlen(s);
	if (n >= sizeof(c->err))
		n = sizeof(c->err) - 1;
	memcpy(c->err, s, n);
	c->err[n] = '\0';
}

const char*
ninep_client_err(NinePClient* c)
{
	if (c == NULL || c->err[0] == '\0')
		return "9p error";
	return c->err;
}

void
ninep_list_free(NinePList* list)
{
	int i;

	if (list == NULL)
		return;
	if (list->names != NULL) {
		for (i = 0; i < list->count; i++)
			free(list->names[i]);
		free(list->names);
	}
	list->names = NULL;
	list->count = 0;
}

void
ninep_client_close(NinePClient* c)
{
	if (c == NULL)
		return;
	if (c->fd >= 0)
		close(c->fd);
	free(c);
}

static uint16_t
nexttag(NinePClient* c)
{
	uint16_t t;

	t = c->tag++;
	if (c->tag == 0 || c->tag == 0xffff)
		c->tag = 1;
	return t;
}

static uint32_t
nextfid(NinePClient* c)
{
	uint32_t f;

	f = c->nextfid++;
	if (c->nextfid == 0)
		c->nextfid = 1;
	return f;
}

static int
rpc(NinePClient* c, unsigned char* tx, int txn, unsigned char* rx, int rxmax, int* rxn)
{
	unsigned char hdr[4];
	uint32_t size;
	uint16_t nlen;

	if (c == NULL || c->fd < 0)
		return -1;
	size = (uint32_t)(txn + 4);
	hdr[0] = (unsigned char)(size & 0xff);
	hdr[1] = (unsigned char)((size >> 8) & 0xff);
	hdr[2] = (unsigned char)((size >> 16) & 0xff);
	hdr[3] = (unsigned char)((size >> 24) & 0xff);
	if (writen(c->fd, hdr, 4) != 4)
		return -1;
	if (txn > 0 && writen(c->fd, tx, txn) != txn)
		return -1;
	if (readn(c->fd, hdr, 4) != 4) {
		seterr(c, "eof");
		return -1;
	}
	size = g32(hdr);
	if (size < 7 || size > c->msize) {
		seterr(c, "bad size");
		return -1;
	}
	if ((int)(size - 4) > rxmax) {
		seterr(c, "message too big");
		return -1;
	}
	if (readn(c->fd, rx, (int)(size - 4)) != (int)(size - 4)) {
		seterr(c, "eof");
		return -1;
	}
	*rxn = (int)(size - 4);
	if (rx[0] == Rerror) {
		if (*rxn < 5) {
			seterr(c, "Rerror");
			return -1;
		}
		nlen = g16(rx + 3);
		if (5 + nlen > *rxn)
			nlen = (uint16_t)(*rxn - 5);
		if (nlen >= NINEP_ERRMAX)
			nlen = NINEP_ERRMAX - 1;
		memcpy(c->err, rx + 5, nlen);
		c->err[nlen] = '\0';
		return -1;
	}
	return 0;
}

static int
gstr(const unsigned char* p, const unsigned char* end, char* out, int outmax)
{
	uint16_t n;

	if (p + 2 > end)
		return -1;
	n = g16(p);
	p += 2;
	if (p + n > end)
		return -1;
	if ((int)n >= outmax)
		return -1;
	memcpy(out, p, n);
	out[n] = '\0';
	return 2 + (int)n;
}

static int
parsestat(const unsigned char* p, int n, NinePStat* st)
{
	const unsigned char* end;
	uint16_t sz;
	unsigned char qtype;
	int k;

	if (st == NULL || n < 2)
		return -1;
	memset(st, 0, sizeof(*st));
	sz = g16(p);
	p += 2;
	if (2 + (int)sz > n)
		return -1;
	end = p + sz;
	if (p + 2 + 4 + 13 + 4 + 4 + 4 + 8 > end)
		return -1;
	p += 2;
	p += 4;
	qtype = *p;
	p += 13;
	st->isdir = (qtype & 0x80) ? 1 : 0;
	st->mode = g32(p);
	p += 4;
	p += 4;
	p += 4;
	st->length = g64(p);
	p += 8;
	k = gstr(p, end, st->name, (int)sizeof(st->name));
	if (k < 0)
		return -1;
	p += k;
	k = gstr(p, end, st->uid, (int)sizeof(st->uid));
	if (k < 0)
		return -1;
	p += k;
	k = gstr(p, end, st->gid, (int)sizeof(st->gid));
	if (k < 0)
		return -1;
	return 0;
}

static int
splitpath(const char* path, char names[][256], int max)
{
	int n = 0;
	const char* p;
	const char* start;
	int len;

	if (path == NULL)
		return 0;
	p = path;
	while (*p == '/')
		p++;
	while (*p != '\0' && n < max) {
		start = p;
		while (*p != '\0' && *p != '/')
			p++;
		len = (int)(p - start);
		if (len == 0)
			break;
		if (len >= 256)
			return -1;
		memcpy(names[n], start, (size_t)len);
		names[n][len] = '\0';
		n++;
		while (*p == '/')
			p++;
	}
	if (*p != '\0')
		return -1;
	return n;
}

static int
clunkfid(NinePClient* c, uint32_t fid)
{
	unsigned char tx[16];
	unsigned char* p = tx;
	unsigned char rx[64];
	int rxn;

	p8(&p, Tclunk);
	p16(&p, nexttag(c));
	p32(&p, fid);
	return rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn);
}

static int
walkto(NinePClient* c, const char* path, uint32_t* outfid)
{
	char names[NINEP_MAXWALK][256];
	int nw;
	uint32_t newfid;
	unsigned char tx[NINEP_MSIZE];
	unsigned char rx[NINEP_MSIZE];
	unsigned char* p;
	int i;
	int rxn;
	uint16_t nwqid;

	nw = splitpath(path, names, NINEP_MAXWALK);
	if (nw < 0) {
		seterr(c, "bad path");
		return -1;
	}
	newfid = nextfid(c);
	p = tx;
	p8(&p, Twalk);
	p16(&p, nexttag(c));
	p32(&p, 0);
	p32(&p, newfid);
	p16(&p, (unsigned int)nw);
	for (i = 0; i < nw; i++)
		pstr(&p, names[i]);
	if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0)
		return -1;
	if (rxn < 5 || rx[0] != Rwalk) {
		seterr(c, "bad Rwalk");
		return -1;
	}
	nwqid = g16(rx + 3);
	if (nwqid != (uint16_t)nw) {
		seterr(c, "file not found");
		clunkfid(c, newfid);
		return -1;
	}
	*outfid = newfid;
	return 0;
}

static int
openfid(NinePClient* c, uint32_t fid, int mode, int* isdir)
{
	unsigned char tx[16];
	unsigned char* p = tx;
	unsigned char rx[64];
	int rxn;

	p8(&p, Topen);
	p16(&p, nexttag(c));
	p32(&p, fid);
	p8(&p, (unsigned int)mode);
	if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0)
		return -1;
	if (rxn < 16 || rx[0] != Ropen) {
		seterr(c, "bad Ropen");
		return -1;
	}
	if (isdir != NULL)
		*isdir = (rx[3] & 0x80) ? 1 : 0;
	return 0;
}

NinePClient*
ninep_client_attach(int fd, const char* uname, const char* aname)
{
	NinePClient* c;
	unsigned char tx[256];
	unsigned char rx[NINEP_MSIZE];
	unsigned char* p;
	int rxn;
	uint32_t msize;

	if (fd < 0)
		return NULL;
	c = (NinePClient*)calloc(1, sizeof(*c));
	if (c == NULL) {
		close(fd);
		return NULL;
	}
	c->fd = fd;
	c->msize = NINEP_MSIZE;
	c->tag = 1;
	c->nextfid = 1;
	if (uname == NULL || uname[0] == '\0')
		uname = "none";
	if (aname == NULL)
		aname = "";

	p = tx;
	p8(&p, Tversion);
	p16(&p, 0xffff);
	p32(&p, NINEP_MSIZE);
	pstr(&p, "9P2000");
	if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0) {
		ninep_client_close(c);
		return NULL;
	}
	if (rxn < 9 || rx[0] != Rversion) {
		seterr(c, "bad Rversion");
		ninep_client_close(c);
		return NULL;
	}
	msize = g32(rx + 3);
	if (msize < 256) {
		seterr(c, "msize");
		ninep_client_close(c);
		return NULL;
	}
	if (msize < c->msize)
		c->msize = msize;

	p = tx;
	p8(&p, Tattach);
	p16(&p, nexttag(c));
	p32(&p, 0);
	p32(&p, (uint32_t)NOFID);
	pstr(&p, uname);
	pstr(&p, aname);
	if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0) {
		ninep_client_close(c);
		return NULL;
	}
	if (rxn < 16 || rx[0] != Rattach) {
		seterr(c, "bad Rattach");
		ninep_client_close(c);
		return NULL;
	}
	c->err[0] = '\0';
	return c;
}

int
ninep_client_read(NinePClient* c, const char* path, char** data, int* len)
{
	uint32_t fid;
	int isdir = 0;
	uint64_t off = 0;
	char* buf = NULL;
	int cap = 0;
	int used = 0;

	if (data == NULL || len == NULL) {
		seterr(c, "bad args");
		return -1;
	}
	*data = NULL;
	*len = 0;
	if (walkto(c, path, &fid) < 0)
		return -1;
	if (openfid(c, fid, OREAD, &isdir) < 0) {
		clunkfid(c, fid);
		return -1;
	}
	if (isdir) {
		seterr(c, "is a directory");
		clunkfid(c, fid);
		return -1;
	}
	for (;;) {
		unsigned char tx[32];
		unsigned char rx[NINEP_MSIZE];
		unsigned char* p = tx;
		int rxn;
		uint32_t count;
		uint32_t want;

		want = c->msize - 32;
		p8(&p, Tread);
		p16(&p, nexttag(c));
		p32(&p, fid);
		p64(&p, off);
		p32(&p, want);
		if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0) {
			free(buf);
			clunkfid(c, fid);
			return -1;
		}
		if (rxn < 7 || rx[0] != Rread) {
			seterr(c, "bad Rread");
			free(buf);
			clunkfid(c, fid);
			return -1;
		}
		count = g32(rx + 3);
		if (7 + (int)count > rxn)
			count = (uint32_t)(rxn - 7);
		if (count == 0)
			break;
		if (used + (int)count > NINEP_MAXREAD) {
			seterr(c, "file too large");
			free(buf);
			clunkfid(c, fid);
			return -1;
		}
		if (used + (int)count + 1 > cap) {
			char* nbuf;
			int ncap = cap == 0 ? 4096 : cap * 2;
			while (ncap < used + (int)count + 1)
				ncap *= 2;
			nbuf = (char*)realloc(buf, (size_t)ncap);
			if (nbuf == NULL) {
				seterr(c, "out of memory");
				free(buf);
				clunkfid(c, fid);
				return -1;
			}
			buf = nbuf;
			cap = ncap;
		}
		memcpy(buf + used, rx + 7, count);
		used += (int)count;
		off += count;
	}
	clunkfid(c, fid);
	if (buf == NULL) {
		buf = (char*)malloc(1);
		if (buf == NULL) {
			seterr(c, "out of memory");
			return -1;
		}
	}
	buf[used] = '\0';
	*data = buf;
	*len = used;
	c->err[0] = '\0';
	return 0;
}

int
ninep_client_write(NinePClient* c, const char* path, const char* data, int len)
{
	uint32_t fid;
	int isdir = 0;
	uint64_t off = 0;
	int sent = 0;

	if (data == NULL)
		data = "";
	if (len < 0)
		len = (int)strlen(data);
	if (walkto(c, path, &fid) < 0)
		return -1;
	if (openfid(c, fid, OWRITE, &isdir) < 0) {
		clunkfid(c, fid);
		return -1;
	}
	if (isdir) {
		seterr(c, "is a directory");
		clunkfid(c, fid);
		return -1;
	}
	while (sent < len) {
		unsigned char tx[NINEP_MSIZE];
		unsigned char rx[32];
		unsigned char* p = tx;
		int rxn;
		uint32_t chunk;
		uint32_t got;

		chunk = (uint32_t)(len - sent);
		if (chunk > c->msize - 32)
			chunk = c->msize - 32;
		p8(&p, Twrite);
		p16(&p, nexttag(c));
		p32(&p, fid);
		p64(&p, off);
		p32(&p, chunk);
		memcpy(p, data + sent, chunk);
		p += chunk;
		if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0) {
			clunkfid(c, fid);
			return -1;
		}
		if (rxn < 7 || rx[0] != Rwrite) {
			seterr(c, "bad Rwrite");
			clunkfid(c, fid);
			return -1;
		}
		got = g32(rx + 3);
		if (got == 0)
			break;
		sent += (int)got;
		off += got;
	}
	clunkfid(c, fid);
	if (sent != len) {
		seterr(c, "short write");
		return -1;
	}
	c->err[0] = '\0';
	return sent;
}

int
ninep_client_stat(NinePClient* c, const char* path, NinePStat* st)
{
	uint32_t fid;
	unsigned char tx[16];
	unsigned char rx[NINEP_MSIZE];
	unsigned char* p;
	int rxn;
	uint16_t nstat;

	if (st == NULL) {
		seterr(c, "bad args");
		return -1;
	}
	if (walkto(c, path, &fid) < 0)
		return -1;
	p = tx;
	p8(&p, Tstat);
	p16(&p, nexttag(c));
	p32(&p, fid);
	if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0) {
		clunkfid(c, fid);
		return -1;
	}
	clunkfid(c, fid);
	if (rxn < 5 || rx[0] != Rstat) {
		seterr(c, "bad Rstat");
		return -1;
	}
	nstat = g16(rx + 3);
	if (5 + nstat > rxn) {
		seterr(c, "bad Rstat");
		return -1;
	}
	if (parsestat(rx + 5, nstat, st) < 0) {
		seterr(c, "bad stat");
		return -1;
	}
	c->err[0] = '\0';
	return 0;
}

int
ninep_client_ls(NinePClient* c, const char* path, NinePList* list)
{
	uint32_t fid;
	int isdir = 0;
	uint64_t off = 0;
	char* raw = NULL;
	int used = 0;
	int cap = 0;
	int pos;

	if (list == NULL) {
		seterr(c, "bad args");
		return -1;
	}
	list->names = NULL;
	list->count = 0;
	if (walkto(c, path, &fid) < 0)
		return -1;
	if (openfid(c, fid, OREAD, &isdir) < 0) {
		clunkfid(c, fid);
		return -1;
	}
	if (!isdir) {
		seterr(c, "not a directory");
		clunkfid(c, fid);
		return -1;
	}
	for (;;) {
		unsigned char tx[32];
		unsigned char rx[NINEP_MSIZE];
		unsigned char* p = tx;
		int rxn;
		uint32_t count;

		p8(&p, Tread);
		p16(&p, nexttag(c));
		p32(&p, fid);
		p64(&p, off);
		p32(&p, c->msize - 32);
		if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0) {
			free(raw);
			clunkfid(c, fid);
			return -1;
		}
		if (rxn < 7 || rx[0] != Rread) {
			seterr(c, "bad Rread");
			free(raw);
			clunkfid(c, fid);
			return -1;
		}
		count = g32(rx + 3);
		if (count == 0)
			break;
		if (7 + (int)count > rxn)
			count = (uint32_t)(rxn - 7);
		if (used + (int)count > cap) {
			char* nbuf;
			int ncap = cap == 0 ? 4096 : cap * 2;
			while (ncap < used + (int)count)
				ncap *= 2;
			nbuf = (char*)realloc(raw, (size_t)ncap);
			if (nbuf == NULL) {
				seterr(c, "out of memory");
				free(raw);
				clunkfid(c, fid);
				return -1;
			}
			raw = nbuf;
			cap = ncap;
		}
		memcpy(raw + used, rx + 7, count);
		used += (int)count;
		off += count;
	}
	clunkfid(c, fid);
	pos = 0;
	while (pos + 2 <= used) {
		uint16_t sz = g16((unsigned char*)raw + pos);
		NinePStat st;
		char** nn;
		char* copy;

		if (pos + 2 + sz > used)
			break;
		if (parsestat((unsigned char*)raw + pos, 2 + sz, &st) == 0 &&
		    st.name[0] != '\0') {
			nn = (char**)realloc(list->names,
			                     (size_t)(list->count + 1) * sizeof(char*));
			if (nn == NULL) {
				seterr(c, "out of memory");
				free(raw);
				ninep_list_free(list);
				return -1;
			}
			list->names = nn;
			copy = (char*)malloc(strlen(st.name) + 1);
			if (copy == NULL) {
				seterr(c, "out of memory");
				free(raw);
				ninep_list_free(list);
				return -1;
			}
			strcpy(copy, st.name);
			list->names[list->count++] = copy;
		}
		pos += 2 + sz;
	}
	free(raw);
	c->err[0] = '\0';
	return 0;
}
