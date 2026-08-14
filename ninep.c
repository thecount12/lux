#include <u.h>
#include <libc.h>
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
	QTFILE = 0x00
};

#define DMDIR ((ulong)0x80000000)
#define MAXFID 128

typedef struct {
	int inuse;
	ulong fid;
	int fileidx; /* -1 = root */
	int opened;
	char* cached;
	int cachedlen;
} Fid;

static ulong
g32(const uchar* p)
{
	return (ulong)p[0] | ((ulong)p[1] << 8) |
	       ((ulong)p[2] << 16) | ((ulong)p[3] << 24);
}

static ushort
g16(const uchar* p)
{
	return (ushort)p[0] | ((ushort)p[1] << 8);
}

static uvlong
g64(const uchar* p)
{
	return (uvlong)g32(p) | ((uvlong)g32(p + 4) << 32);
}

static void
p8(uchar** pp, unsigned int v)
{
	*(*pp)++ = (uchar)v;
}

static void
p16(uchar** pp, unsigned int v)
{
	p8(pp, v & 0xff);
	p8(pp, (v >> 8) & 0xff);
}

static void
p32(uchar** pp, ulong v)
{
	p8(pp, v & 0xff);
	p8(pp, (v >> 8) & 0xff);
	p8(pp, (v >> 16) & 0xff);
	p8(pp, (v >> 24) & 0xff);
}

static void
p64(uchar** pp, uvlong v)
{
	p32(pp, (ulong)v);
	p32(pp, (ulong)(v >> 32));
}

static void
pstr(uchar** pp, char* s)
{
	int n = s ? (int)strlen(s) : 0;
	p16(pp, (unsigned int)n);
	if (n > 0) {
		memcpy(*pp, s, (ulong)n);
		*pp += n;
	}
}

static void
pqid(uchar** pp, uchar type, ulong vers, uvlong path)
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
		int r = (int)read(fd, p + got, (ulong)(n - got));
		if (r <= 0)
			return -1;
		got += r;
	}
	return got;
}

static int
writen(int fd, void* buf, int n)
{
	char* p = (char*)buf;
	int put = 0;
	while (put < n) {
		int w = (int)write(fd, p + put, (ulong)(n - put));
		if (w <= 0)
			return -1;
		put += w;
	}
	return put;
}

static Fid*
fidfind(Fid* fids, ulong fid)
{
	int i;
	for (i = 0; i < MAXFID; i++) {
		if (fids[i].inuse && fids[i].fid == fid)
			return &fids[i];
	}
	return nil;
}

static Fid*
fidalloc(Fid* fids, ulong fid)
{
	int i;
	Fid* f = fidfind(fids, fid);
	if (f != nil)
		return nil;
	for (i = 0; i < MAXFID; i++) {
		if (!fids[i].inuse) {
			memset(&fids[i], 0, sizeof(fids[i]));
			fids[i].inuse = 1;
			fids[i].fid = fid;
			fids[i].fileidx = -1;
			return &fids[i];
		}
	}
	return nil;
}

static void
fidfree(Fid* f)
{
	if (f == nil)
		return;
	free(f->cached);
	memset(f, 0, sizeof(*f));
}

static NinePFile*
filebyname(NinePFile* files, int nfiles, char* name, int nlen)
{
	int i;
	for (i = 0; i < nfiles; i++) {
		if ((int)strlen(files[i].name) == nlen &&
		    memcmp(files[i].name, name, (ulong)nlen) == 0)
			return &files[i];
	}
	return nil;
}

static int
fileindex(NinePFile* files, int nfiles, NinePFile* f)
{
	int i;
	if (f == nil)
		return -1;
	for (i = 0; i < nfiles; i++) {
		if (&files[i] == f)
			return i;
	}
	return -1;
}

static ulong
nowsec(void)
{
	return (ulong)time(nil);
}

static void
packstat(uchar** pp, int isdir, int idx, char* name,
         uvlong length, int modebits)
{
	uchar* sizep;
	uchar* start;
	ulong mode;
	ulong t = nowsec();

	sizep = *pp;
	p16(pp, 0);
	start = *pp;
	p16(pp, 0); /* type */
	p32(pp, 0); /* dev */
	if (isdir)
		pqid(pp, QTDIR, 0, 0);
	else
		pqid(pp, QTFILE, 0, (uvlong)(idx + 1));
	mode = (ulong)modebits;
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
		sizep[0] = (uchar)(sz & 0xff);
		sizep[1] = (uchar)((sz >> 8) & 0xff);
	}
}

static int
sendmsg(int fd, uchar* body, int n)
{
	uchar hdr[4];
	int size = n + 4;
	hdr[0] = (uchar)(size & 0xff);
	hdr[1] = (uchar)((size >> 8) & 0xff);
	hdr[2] = (uchar)((size >> 16) & 0xff);
	hdr[3] = (uchar)((size >> 24) & 0xff);
	if (writen(fd, hdr, 4) != 4)
		return -1;
	if (n > 0 && writen(fd, body, n) != n)
		return -1;
	return 0;
}

static int
rerror(int fd, ushort tag, char* ename)
{
	uchar buf[256];
	uchar* p = buf;
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

	USED(nfiles);
	if (f->fileidx < 0)
		return 0;
	if (f->cached != nil)
		return 0;
	if (ops == nil || ops->onread == nil)
		return -1;
	file = &files[f->fileidx];
	data = nil;
	len = 0;
	if (ops->onread(file->aux, &data, &len) < 0)
		return -1;
	f->cached = data;
	f->cachedlen = len;
	return 0;
}

static int
dirdata(NinePFile* files, int nfiles, uvlong off, ulong count,
        uchar* out, int* outn)
{
	uchar tmp[NINEP_MSIZE];
	uchar* p = tmp;
	int i, total;

	for (i = 0; i < nfiles; i++)
		packstat(&p, 0, i, files[i].name, 0,
		         files[i].writable ? 0666 : 0444);
	total = (int)(p - tmp);
	if (off >= (uvlong)total) {
		*outn = 0;
		return 0;
	}
	if (off + count > (uvlong)total)
		count = (ulong)(total - (int)off);
	memcpy(out, tmp + (ulong)off, count);
	*outn = (int)count;
	return 0;
}

int
ninep_serve(int fd, NinePFile* files, int nfiles, NinePOps* ops)
{
	Fid fids[MAXFID];
	uchar* msg;
	ulong msize = NINEP_MSIZE;

	if (nfiles < 0 || nfiles > NINEP_MAXFILES)
		return -1;
	memset(fids, 0, sizeof(fids));

	for (;;) {
		uchar szb[4];
		ulong size;
		uchar* p;
		uchar* end;
		uchar type;
		ushort tag;
		uchar out[NINEP_MSIZE];
		uchar* op;

		if (readn(fd, szb, 4) != 4)
			break;
		size = g32(szb);
		if (size < 7 || size > msize)
			break;
		msg = (uchar*)malloc(size - 4);
		if (msg == nil)
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
			ulong cm;
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
			ulong fid;
			Fid* f;
			if (p + 4 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p);
			f = fidalloc(fids, fid);
			if (f == nil) {
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
			ulong fid, newfid;
			ushort nwname, i;
			Fid* f;
			Fid* nf;
			int idx;
			uchar* qidp;
			ushort nwqid = 0;

			if (p + 10 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p); p += 4;
			newfid = g32(p); p += 4;
			nwname = g16(p); p += 2;
			f = fidfind(fids, fid);
			if (f == nil) {
				rerror(fd, tag, "unknown fid");
				break;
			}
			if (fid == newfid)
				nf = f;
			else {
				nf = fidalloc(fids, newfid);
				if (nf == nil) {
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
				ushort nlen;
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
				if (file == nil)
					break;
				idx = fileindex(files, nfiles, file);
				pqid(&op, QTFILE, 0, (uvlong)(idx + 1));
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
			qidp[0] = (uchar)(nwqid & 0xff);
			qidp[1] = (uchar)((nwqid >> 8) & 0xff);
			sendmsg(fd, out, (int)(op - out));
			break;
		}
		case Topen: {
			ulong fid;
			uchar mode;
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
			if (f == nil) {
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
				f->cached = nil;
				f->cachedlen = 0;
			}
			f->opened = 1;
			p8(&op, Ropen);
			p16(&op, tag);
			if (isdir)
				pqid(&op, QTDIR, 0, 0);
			else
				pqid(&op, QTFILE, 0, (uvlong)(f->fileidx + 1));
			p32(&op, 0); /* iounit */
			sendmsg(fd, out, (int)(op - out));
			break;
		}
		case Tread: {
			ulong fid, count;
			uvlong off;
			Fid* f;
			int nout = 0;
			uchar data[NINEP_MSIZE];

			if (p + 16 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p); p += 4;
			off = g64(p); p += 8;
			count = g32(p);
			f = fidfind(fids, fid);
			if (f == nil || !f->opened) {
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
				if (off >= (uvlong)f->cachedlen)
					nout = 0;
				else {
					if (off + count > (uvlong)f->cachedlen)
						count = (ulong)(f->cachedlen - (int)off);
					memcpy(data, f->cached + (ulong)off, count);
					nout = (int)count;
				}
			}
			p8(&op, Rread);
			p16(&op, tag);
			p32(&op, (ulong)nout);
			memcpy(op, data, (ulong)nout);
			op += nout;
			sendmsg(fd, out, (int)(op - out));
			break;
		}
		case Twrite: {
			ulong fid, count;
			uvlong off;
			Fid* f;
			NinePFile* file;

			if (p + 16 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p); p += 4;
			off = g64(p); p += 8;
			count = g32(p); p += 4;
			USED(off);
			f = fidfind(fids, fid);
			if (f == nil || f->fileidx < 0 || !f->opened) {
				rerror(fd, tag, "bad fid");
				break;
			}
			file = &files[f->fileidx];
			if (!file->writable || ops == nil || ops->onwrite == nil) {
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
			f->cached = nil;
			f->cachedlen = 0;
			p8(&op, Rwrite);
			p16(&op, tag);
			p32(&op, count);
			sendmsg(fd, out, (int)(op - out));
			break;
		}
		case Tclunk: {
			ulong fid;
			Fid* f;
			if (p + 4 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p);
			f = fidfind(fids, fid);
			if (f == nil) {
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
			ulong fid;
			Fid* f;
			uchar* sizep;
			uchar* statstart;
			char* name;
			int isdir;
			int modebits;
			uvlong length = 0;

			if (p + 4 > end) {
				rerror(fd, tag, "bad conv");
				break;
			}
			fid = g32(p);
			f = fidfind(fids, fid);
			if (f == nil) {
				rerror(fd, tag, "unknown fid");
				break;
			}
			isdir = (f->fileidx < 0);
			name = isdir ? "/" : files[f->fileidx].name;
			modebits = isdir ? 0555 :
				(files[f->fileidx].writable ? 0666 : 0444);
			if (!isdir && f->cached != nil)
				length = (uvlong)f->cachedlen;
			p8(&op, Rstat);
			p16(&op, tag);
			sizep = op;
			p16(&op, 0);
			statstart = op;
			packstat(&op, isdir, f->fileidx, name, length, modebits);
			{
				int sz = (int)(op - statstart);
				sizep[0] = (uchar)(sz & 0xff);
				sizep[1] = (uchar)((sz >> 8) & 0xff);
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
