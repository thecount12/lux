#ifndef lux_template_render_h
#define lux_template_render_h

#include "object.h"

/* Render path with ctx instance fields. Caller frees returned string. */
char* tplRenderFull(char* path, ObjInstance* ctx);

#endif
