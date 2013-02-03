//
//  main.c
//  console
//
//  Created by Li Wei on 13-2-2.
//  Copyright (c) 2013年 Li Wei. All rights reserved.
//

#include <stdio.h>
#include "lh_httpd.h"
#include "stdlib.h"

const char* g_path = NULL;

void stop_server(const struct kv_elem *param, const struct kv_elem *cookies, struct http_response_body* resb_body) {
	server_stop();
}

void server_path(const struct kv_elem *param, const struct kv_elem *cookies, struct http_response_body* resb_body) {
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"path\":\"%d:%s\"}", rand()%1000, g_path);
	append_to_response(resb_body, buf);
    const char *name = get_kv_string(cookies, "name", NULL);
    printf("%s\n", name);
}

int main(int argc, const char * argv[])
{
    g_path = argv[0];
    
	register_callback("/stopserver", stop_server);
	register_callback("/serverpath", server_path);
	
	server_start(5555, "./www");
	server_loop();
	return 0;
}

