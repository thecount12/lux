#ifndef lux_markdown_h
#define lux_markdown_h

/* Convert Markdown text to HTML. Caller frees returned string. */
char* mdToHtml(char* src, int len);

/* Load path and convert. Caller frees returned string. */
char* mdRenderFile(char* path);

#endif
