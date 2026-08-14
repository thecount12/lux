#ifndef lux_luxdraw_h
#define lux_luxdraw_h

typedef struct {
	int kind;   /* 0=mouse 1=kbd 2=resize 3=quit */
	int x;
	int y;
	int button;
	int r;
} LuxDrawEvent;

typedef struct {
	char* src;
	char* dst;
	char* wdir;
	char* type;
	char* data;
} LuxPlumbMsg;

void luxdraw_set_helper_path(const char* argv0);
void luxdraw_shutdown(void);

int luxdraw_available(void);
int luxdraw_is_open(void);
int luxdraw_open(const char* title, int w, int h);
int luxdraw_fill(int x, int y, int w, int h, int r, int g, int b);
int luxdraw_string(int x, int y, const char* s, int r, int g, int b);
int luxdraw_flush(void);
int luxdraw_event(LuxDrawEvent* e);
int luxdraw_close(void);

int luxsnarf_get(char** out, int* len);
int luxsnarf_put(const char* s, int n);

int luxplumb_send(const char* dst, const char* data, const char* wdir);
int luxplumb_recv(LuxPlumbMsg* msg);
void luxplumb_msg_free(LuxPlumbMsg* msg);

#endif
