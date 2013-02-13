/****************************************************************************
Copyright (C) 2013 Li Wei <henyouqian@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/

#ifndef lh_httpd_h
#define lh_httpd_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int lh_start(unsigned short port, const char* root_dir);
void lh_stop();
void lh_stop_delay(); //call this function in lh_select();
void lh_loop();
void lh_select(int timeout); //timeout: use millisecond, if<0: block

struct lh_kv_elem;
struct lh_response;
typedef void (*lh_request_callback) (const struct lh_kv_elem *params, const struct lh_kv_elem *cookies, struct lh_response* resp);

int lh_append_header(struct lh_response *resp, const char *key, const char* value);
int lh_append_body(struct lh_response *resp, const char *content);
int lh_appendf_body(struct lh_response *resp, const char *fmt, ...);

void lh_register_callback(const char *path, lh_request_callback cb);
void lh_clear_callback();

//Param error not set when no error occurred.
const char* lh_kv_string(const struct lh_kv_elem *kvs, const char *key, int *error);
int32_t lh_kv_int32(const struct lh_kv_elem *kvs, const char *key, int *error);
uint32_t lh_kv_uint32(const struct lh_kv_elem *kvs, const char *key, int *error);
int64_t lh_kv_int64(const struct lh_kv_elem *kvs, const char *key, int *error);
uint64_t lh_kv_uint64(const struct lh_kv_elem *kvs, const char *key, int *error);
float lh_kv_float(const struct lh_kv_elem *kvs, const char *key, int *error);
double lh_kv_double(const struct lh_kv_elem *kvs, const char *key, int *error);

#ifdef __cplusplus
}
#endif

#endif
