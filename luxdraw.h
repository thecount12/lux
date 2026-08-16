#ifndef lux_luxdraw_h
#define lux_luxdraw_h

typedef struct LuxDrawEvent LuxDrawEvent;
typedef struct LuxPlumbMsg LuxPlumbMsg;

struct LuxDrawEvent {
	int kind;
	int x;
	int y;
	int button;
	int r;
};

struct LuxPlumbMsg {
	char* src;
	char* dst;
	char* wdir;
	char* type;
	char* data;
};

void luxdraw_shutdown(void);
int luxdraw_available(void);
int luxdraw_is_open(void);
int luxdraw_open(char* title, int w, int h);
int luxdraw_info(int* w, int* h, int* fh, int* fw);
int luxdraw_fill(int x, int y, int w, int h, int r, int g, int b);
int luxdraw_string(int x, int y, char* s, int r, int g, int b);
int luxdraw_flush(void);
int luxdraw_event(LuxDrawEvent* e);
int luxdraw_close(void);
int luxsnarf_get(char** out, int* len);
int luxsnarf_put(char* s, int n);
int luxplumb_send(char* dst, char* data, char* wdir);
int luxplumb_recv(LuxPlumbMsg* msg);
void luxplumb_msg_free(LuxPlumbMsg* msg);

#endif
