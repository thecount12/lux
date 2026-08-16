#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include "luxdraw.h"

static int windowopen;
static int resized;
static int resizew;
static int resizeh;

void
eresized(int new)
{
	if(new && display != nil && getwindow(display, Refnone) < 0)
		windowopen = 0;
	resized = 1;
	if(screen != nil){
		resizew = Dx(screen->r);
		resizeh = Dy(screen->r);
	}
}

static Image*
mkcolor(int r, int g, int b)
{
	ulong col;

	col = ((ulong)r<<24) | ((ulong)g<<16) | ((ulong)b<<8) | 0xff;
	return allocimage(display, Rect(0,0,1,1), RGBA32, 1, col);
}

int
luxdraw_available(void)
{
	return 1;
}

int
luxdraw_is_open(void)
{
	return windowopen;
}

int
luxsnarf_get(char **out, int *len)
{
	int fd, n, r, cap;
	char *buf;

	fd = open("/dev/snarf", OREAD);
	if(fd < 0)
		return -1;
	cap = 256;
	n = 0;
	buf = malloc(cap);
	if(buf == nil){
		close(fd);
		return -1;
	}
	for(;;){
		if(n >= cap-1){
			char *nb;
			cap *= 2;
			nb = realloc(buf, cap);
			if(nb == nil){
				free(buf);
				close(fd);
				return -1;
			}
			buf = nb;
		}
		r = read(fd, buf+n, cap-n-1);
		if(r <= 0)
			break;
		n += r;
	}
	close(fd);
	buf[n] = 0;
	*out = buf;
	*len = n;
	return 0;
}

int
luxsnarf_put(char *s, int n)
{
	int fd;

	fd = open("/dev/snarf", OWRITE);
	if(fd < 0)
		return -1;
	if(write(fd, s, n) != n){
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int
luxdraw_open(char *title, int w, int h)
{
	int fd;
	char wctl[64];

	if(windowopen)
		return -1;
	if(title == nil)
		title = "lux";
	if(initdraw(nil, nil, title) < 0)
		return -1;
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
	windowopen = 1;
	return 0;
}

int
luxdraw_info(int *w, int *h, int *fh, int *fw)
{
	if(!windowopen || screen == nil)
		return -1;
	if(w != nil)
		*w = Dx(screen->r);
	if(h != nil)
		*h = Dy(screen->r);
	if(fh != nil)
		*fh = (font != nil) ? font->height : 16;
	if(fw != nil){
		int n;

		n = 8;
		if(font != nil){
			n = stringwidth(font, "m");
			if(n <= 0)
				n = font->height / 2;
			if(n <= 0)
				n = 8;
		}
		*fw = n;
	}
	return 0;
}

int
luxdraw_fill(int x, int y, int w, int h, int r, int g, int b)
{
	Image *im;
	Point min;
	Rectangle rr;

	if(!windowopen)
		return -1;
	im = mkcolor(r, g, b);
	if(im == nil)
		return -1;
	min = screen->r.min;
	rr = Rect(min.x+x, min.y+y, min.x+x+w, min.y+y+h);
	draw(screen, rr, im, nil, ZP);
	freeimage(im);
	return 0;
}

int
luxdraw_string(int x, int y, char *s, int r, int g, int b)
{
	Image *im;
	Point pt;

	if(!windowopen || s == nil)
		return -1;
	im = mkcolor(r, g, b);
	if(im == nil)
		return -1;
	pt = addpt(screen->r.min, Pt(x, y));
	if(font != nil)
		pt.y += font->ascent;
	string(screen, pt, im, ZP, font, s);
	freeimage(im);
	return 0;
}

int
luxdraw_flush(void)
{
	if(!windowopen)
		return -1;
	flushimage(display, 1);
	return 0;
}

int
luxdraw_event(LuxDrawEvent *e)
{
	Event ev;
	int kind;
	Point xy;

	if(!windowopen || e == nil)
		return -1;
	memset(e, 0, sizeof *e);
	if(resized){
		resized = 0;
		e->kind = 2;
		e->x = resizew;
		e->y = resizeh;
		return 0;
	}
	kind = eread(Emouse|Ekeyboard, &ev);
	if(kind == Emouse){
		xy = subpt(ev.mouse.xy, screen->r.min);
		e->kind = 0;
		e->x = xy.x;
		e->y = xy.y;
		e->button = ev.mouse.buttons;
		return 0;
	}
	if(kind == Ekeyboard){
		e->kind = 1;
		e->r = ev.kbdc;
		return 0;
	}
	e->kind = 3;
	windowopen = 0;
	return 0;
}

int
luxdraw_close(void)
{
	if(windowopen){
		closedisplay(display);
		windowopen = 0;
	}
	return 0;
}

int
luxplumb_send(char *dst, char *data, char *wdir)
{
	int fd, n;
	char hdr[256];

	if(dst == nil || data == nil)
		return -1;
	if(wdir == nil)
		wdir = ".";
	fd = open("/mnt/plumb/send", OWRITE);
	if(fd < 0)
		return -1;
	n = strlen(data);
	snprint(hdr, sizeof hdr, "lux\n%s\n%s\ntext\n\n%d\n", dst, wdir, n);
	if(write(fd, hdr, strlen(hdr)) != strlen(hdr) || write(fd, data, n) != n){
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static char*
dupn(char *s, int n)
{
	char *d;

	d = malloc(n+1);
	if(d == nil)
		return nil;
	memcpy(d, s, n);
	d[n] = 0;
	return d;
}

int
luxplumb_recv(LuxPlumbMsg *msg)
{
	int fd, n;
	char buf[8192];
	char *p, *nl;
	int ndata;

	if(msg == nil)
		return -1;
	memset(msg, 0, sizeof *msg);
	fd = open("/mnt/plumb/lux", OREAD);
	if(fd < 0)
		fd = open("/mnt/plumb/edit", OREAD);
	if(fd < 0)
		return -1;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return -1;
	buf[n] = 0;
	p = buf;
	nl = strchr(p, '\n'); if(nl == nil) return -1; *nl = 0; msg->src = dupn(p, strlen(p)); p = nl+1;
	nl = strchr(p, '\n'); if(nl == nil) return -1; *nl = 0; msg->dst = dupn(p, strlen(p)); p = nl+1;
	nl = strchr(p, '\n'); if(nl == nil) return -1; *nl = 0; msg->wdir = dupn(p, strlen(p)); p = nl+1;
	nl = strchr(p, '\n'); if(nl == nil) return -1; *nl = 0; msg->type = dupn(p, strlen(p)); p = nl+1;
	nl = strchr(p, '\n'); if(nl == nil) return -1; p = nl+1; /* attr */
	nl = strchr(p, '\n'); if(nl == nil) return -1; *nl = 0; ndata = atoi(p); p = nl+1;
	if(ndata < 0)
		ndata = 0;
	if(p + ndata > buf + n)
		ndata = (buf + n) - p;
	msg->data = dupn(p, ndata);
	if(msg->src == nil || msg->dst == nil || msg->wdir == nil ||
	   msg->type == nil || msg->data == nil){
		luxplumb_msg_free(msg);
		return -1;
	}
	return 0;
}

void
luxplumb_msg_free(LuxPlumbMsg *msg)
{
	if(msg == nil)
		return;
	free(msg->src);
	free(msg->dst);
	free(msg->wdir);
	free(msg->type);
	free(msg->data);
	msg->src = msg->dst = msg->wdir = msg->type = msg->data = nil;
}

void
luxdraw_shutdown(void)
{
	luxdraw_close();
}
