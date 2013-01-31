//
//  lh_httpd.h
//
//  Created by Li Wei on 13-1-28.
//  Copyright (c) 2013 Li Wei. All rights reserved.
//

#ifndef lh_httpd_h
#define lh_httpd_h
#include <stdint.h>
#include <stdbool.h>

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
const char* get_param_string(const struct url_param *param, const char *key, bool *error);
int32_t get_param_int32(const struct url_param *param, const char *key, bool *error);
uint32_t get_param_uint32(const struct url_param *param, const char *key, bool *error);
int64_t get_param_int64(const struct url_param *param, const char *key, bool *error);
uint64_t get_param_uint64(const struct url_param *param, const char *key, bool *error);
float get_param_float(const struct url_param *param, const char *key, bool *error);
double get_param_double(const struct url_param *param, const char *key, bool *error);


#endif
