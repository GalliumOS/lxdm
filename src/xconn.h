#ifndef _XCONN_H_
#define _XCONN_H_

typedef void *xconn_t;
xconn_t xconn_open(const char *display);
void xconn_close(xconn_t c);
void xconn_clean(xconn_t c);

#endif/*_XCONN_H_*/

