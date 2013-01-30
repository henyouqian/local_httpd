//
//  lh_httpd.h
//
//  Created by Li Wei on 13-1-28.
//  Copyright (c) 2013 Li Wei. All rights reserved.
//

#ifndef lh_httpd_h
#define lh_httpd_h

int server_start(unsigned short port, const char* root_dir);
void server_select();
void server_stop();

typedef void (*request_callback) (const char *param);
void register_callback(const char *path, request_callback cb);
void clear_callback();


#endif
