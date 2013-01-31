#include <stdio.h>
#include "lh_httpd.h"

const char* g_path = NULL;

void stop_server(const struct url_param *param, struct http_response_body* resb_body) {
	server_stop();
}

void server_path(const struct url_param *param, struct http_response_body* resb_body) {
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"path\":\"%s\"}", g_path);
	append_to_response(resb_body, buf);
}

int main(int argc, char **argv)
{
	g_path = argv[0];

	register_callback("/stopserver", stop_server);
	register_callback("/serverpath", server_path);
	
	server_start(5555, "../../../app_resource/www");
	server_loop();
	return 0;
}