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

void stop_server(const struct lh_kv_elem *param, const struct lh_kv_elem *cookies, struct lh_response* resp) {
	lh_stop();
}

void server_path(const struct lh_kv_elem *param, const struct lh_kv_elem *cookies, struct lh_response* resp) {
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"path\":\"%d:%s\"}", rand()%1000, g_path);
	lh_append_body(resp, buf);
    const char *name = lh_kv_string(cookies, "name", NULL);
    printf("%s\n", name);
}

int main(int argc, const char * argv[])
{
    g_path = argv[0];
    
	lh_register_callback("/stopserver", stop_server);
	lh_register_callback("/serverpath", server_path);
	
	lh_start(5555, "./www");
	lh_loop();
	return 0;
}

