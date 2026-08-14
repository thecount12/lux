#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
sendmsg(int fd, unsigned char* body, int n)
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
	return sendmsg(fd, buf, (int)(p - buf));
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
			sendmsg(fd, out, (int)(op - out));
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
			sendmsg(fd, out, (int)(op - out));
			break;
		}
		case Tflush:
			p8(&op, Rflush);
			p16(&op, tag);
			sendmsg(fd, out, (int)(op - out));
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
			sendmsg(fd, out, (int)(op - out));
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
			sendmsg(fd, out, (int)(op - out));
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
			sendmsg(fd, out, (int)(op - out));
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
			sendmsg(fd, out, (int)(op - out));
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
			sendmsg(fd, out, (int)(op - out));
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
			sendmsg(fd, out, (int)(op - out));
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
