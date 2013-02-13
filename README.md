local_httpd
===========
Micro cross-platform HTTP server running at localhost.

##Sample code for standalone
```c
#include "lh_httpd.h"

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

	lh_register_callback("/echo", cb_echo);
	
	lh_start(5555, "../../../app_resource/www");
	lh_loop();
	lh_stop();
	return 0;
}
```
