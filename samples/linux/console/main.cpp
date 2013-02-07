#include <stdio.h>
#include <stdlib.h>
#include "lh_httpd.h"

const char* g_path = NULL;

void stop_server(const struct lh_kv_elem *params, const struct lh_kv_elem *cookies, struct lh_response* resp) {
	lh_stop();
}

void server_path(const struct lh_kv_elem *params, const struct lh_kv_elem *cookies, struct lh_response* resp) {
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"path\":\"%d%s\"}", rand()%1000, g_path);
	lh_append_body(resp, buf);
}

int main(int argc, char **argv)
{
	g_path = argv[0];

	lh_register_callback("/stopserver", stop_server);
	lh_register_callback("/serverpath", server_path);
	
	lh_start(5555, "../../../app_resource/www");
	lh_loop();
	lh_stop();
	return 0;
}
