local_httpd
===========
A Micro cross-platform HTTP server running at localhost. It serves static files simply by setting a root directory and support dynamic queries via callback mechanism.  
  
This is a decent solution for bridging web view and native C/C++ code. So we can pack client and server together into one application.

##Sample code for standalone
```c
#include "lh_httpd.h"

void cb_echo(const struct lh_kv_elem *params, const struct lh_kv_elem *cookies, struct lh_response* resp)
{
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
	lh_register_callback("/echo", cb_echo);
	
	lh_start(5555, "../../../app_resource/www");
	lh_loop();
	lh_stop();
	return 0;
}
```
##Sample code for game
```c
#include "lh_httpd.h"

void cb_echo(const struct lh_kv_elem *params, const struct lh_kv_elem *cookies, struct lh_response* resp)
{
  int err = 0;
	const char *message = lh_kv_string(params, "message", &err);
	if (err) {
		lh_append_body(resp, "{\"message\":\"error\"}");
		return;
	}
	lh_appendf_body(resp, "{\"message\":\"%s\"}", message);
}

void onGameInit()
{
	lh_register_callback("/echo", cb_echo);	
	lh_start(5555, "../../../app_resource/www");
}

void onGameExit()
{
	lh_stop();
}

void onUpdateFrame()
{
	lh_select(0); //no block
}

```
