/* fetch - asynchronous small GETs (catalog feeds, cover images) on one worker thread, so
 * the main loop never blocks on the network. Images can be decoded on the worker too
 * (img/decode), the main thread then only uploads the RGBA.
 *
 * Requests are served newest first (the screen the user is looking at wins); fetch_forget
 * drops a request whether or not it ran. Results are kept until taken or forgotten. */
#ifndef NOCT_FETCH_H
#define NOCT_FETCH_H

#include "img/decode.h"
#include "net/http.h"

#define FETCH_MAX_BODY (4 * 1024 * 1024)
#define FETCH_SLOTS 64 /* requests in flight + finished-not-taken; fetch_get fails beyond */

typedef enum { FETCH_PENDING, FETCH_DONE, FETCH_FAILED, FETCH_UNKNOWN } fetch_state_t;

typedef struct {
	http_doc_t doc;  /* body (for an image request: freed, only status/final_url kept) */
	img_t img;       /* decoded image for fetch_image, else zeroed */
	char err[160];
} fetch_result_t;

/* Start / stop the worker. fetch_stop cancels the running transfer and frees everything. */
int fetch_start(void);
void fetch_stop(void);

/* Queue a GET. auth strings are copied. Returns a request id > 0, or -1 when every slot is
 * taken or the worker is not running. */
int fetch_get(const char *url, const http_auth_t *auth);
/* Queue a GET whose body is decoded as an image fitting max_w x max_h (img_decode). */
int fetch_image(const char *url, const http_auth_t *auth, int max_w, int max_h);

/* Load and decode a local image on the worker (max FETCH_MAX_BODY bytes). */
int fetch_file_image(const char *path, int max_w, int max_h);

/* State of a request; FETCH_UNKNOWN for an id never issued or already taken/forgotten. */
fetch_state_t fetch_state(int id);
/* When the request is DONE or FAILED: move its result into *out (caller frees with
 * fetch_result_free), free the slot and return the state. Else return the state and leave
 * *out zeroed. */
fetch_state_t fetch_take(int id, fetch_result_t *out);
void fetch_result_free(fetch_result_t *r);
/* Drop a request: cancelled if running, discarded if queued or finished. */
void fetch_forget(int id);

#endif
