/*
 * luxp9 — plan9port helper for Lux Draw / snarf / plumber.
 * Compiled with 9c/9l only. Speaks length-prefixed frames on stdin/stdout.
 *
 * Frame: 4-byte little-endian length, then payload.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <plumb.h>
#include <thread.h>

static int drawopen;
static int resized;
static int resizew;
static int resizeh;
static int plumbsendfd = -1;
static int plumbrecvfd = -1;
static char winsz[64];

void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		threadexits("eresized");
	resized = 1;
	if(screen != nil){
		resizew = Dx(screen->r);
		resizeh = Dy(screen->r);
	}
}

static int
writen(int fd, void *buf, int n)
{
	char *p;
	int put, w;

	p = buf;
	put = 0;
	while(put < n){
		w = write(fd, p+put, n-put);
		if(w <= 0)
			return -1;
		put += w;
	}
	return put;
}

static int
wrframe(int fd, char *p, int n)
{
	uchar hdr[4];

	if(n < 0 || n > 1024*1024)
		return -1;
	hdr[0] = n & 0xff;
	hdr[1] = (n>>8) & 0xff;
	hdr[2] = (n>>16) & 0xff;
	hdr[3] = (n>>24) & 0xff;
	if(writen(fd, hdr, 4) != 4)
		return -1;
	if(n > 0 && writen(fd, p, n) != n)
		return -1;
	return 0;
}

static int
rdframe(int fd, char **out, int *n)
{
	uchar hdr[4];
	int len;
	char *buf;

	if(readn(fd, hdr, 4) != 4)
		return -1;
	len = hdr[0] | (hdr[1]<<8) | (hdr[2]<<16) | (hdr[3]<<24);
	if(len < 0 || len > 1024*1024)
		return -1;
	buf = malloc(len+1);
	if(buf == nil)
		return -1;
	if(len > 0 && readn(fd, buf, len) != len){
		free(buf);
		return -1;
	}
	buf[len] = 0;
	*out = buf;
	*n = len;
	return 0;
}

static Image*
mkcolor(int r, int g, int b)
{
	ulong col;

	col = ((ulong)r<<24) | ((ulong)g<<16) | ((ulong)b<<8) | 0xff;
	return allocimage(display, Rect(0,0,1,1), RGBA32, 1, col);
}

static void
replyok(void)
{
	wrframe(1, "OK", 2);
}

static void
replyoksize(void)
{
	char buf[80];
	int n, ww, hh, fh, fw;

	ww = (screen != nil) ? Dx(screen->r) : 0;
	hh = (screen != nil) ? Dy(screen->r) : 0;
	fh = (font != nil) ? font->height : 16;
	fw = 8;
	if(font != nil){
		fw = stringwidth(font, "m");
		if(fw <= 0)
			fw = font->height / 2;
		if(fw <= 0)
			fw = 8;
	}
	n = snprint(buf, sizeof buf, "OK %d %d %d %d", ww, hh, fh, fw);
	wrframe(1, buf, n);
}

static void
replyerr(char *s)
{
	char buf[256];
	int n;

	n = snprint(buf, sizeof buf, "ERR %s", s);
	wrframe(1, buf, n);
}

static void
cmdopen(char *p)
{
	int w, h;
	char *title;
	int fd;
	char wctl[64];

	if(drawopen){
		replyerr("already open");
		return;
	}
	w = strtol(p, &p, 10);
	h = strtol(p, &p, 10);
	while(*p == ' ')
		p++;
	title = p;
	if(title[0] == 0)
		title = "lux";
	/* plan9port default is 2/3 of the screen unless winsize is set first. */
	if(w > 0 && h > 0){
		snprint(winsz, sizeof winsz, "%dx%d", w, h);
		winsize = winsz;
	}
	if(initdraw(nil, nil, title) < 0){
		replyerr("initdraw");
		return;
	}
	einit(Emouse|Ekeyboard);
	if(w > 0 && h > 0){
		fd = open("/dev/wctl", OWRITE);
		if(fd >= 0){
			snprint(wctl, sizeof wctl, "resize -dx %d -dy %d", w, h);
			write(fd, wctl, strlen(wctl));
			close(fd);
			getwindow(display, Refnone);
		}
	}
	if(screen != nil)
		draw(screen, screen->r, display->black, nil, ZP);
	drawopen = 1;
	replyoksize();
}

static void
cmdfill(char *p)
{
	int x, y, w, h, r, g, b;
	Point min;
	Image *im;
	Rectangle rr;

	if(!drawopen){
		replyerr("no window");
		return;
	}
	x = strtol(p, &p, 10);
	y = strtol(p, &p, 10);
	w = strtol(p, &p, 10);
	h = strtol(p, &p, 10);
	r = strtol(p, &p, 10);
	g = strtol(p, &p, 10);
	b = strtol(p, &p, 10);
	im = mkcolor(r, g, b);
	if(im == nil){
		replyerr("allocimage");
		return;
	}
	min = screen->r.min;
	rr = Rect(min.x+x, min.y+y, min.x+x+w, min.y+y+h);
	draw(screen, rr, im, nil, ZP);
	freeimage(im);
	replyok();
}

static void
cmdstring(char *p, int n)
{
	int x, y, r, g, b;
	char *nl;
	Image *im;
	Point pt;

	if(!drawopen){
		replyerr("no window");
		return;
	}
	x = strtol(p, &p, 10);
	y = strtol(p, &p, 10);
	r = strtol(p, &p, 10);
	g = strtol(p, &p, 10);
	b = strtol(p, &p, 10);
	nl = strchr(p, '\n');
	if(nl == nil){
		replyerr("string");
		return;
	}
	nl++;
	im = mkcolor(r, g, b);
	if(im == nil){
		replyerr("allocimage");
		return;
	}
	pt = addpt(screen->r.min, Pt(x, y));
	if(font != nil)
		pt.y += font->ascent;
	string(screen, pt, im, ZP, font, nl);
	USED(n);
	freeimage(im);
	replyok();
}

static void
cmdflush(void)
{
	if(!drawopen){
		replyerr("no window");
		return;
	}
	flushimage(display, 1);
	replyok();
}

static void
cmdevent(void)
{
	Event ev;
	char buf[64];
	int n, kind;
	Point xy;

	if(!drawopen){
		wrframe(1, "QUIT", 4);
		return;
	}
	if(resized){
		resized = 0;
		n = snprint(buf, sizeof buf, "RESIZE %d %d", resizew, resizeh);
		wrframe(1, buf, n);
		return;
	}
	kind = eread(Emouse|Ekeyboard, &ev);
	if(kind == Emouse){
		xy = subpt(ev.mouse.xy, screen->r.min);
		n = snprint(buf, sizeof buf, "MOUSE %d %d %d", xy.x, xy.y, ev.mouse.buttons);
		wrframe(1, buf, n);
		return;
	}
	if(kind == Ekeyboard){
		n = snprint(buf, sizeof buf, "KBD %d", ev.kbdc);
		wrframe(1, buf, n);
		return;
	}
	wrframe(1, "QUIT", 4);
}

static void
cmdsnarfget(void)
{
	char *s;
	char *out;
	int n;

	if(!drawopen){
		replyerr("no window");
		return;
	}
	s = getsnarf();
	if(s == nil)
		s = strdup("");
	n = strlen(s);
	out = malloc(n+6);
	if(out == nil){
		free(s);
		replyerr("mem");
		return;
	}
	memcpy(out, "DATA\n", 5);
	memcpy(out+5, s, n);
	wrframe(1, out, n+5);
	free(out);
	free(s);
}

static void
cmdsnarfput(char *p, int n)
{
	if(!drawopen){
		replyerr("no window");
		return;
	}
	if(n >= 9 && memcmp(p, "SNARFPUT\n", 9) == 0)
		p += 9;
	putsnarf(p);
	replyok();
}

static void
cmdplumb(char *p, int n)
{
	int dstn, wdirn, datan;
	char *nl, *dst, *wdir, *data, *q;
	char *ds, *ws, *dat;
	Plumbmsg m;

	USED(n);
	if(strncmp(p, "PLUMB ", 6) != 0){
		replyerr("plumb");
		return;
	}
	p += 6;
	dstn = strtol(p, &p, 10);
	wdirn = strtol(p, &p, 10);
	datan = strtol(p, &p, 10);
	if(dstn < 0 || wdirn < 0 || datan < 0){
		replyerr("plumb");
		return;
	}
	nl = strchr(p, '\n');
	if(nl == nil){
		replyerr("plumb");
		return;
	}
	q = nl+1;
	dst = q; q += dstn;
	wdir = q; q += wdirn;
	data = q;
	ds = malloc(dstn+1);
	ws = malloc(wdirn+1);
	dat = malloc(datan+1);
	if(ds == nil || ws == nil || dat == nil){
		free(ds); free(ws); free(dat);
		replyerr("mem");
		return;
	}
	memcpy(ds, dst, dstn); ds[dstn] = 0;
	memcpy(ws, wdir, wdirn); ws[wdirn] = 0;
	memcpy(dat, data, datan); dat[datan] = 0;
	if(plumbsendfd < 0)
		plumbsendfd = plumbopen("send", OWRITE);
	if(plumbsendfd < 0){
		free(ds); free(ws); free(dat);
		replyerr("plumbopen");
		return;
	}
	memset(&m, 0, sizeof m);
	m.src = "lux";
	m.dst = ds;
	m.wdir = ws;
	m.type = "text";
	m.ndata = datan;
	m.data = dat;
	if(plumbsend(plumbsendfd, &m) < 0){
		free(ds); free(ws); free(dat);
		replyerr("plumbsend");
		return;
	}
	free(ds); free(ws); free(dat);
	replyok();
}

static void
cmdplumbrecv(void)
{
	Plumbmsg *m;
	char hdr[80];
	char *out;
	int hn, total, srcn, dstn, wdirn, typen, datan;
	char *p;

	if(plumbrecvfd < 0)
		plumbrecvfd = plumbopen("lux", OREAD);
	if(plumbrecvfd < 0)
		plumbrecvfd = plumbopen("edit", OREAD);
	if(plumbrecvfd < 0){
		replyerr("plumbopen");
		return;
	}
	m = plumbrecv(plumbrecvfd);
	if(m == nil){
		replyerr("plumbrecv");
		return;
	}
	srcn = m->src ? strlen(m->src) : 0;
	dstn = m->dst ? strlen(m->dst) : 0;
	wdirn = m->wdir ? strlen(m->wdir) : 0;
	typen = m->type ? strlen(m->type) : 0;
	datan = m->ndata;
	hn = snprint(hdr, sizeof hdr, "PLUMB %d %d %d %d %d\n", srcn, dstn, wdirn, typen, datan);
	total = hn + srcn + dstn + wdirn + typen + datan;
	out = malloc(total);
	if(out == nil){
		plumbfree(m);
		replyerr("mem");
		return;
	}
	p = out;
	memcpy(p, hdr, hn); p += hn;
	if(srcn){ memcpy(p, m->src, srcn); p += srcn; }
	if(dstn){ memcpy(p, m->dst, dstn); p += dstn; }
	if(wdirn){ memcpy(p, m->wdir, wdirn); p += wdirn; }
	if(typen){ memcpy(p, m->type, typen); p += typen; }
	if(datan && m->data){ memcpy(p, m->data, datan); }
	wrframe(1, out, total);
	free(out);
	plumbfree(m);
}

static void
cmdclose(void)
{
	if(drawopen){
		closedisplay(display);
		drawopen = 0;
	}
	replyok();
}

void
threadmain(int argc, char **argv)
{
	char *req;
	int n;

	USED(argc);
	USED(argv);
	for(;;){
		if(rdframe(0, &req, &n) < 0)
			threadexits(nil);
		if(n >= 8 && memcmp(req, "SHUTDOWN", 8) == 0){
			free(req);
			threadexits(nil);
		}
		if(n >= 5 && memcmp(req, "OPEN ", 5) == 0)
			cmdopen(req+5);
		else if(n >= 5 && memcmp(req, "FILL ", 5) == 0)
			cmdfill(req+5);
		else if(n >= 7 && memcmp(req, "STRING ", 7) == 0)
			cmdstring(req+7, n-7);
		else if(n >= 5 && memcmp(req, "FLUSH", 5) == 0)
			cmdflush();
		else if(n >= 5 && memcmp(req, "EVENT", 5) == 0)
			cmdevent();
		else if(n >= 8 && memcmp(req, "SNARFGET", 8) == 0)
			cmdsnarfget();
		else if(n >= 8 && memcmp(req, "SNARFPUT", 8) == 0)
			cmdsnarfput(req, n);
		else if(n >= 6 && memcmp(req, "PLUMB ", 6) == 0)
			cmdplumb(req, n);
		else if(n >= 9 && memcmp(req, "PLUMBRECV", 9) == 0)
			cmdplumbrecv();
		else if(n >= 5 && memcmp(req, "CLOSE", 5) == 0)
			cmdclose();
		else
			replyerr("unknown");
		free(req);
	}
}
