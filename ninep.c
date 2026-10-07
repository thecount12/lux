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
p8(uchar** pp, uint v)
{
	*(*pp)++ = (uchar)v;
}

static void
p16(uchar** pp, uint v)
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
	p16(pp, (uint)n);
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
			if (!isdir && files[f->fileidx].readable)
				ensurecache(f, files, nfiles, ops);
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

enum {
	OREAD9 = 0,
	OWRITE9 = 1,
	NINEP_MAXREAD = 8 * 1024 * 1024,
	NINEP_MAXWALK = 16
};

#define NOFID (~0UL)

struct NinePClient {
	int fd;
	ulong msize;
	ushort tag;
	ulong nextfid;
	char err[NINEP_ERRMAX];
};

static int
istcp(char *s)
{
	return s != nil && strncmp(s, "tcp!", 4) == 0;
}

int
ninep_listen(char *addr, NinePListener *lis)
{
	int acfd;

	if (lis == nil || addr == nil)
		return -1;
	memset(lis, 0, sizeof(*lis));
	lis->fd = -1;
	if (!istcp(addr))
		return -1;
	acfd = announce(addr, lis->adir);
	if (acfd < 0)
		return -1;
	lis->fd = acfd;
	lis->tcp = 1;
	return 0;
}

int
ninep_accept(NinePListener *lis)
{
	char ldir[40];
	int lcfd, dfd;

	if (lis == nil || lis->fd < 0)
		return -1;
	lcfd = listen(lis->adir, ldir);
	if (lcfd < 0)
		return -1;
	dfd = accept(lcfd, ldir);
	close(lcfd);
	return dfd;
}

void
ninep_unlisten(NinePListener *lis)
{
	if (lis == nil)
		return;
	if (lis->fd >= 0) {
		close(lis->fd);
		lis->fd = -1;
	}
}

int
ninep_dial(char *addr)
{
	int fd;

	if (addr == nil || addr[0] == 0)
		return -1;
	if (istcp(addr))
		return dial(addr, nil, nil, nil);
	fd = open(addr, ORDWR);
	return fd;
}

static void
seterr(NinePClient *c, char *s)
{
	int n;

	if (c == nil)
		return;
	if (s == nil)
		s = "9p error";
	n = strlen(s);
	if (n >= NINEP_ERRMAX)
		n = NINEP_ERRMAX - 1;
	memcpy(c->err, s, n);
	c->err[n] = 0;
}

char *
ninep_client_err(NinePClient *c)
{
	if (c == nil || c->err[0] == 0)
		return "9p error";
	return c->err;
}

void
ninep_list_free(NinePList *list)
{
	int i;

	if (list == nil)
		return;
	if (list->names != nil) {
		for (i = 0; i < list->count; i++)
			free(list->names[i]);
		free(list->names);
	}
	list->names = nil;
	list->count = 0;
}

void
ninep_client_close(NinePClient *c)
{
	if (c == nil)
		return;
	if (c->fd >= 0)
		close(c->fd);
	free(c);
}

static ushort
nexttag(NinePClient *c)
{
	ushort t;

	t = c->tag++;
	if (c->tag == 0 || c->tag == 0xffff)
		c->tag = 1;
	return t;
}

static ulong
nextfid(NinePClient *c)
{
	ulong f;

	f = c->nextfid++;
	if (c->nextfid == 0)
		c->nextfid = 1;
	return f;
}

static int
rpc(NinePClient *c, uchar *tx, int txn, uchar *rx, int rxmax, int *rxn)
{
	uchar hdr[4];
	ulong size;
	ushort nlen;

	if (c == nil || c->fd < 0)
		return -1;
	size = (ulong)(txn + 4);
	hdr[0] = size & 0xff;
	hdr[1] = (size >> 8) & 0xff;
	hdr[2] = (size >> 16) & 0xff;
	hdr[3] = (size >> 24) & 0xff;
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
			nlen = (ushort)(*rxn - 5);
		if (nlen >= NINEP_ERRMAX)
			nlen = NINEP_ERRMAX - 1;
		memcpy(c->err, rx + 5, nlen);
		c->err[nlen] = 0;
		return -1;
	}
	return 0;
}

static int
gstr(uchar *p, uchar *end, char *out, int outmax)
{
	ushort n;

	if (p + 2 > end)
		return -1;
	n = g16(p);
	p += 2;
	if (p + n > end)
		return -1;
	if ((int)n >= outmax)
		return -1;
	memcpy(out, p, n);
	out[n] = 0;
	return 2 + (int)n;
}

static int
parsestat(uchar *p, int n, NinePStat *st)
{
	uchar *end;
	ushort sz;
	uchar qtype;
	int k;

	if (st == nil || n < 2)
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
splitpath(char *path, char names[][256], int max)
{
	int n, len;
	char *p, *start;

	n = 0;
	if (path == nil)
		return 0;
	p = path;
	while (*p == '/')
		p++;
	while (*p != 0 && n < max) {
		start = p;
		while (*p != 0 && *p != '/')
			p++;
		len = (int)(p - start);
		if (len == 0)
			break;
		if (len >= 256)
			return -1;
		memcpy(names[n], start, len);
		names[n][len] = 0;
		n++;
		while (*p == '/')
			p++;
	}
	if (*p != 0)
		return -1;
	return n;
}

static int
clunkfid(NinePClient *c, ulong fid)
{
	uchar tx[16], rx[64], *p;
	int rxn;

	p = tx;
	p8(&p, Tclunk);
	p16(&p, nexttag(c));
	p32(&p, fid);
	return rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn);
}

static int
walkto(NinePClient *c, char *path, ulong *outfid)
{
	char names[NINEP_MAXWALK][256];
	int nw, i, rxn;
	ulong newfid;
	uchar tx[NINEP_MSIZE], rx[NINEP_MSIZE], *p;
	ushort nwqid;

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
	p16(&p, (uint)nw);
	for (i = 0; i < nw; i++)
		pstr(&p, names[i]);
	if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0)
		return -1;
	if (rxn < 5 || rx[0] != Rwalk) {
		seterr(c, "bad Rwalk");
		return -1;
	}
	nwqid = g16(rx + 3);
	if (nwqid != (ushort)nw) {
		seterr(c, "file not found");
		clunkfid(c, newfid);
		return -1;
	}
	*outfid = newfid;
	return 0;
}

static int
openfid(NinePClient *c, ulong fid, int mode, int *isdir)
{
	uchar tx[16], rx[64], *p;
	int rxn;

	p = tx;
	p8(&p, Topen);
	p16(&p, nexttag(c));
	p32(&p, fid);
	p8(&p, (uint)mode);
	if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0)
		return -1;
	if (rxn < 16 || rx[0] != Ropen) {
		seterr(c, "bad Ropen");
		return -1;
	}
	if (isdir != nil)
		*isdir = (rx[3] & 0x80) ? 1 : 0;
	return 0;
}

NinePClient *
ninep_client_attach(int fd, char *uname, char *aname)
{
	NinePClient *c;
	uchar tx[256], rx[NINEP_MSIZE], *p;
	int rxn;
	ulong msize;

	if (fd < 0)
		return nil;
	c = malloc(sizeof(*c));
	if (c == nil) {
		close(fd);
		return nil;
	}
	memset(c, 0, sizeof(*c));
	c->fd = fd;
	c->msize = NINEP_MSIZE;
	c->tag = 1;
	c->nextfid = 1;
	if (uname == nil || uname[0] == 0)
		uname = "none";
	if (aname == nil)
		aname = "";

	p = tx;
	p8(&p, Tversion);
	p16(&p, 0xffff);
	p32(&p, NINEP_MSIZE);
	pstr(&p, "9P2000");
	if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0) {
		ninep_client_close(c);
		return nil;
	}
	if (rxn < 9 || rx[0] != Rversion) {
		seterr(c, "bad Rversion");
		ninep_client_close(c);
		return nil;
	}
	msize = g32(rx + 3);
	if (msize < 256) {
		seterr(c, "msize");
		ninep_client_close(c);
		return nil;
	}
	if (msize < c->msize)
		c->msize = msize;

	p = tx;
	p8(&p, Tattach);
	p16(&p, nexttag(c));
	p32(&p, 0);
	p32(&p, NOFID);
	pstr(&p, uname);
	pstr(&p, aname);
	if (rpc(c, tx, (int)(p - tx), rx, (int)sizeof(rx), &rxn) < 0) {
		ninep_client_close(c);
		return nil;
	}
	if (rxn < 16 || rx[0] != Rattach) {
		seterr(c, "bad Rattach");
		ninep_client_close(c);
		return nil;
	}
	c->err[0] = 0;
	return c;
}

int
ninep_client_read(NinePClient *c, char *path, char **data, int *len)
{
	ulong fid;
	int isdir;
	uvlong off;
	char *buf;
	int cap, used;

	isdir = 0;
	off = 0;
	buf = nil;
	cap = 0;
	used = 0;
	if (data == nil || len == nil) {
		seterr(c, "bad args");
		return -1;
	}
	*data = nil;
	*len = 0;
	if (walkto(c, path, &fid) < 0)
		return -1;
	if (openfid(c, fid, OREAD9, &isdir) < 0) {
		clunkfid(c, fid);
		return -1;
	}
	if (isdir) {
		seterr(c, "is a directory");
		clunkfid(c, fid);
		return -1;
	}
	for (;;) {
		uchar tx[32], rx[NINEP_MSIZE], *p;
		int rxn;
		ulong count, want;

		want = c->msize - 32;
		p = tx;
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
			count = (ulong)(rxn - 7);
		if (count == 0)
			break;
		if (used + (int)count > NINEP_MAXREAD) {
			seterr(c, "file too large");
			free(buf);
			clunkfid(c, fid);
			return -1;
		}
		if (used + (int)count + 1 > cap) {
			char *nbuf;
			int ncap;

			ncap = cap == 0 ? 4096 : cap * 2;
			while (ncap < used + (int)count + 1)
				ncap *= 2;
			nbuf = realloc(buf, ncap);
			if (nbuf == nil) {
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
	if (buf == nil) {
		buf = malloc(1);
		if (buf == nil) {
			seterr(c, "out of memory");
			return -1;
		}
	}
	buf[used] = 0;
	*data = buf;
	*len = used;
	c->err[0] = 0;
	return 0;
}

int
ninep_client_write(NinePClient *c, char *path, char *data, int len)
{
	ulong fid;
	int isdir, sent;
	uvlong off;

	isdir = 0;
	sent = 0;
	off = 0;
	if (data == nil)
		data = "";
	if (len < 0)
		len = strlen(data);
	if (walkto(c, path, &fid) < 0)
		return -1;
	if (openfid(c, fid, OWRITE9, &isdir) < 0) {
		clunkfid(c, fid);
		return -1;
	}
	if (isdir) {
		seterr(c, "is a directory");
		clunkfid(c, fid);
		return -1;
	}
	while (sent < len) {
		uchar tx[NINEP_MSIZE], rx[32], *p;
		int rxn;
		ulong chunk, got;

		chunk = (ulong)(len - sent);
		if (chunk > c->msize - 32)
			chunk = c->msize - 32;
		p = tx;
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
	c->err[0] = 0;
	return sent;
}

int
ninep_client_stat(NinePClient *c, char *path, NinePStat *st)
{
	ulong fid;
	uchar tx[16], rx[NINEP_MSIZE], *p;
	int rxn;
	ushort nstat;

	if (st == nil) {
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
	c->err[0] = 0;
	return 0;
}

int
ninep_client_ls(NinePClient *c, char *path, NinePList *list)
{
	ulong fid;
	int isdir, used, cap, pos;
	uvlong off;
	char *raw;

	isdir = 0;
	off = 0;
	raw = nil;
	used = 0;
	cap = 0;
	if (list == nil) {
		seterr(c, "bad args");
		return -1;
	}
	list->names = nil;
	list->count = 0;
	if (walkto(c, path, &fid) < 0)
		return -1;
	if (openfid(c, fid, OREAD9, &isdir) < 0) {
		clunkfid(c, fid);
		return -1;
	}
	if (!isdir) {
		seterr(c, "not a directory");
		clunkfid(c, fid);
		return -1;
	}
	for (;;) {
		uchar tx[32], rx[NINEP_MSIZE], *p;
		int rxn;
		ulong count;

		p = tx;
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
			count = (ulong)(rxn - 7);
		if (used + (int)count > cap) {
			char *nbuf;
			int ncap;

			ncap = cap == 0 ? 4096 : cap * 2;
			while (ncap < used + (int)count)
				ncap *= 2;
			nbuf = realloc(raw, ncap);
			if (nbuf == nil) {
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
		ushort sz;
		NinePStat st;
		char **nn, *copy;

		sz = g16((uchar*)raw + pos);
		if (pos + 2 + sz > used)
			break;
		if (parsestat((uchar*)raw + pos, 2 + sz, &st) == 0 && st.name[0] != 0) {
			nn = realloc(list->names, (list->count + 1) * sizeof(char*));
			if (nn == nil) {
				seterr(c, "out of memory");
				free(raw);
				ninep_list_free(list);
				return -1;
			}
			list->names = nn;
			copy = malloc(strlen(st.name) + 1);
			if (copy == nil) {
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
	c->err[0] = 0;
	return 0;
}
