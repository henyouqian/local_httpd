#include <stdio.h>
#include <stdlib.h>
#include "lh_httpd.h"

const char* g_path = NULL;

void cb_stop(const struct lh_kv_elem *params, const struct lh_kv_elem *cookies, struct lh_response* resp) {
	lh_stop_delay();
}

void cb_path(const struct lh_kv_elem *params, const struct lh_kv_elem *cookies, struct lh_response* resp) {
	char buf[512];
	snprintf(buf, sizeof(buf), "{\"path\":\"%d%s\"}", rand()%1000, g_path);
	lh_append_body(resp, buf);
}

void cb_echo(const struct lh_kv_elem *params, const struct lh_kv_elem *cookies, struct lh_response* resp) {
	int err = 0;
	const char *message = lh_kv_string(params, "message", &err);
	if (err) {
		lh_append_body(resp, "{\"message\":\"error\"}");
		return;
	}
	lh_appendf_body(resp, "{\"message\":\"%s\"}", message);
}

int main(int argc, char **argv)
{
	g_path = argv[0];

	lh_register_callback("/stopserver", cb_stop);
	lh_register_callback("/serverpath", cb_path);
	lh_register_callback("/echo", cb_echo);
	
	lh_start(5555, "../../../app_resource/www");
	lh_loop();
	lh_stop();
	return 0;
}
