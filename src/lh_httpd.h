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

int server_start(unsigned short port, const char* root_dir);
void server_loop();
void server_select(int timeout); //timeout: use millisecond, if<0: block
void server_stop();

struct url_param;
struct http_response_body;
typedef void (*request_callback) (const struct url_param *param, struct http_response_body* resb_body);

void append_to_response(struct http_response_body *resb_body, const char *content);

void register_callback(const char *path, request_callback cb);
void clear_callback();

//Param error not set when no error occurred.
const char* get_param_string(const struct url_param *param, const char *key, int *error);
int32_t get_param_int32(const struct url_param *param, const char *key, int *error);
uint32_t get_param_uint32(const struct url_param *param, const char *key, int *error);
int64_t get_param_int64(const struct url_param *param, const char *key, int *error);
uint64_t get_param_uint64(const struct url_param *param, const char *key, int *error);
float get_param_float(const struct url_param *param, const char *key, int *error);
double get_param_double(const struct url_param *param, const char *key, int *error);

#ifdef __cplusplus
}
#endif

#endif
