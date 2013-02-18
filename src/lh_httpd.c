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

#include "lh_httpd.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#define FRAME_TIMEOUT (1/60.f)
static int _listener = 0;
static struct fd_state *_state[FD_SETSIZE];
char _root_dir[512] = {0};
#define HEADER_BUF_SIZE 2048
#define BODY_BUF_SIZE 2048
#define READ_BUF_SIZE 16384
#define WRITE_BUF_SIZE 16384

static struct callback_elem {
    struct callback_elem *next;
    char *path;
    lh_request_callback callback;
} *callback_head = NULL;;

static enum running_state {
    rs_stopped,
    rs_running,
    rs_stopping,
} rs_state = rs_stopped;


struct lh_kv_elem {
    struct lh_kv_elem *next;
    const char *key;
    const char *value;
};

struct lh_response {
	char header[HEADER_BUF_SIZE];
	size_t header_len;
    char body[BODY_BUF_SIZE];
    size_t body_len;
};

enum io_tatus{
    s_ok,
    s_error,
    s_disconnect,
};

struct fd_state {
    char read_buf[16384];
    size_t n_read;
    
    int is_writing;
    char write_buf[16384];
    size_t n_written;
    size_t write_upto;
    FILE* pf;
};

static struct fd_state* alloc_fd_state(void) {
    struct fd_state *state = malloc(sizeof(struct fd_state));
    if (!state)
        return NULL;
    state->n_read = state->n_written = state->write_upto = 0;
    state->is_writing = 0;
    state->pf = NULL;
    return state;
}

static void free_fd_state(struct fd_state *state) {
    if (state->pf)
        fclose(state->pf);
    free(state);
	static int n = 0;
	printf("free:%d\n", ++n);
}

void urldecode(char *p) {
    int i=0;
    while(*(p+i)) {
        if ((*p=*(p+i)) == '%') {
            if (*(p+i+1) == 0 || *(p+i+2) == 0)
                break;
            *p=*(p+i+1) >= 'A' ? ((*(p+i+1) & 0XDF) - 'A') + 10 : (*(p+i+1) - '0');
            *p=(*p) * 16;
            *p+=*(p+i+2) >= 'A' ? ((*(p+i+2) & 0XDF) - 'A') + 10 : (*(p+i+2) - '0');
            i+=2;
        } else if (*(p+i)=='+') {
            *p=' ';  
        }  
        p++;  
    }  
    *p='\0';  
}

static int write_to_buf(struct fd_state *state, const char* src, size_t len) {
    assert(state && src);
    size_t remain = sizeof(state->write_buf) - state->write_upto;
    if (len > remain)
        return -1;
    memcpy(state->write_buf + state->write_upto, src, len);
    state->write_upto += len;
    return 0;
}

static int write_response_header(struct fd_state *state, const char* mime, size_t body_len, const char* user_header) {
    assert(state && user_header && mime);
    char buf[HEADER_BUF_SIZE] = {0};
    
    const char* fmt =
        "HTTP/1.1 200 OK\r\n" \
        "Content-Type: %s\r\n" \
        "Content-Length: %u\r\n" \
		"%s\r\n";
    
    snprintf(buf, sizeof(buf), fmt, mime, body_len, user_header);
    write_to_buf(state, buf, strlen(buf));
    return 0;
}

static void write_response_error(struct fd_state *state) {
    const char* msg = "HTTP/1.1 404 Not Found\r\nContent-Length:0\r\n\r\n";
    write_to_buf(state, msg, strlen(msg));
}

struct callback_elem *alloc_callback_elem(const char *path, lh_request_callback callback) {
    assert(path && callback);
    struct callback_elem *elem = malloc(sizeof(struct callback_elem));
    elem->next = NULL;
    size_t len = strlen(path) + 1;
    elem->path = malloc(len);
    strcpy(elem->path, path);
    elem->callback = callback;
    return elem;
}

void free_callback_elem(struct callback_elem *elem) {
    assert(elem);
    free(elem->path);
    free(elem);
}

static struct lh_kv_elem* parse_url_params(char *sparams) {
    if (!sparams)
        return NULL;
    struct lh_kv_elem *param = NULL;
    char *key = sparams;
    while (1) {
        char *eq_mark = strchr(key, '=');
        if (eq_mark == NULL)
            break;
        char *value = eq_mark + 1;
        struct lh_kv_elem *newparam = malloc(sizeof(struct lh_kv_elem));
        newparam->next = param;
        newparam->key = key;
        newparam->value = value;
        param = newparam;
        
        char *and_mark = strchr(value, '&');
        *eq_mark = 0;
        //urldecode(key);
        if (and_mark)
            *and_mark = 0;
        urldecode(value);
        if (and_mark == NULL)
            break;
        key = and_mark + 1;
    }
    return param;
}

static void free_kvs(struct lh_kv_elem* kv) {
    while (kv) {
        struct lh_kv_elem *next = kv->next;
        free(kv);
        kv = next;
    }
}

struct lh_kv_elem* parse_cookies(char *scookies) {
    if (!scookies)
        return NULL;
    struct lh_kv_elem *kv = NULL;
    char *key = scookies;
    while (1) {
        char *eq_mark = strchr(key, '=');
        if (eq_mark == NULL)
            break;
        char *value = eq_mark + 1;
        struct lh_kv_elem *newkv = malloc(sizeof(struct lh_kv_elem));
        newkv->next = kv;
        newkv->key = key;
        newkv->value = value;
        kv = newkv;
        
        char *split_mark = strchr(value, ';');
        *eq_mark = 0;
        if (split_mark)
            *split_mark = 0;
		else {
			split_mark = strchr(value, '\r');
			if (split_mark)
				*split_mark = 0;
			break;
		}
        key = split_mark + 1;
        while (*key == ' ')
            ++key;
    }
    
    return kv;
}

int call_callback(struct fd_state *state, const char *path, char *sparams, char *scookies) {
    assert(state && path);
    struct callback_elem *elem = callback_head;
    while (elem) {
        if (strcmp(path, elem->path) == 0) {
            struct lh_response response;
            response.header[0] = 0;
            response.header_len = 0;
			response.body[0] = 0;
			response.body_len = 0;
            
            struct lh_kv_elem *params = parse_url_params(sparams);
            struct lh_kv_elem *cookies = parse_cookies(scookies);
            elem->callback(params, cookies, &response);
            free_kvs(params);
            free_kvs(cookies);
            
            write_response_header(state, "application/json", response.body_len, response.header);
			if (response.header_len > 0)
				write_to_buf(state, response.header, response.header_len);
            if (response.body_len > 0)
                write_to_buf(state, response.body, response.body_len);
            return 1;
        }
        elem = elem->next;
    }
    return 0;
}

static int parse_request(struct fd_state *state) {
    assert(state && state->read_buf);
	//printf("%s", state->read_buf);
    char *path = strchr(state->read_buf, '/');
    if (path == NULL)
        return -1;
    
    char *scookies = strstr(path, "Cookie: ");
    if (scookies)
        scookies += 8;
    char *space = strchr(path, ' ');
    if (space) {
        *space = 0;
    }
    char *question = strchr(path, '?');
    if (question)
        *question = 0;
    urldecode(path);
    
    char *sparams = NULL;
    if (question)
        sparams = question + 1;
    
    int iscalled = call_callback(state, path, sparams, scookies);
    if (iscalled) {
        return 0;
    }
    const char *dot = strrchr(path, '.');
    if (dot) {  //file
        const char *ext = dot + 1;
        const char *mime = "text/plain";
        if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) {
            mime = "text/html";
        } else if (strcmp(ext, "css") == 0) {
            mime = "text/css";
        } else if (strcmp(ext, "js") == 0) {
            mime = "text/javascript";
        } else if (strcmp(ext, "png") == 0) {
            mime = "image/png";
        } else if (strcmp(ext, "jpeg") == 0 || strcmp(ext, "jpg") == 0 || strcmp(ext, "jpe") == 0 ) {
            mime = "image/jpeg";
        } else if (strcmp(ext, "gif") == 0) {
            mime = "image/gif";
        }
        char abspath[1024];
        strncpy(abspath, _root_dir, sizeof(abspath));
        strncat(abspath, path, sizeof(abspath)-strlen(path));
        state->pf = fopen(abspath, "r");
        if (state->pf) {
            fseek(state->pf, 0, SEEK_END);
            write_response_header(state, mime, ftell(state->pf), "");
            rewind(state->pf);
        } else {
            write_response_error(state);
        }
    } else { //func
        write_response_error(state);
    }
    return 0;
}

//0:continue -1:error 1:finish
static int do_read(int fd, struct fd_state *state) {
    assert(state);
    char buf[1024];
    
    enum io_tatus status = s_error;
    while (1) {
        ssize_t nread = recv(fd, buf, sizeof(buf), 0);
        if (nread < 0) {
            if (errno == EAGAIN)
                status = s_ok;
            else
                status = s_error;
            break;
        }
        if (nread == 0) {
            status = s_disconnect;
            break;
        }
        if (nread <= sizeof(state->read_buf) - state->n_read -1) {
            memcpy(state->read_buf + state->n_read, buf, nread);
            state->n_read += nread;
            state->read_buf[state->n_read] = 0;
        } else {
            status = s_error;
            break;
        }
    }
    
    if (status == s_ok) {
        const char *endtoken = "\r\n\r\n";
        const char *httpend = strstr(state->read_buf, endtoken);
        if (httpend && httpend < state->read_buf + state->n_read) {
            parse_request(state);
            
            httpend += strlen(endtoken);
            size_t remain = state->read_buf + state->n_read - httpend;
            if (remain > 0) {
                memmove(state->read_buf, httpend, remain);
            }
            state->n_read = remain;
            state->is_writing = 1;
        }
    }

    return status;
}

static int do_write(int fd, struct fd_state *state) {
    assert(state);
    enum io_tatus status = s_ok;
    clock_t t = clock();
    while (1) {
        if (state->pf) {
            size_t buf_remain = sizeof(state->write_buf) - state->write_upto;
            if (buf_remain > 0) {
                long offset1 = ftell(state->pf);
                size_t nread = fread(state->write_buf + state->write_upto, 1, buf_remain, state->pf);
                long offset2 = ftell(state->pf);
                if (nread > 0) {
                    state->write_upto += nread;
                } else {
                    if ( offset2 > offset1 ) {
                        state->write_upto += offset2 - offset1;
                    }
                    fclose(state->pf);
                    state->pf = NULL;
                }
            }
        }
        while (state->n_written < state->write_upto) {
            ssize_t result = send(fd, state->write_buf + state->n_written,
                                  state->write_upto - state->n_written, 0);
            if (result < 0) {
                if (errno == EAGAIN)
                    status = s_ok;
                else
                    status = s_error;
                break;
            }
            assert(result != 0);
            state->n_written += result;
        }
        if (status == s_ok) {
            if (state->n_written == state->write_upto) {
                state->n_written = state->write_upto = 0;
                if (state->pf == NULL) {
                    state->is_writing = 0;
                    break;
                }
            }
        } else {
            break;
        }
        if (clock() - t > CLOCKS_PER_SEC*FRAME_TIMEOUT)
            break;
    }
    return status;
}

//================================================================
int lh_start(unsigned short port, const char* root_dir) {
    assert(root_dir);
    strncpy(_root_dir, root_dir, sizeof(_root_dir));
    
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(port);
    
    _listener = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(_listener, F_SETFL, O_NONBLOCK);
    
    int on = 1;
    setsockopt(_listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (bind(_listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return -1;
    }
    
    if (listen(_listener, 16)<0) {
        perror("listen");
        return -1;
    }
    
    for (int i = 0; i < FD_SETSIZE; ++i)
        _state[i] = NULL;
    
    rs_state = rs_running;
    return 0;
}

static void lh_cleanup() {
	rs_state = rs_stopped;
	close(_listener);
	_listener = 0;
	for (int i=0; i < FD_SETSIZE; ++i) {
		if (_state[i]) {
			free_fd_state(_state[i]);
			_state[i] = NULL;
			close(i);
		}
	}
	lh_clear_callback();
}

void lh_stop() {
    if (rs_state != rs_running)
        return;
	lh_cleanup();
}

void lh_stop_delay() {
	if (rs_state != rs_running)
        return;
	rs_state = rs_stopping;
}

void lh_loop() {
    while (rs_state == rs_running)
        lh_select(-1);
}

void lh_select(int timeout) {
    if (rs_state == rs_stopping) {
        lh_cleanup();
        return;
    } else if (rs_state == rs_stopped) {
        return;
    }
    
    int maxfd = _listener;
    fd_set readset, writeset, exset;
    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_ZERO(&exset);
    FD_SET(_listener, &readset);
    for (int i=0; i < FD_SETSIZE; ++i) {
        if (_state[i]) {
            if (i > maxfd)
                maxfd = i;
            if (_state[i]->is_writing)
                FD_SET(i, &writeset);
            else
                FD_SET(i, &readset);
        }
    }
    struct timeval *pt = NULL;
    struct timeval t = {0};
    if (timeout >= 0) {
        t.tv_sec = timeout/1000;
        t.tv_usec = (timeout%1000)*1000;
        pt = &t;
    }
    if (select(maxfd+1, &readset, &writeset, &exset, pt) < 0) {
        perror("select");
        return;
    }
    if (FD_ISSET(_listener, &readset)) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int fd = accept(_listener, (struct sockaddr*)&ss, &slen);
		static int na = 0;
		printf("accept:%d\n", ++na);
        if (fd < 0) {
            perror("accept");
        } else if (fd > FD_SETSIZE) {
            close(fd);
        } else {
            fcntl(fd, F_SETFL, O_NONBLOCK);
            _state[fd] = alloc_fd_state();
        }
    }
    for (int i=0; i < maxfd+1; ++i) {
        if (i == _listener || _state[i] == NULL)
            continue;
        enum io_tatus status = s_ok;
        if (FD_ISSET(i, &readset))
            status = do_read(i, _state[i]);
        if (status == s_ok && (FD_ISSET(i, &writeset) || _state[i]->is_writing))
            status = do_write(i, _state[i]);
        if (status == s_error || status == s_disconnect) {
            free_fd_state(_state[i]);
            _state[i] = NULL;
            close(i);
        }
    }
}

int lh_append_header(struct lh_response *resp, const char *key, const char* value) {
	assert(resp && key && value);
	size_t remain = sizeof(resp->header) - resp->header_len;
	size_t len = strlen(key) + strlen(value) + 4;
	if (len >= remain)
		return -1;
	strcat(resp->header, key);
	strcat(resp->header, ": ");
	strcat(resp->header, value);
	strcat(resp->header, "\r\n");
	resp += len;
	return 0;
}

int lh_append_body(struct lh_response *resp, const char *src) {
	assert(resp && src);
    size_t remain = sizeof(resp->body) - resp->body_len;
    size_t len = strlen(src);
	if (len >= remain)
		return -1;
    strcat(resp->body, src);
	resp->body_len += len;
	return 0;
}

int lh_appendf_body(struct lh_response *resp, const char *fmt, ...) {
	assert(resp && fmt);
	char buf[BODY_BUF_SIZE];
	va_list ap;
    va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
	if (n < 0)
		return -1;
    return lh_append_body(resp, buf);
}

void lh_register_callback(const char *path, lh_request_callback callback) {
    assert(path && callback);
    struct callback_elem *elem = alloc_callback_elem(path, callback);
    if (callback_head)
        elem->next = callback_head;
    callback_head = elem;
}

void lh_clear_callback() {
    struct callback_elem *elem = callback_head;
    struct callback_elem *next;
    while (elem) {
        next = elem->next;
        free_callback_elem(elem);
        elem = next;
    }
    callback_head = NULL;
}

const char* lh_kv_string(const struct lh_kv_elem *kvs, const char *key, int *error) {
    assert(key);
    if (!kvs) {
        if (error)
            *error = 1;
        return NULL;
    }
    while (kvs) {
        if (strcmp(key, kvs->key) == 0) {
			if (error && !kvs->value)
				*error = 1;
            return kvs->value;
		}
        kvs = kvs->next;
    }
    *error = 1;
    return NULL;
}

#define _get_kv_value(type, func) \
    const char* str = lh_kv_string(kvs, key, NULL); \
    if (!str) { \
        if (error) \
            *error = 1; \
        return 0; \
    } \
    char* pEnd = NULL; \
    type n = (type)func; \
    if ((*str != '\0' && *pEnd == '\0')) \
        return n; \
    if (error) \
        *error = 1; \
    return 0;

int32_t lh_kv_int32(const struct lh_kv_elem *kvs, const char *key, int *error) {
    _get_kv_value(int32_t, strtol(str, &pEnd, 0));
}

uint32_t lh_kv_uint32(const struct lh_kv_elem *kvs, const char *key, int *error) {
    _get_kv_value(uint32_t, strtoul(str, &pEnd, 0));
}

int64_t lh_kv_int64(const struct lh_kv_elem *kvs, const char *key, int *error) {
    _get_kv_value(int64_t, strtoll(str, &pEnd, 0));
}

uint64_t lh_kv_uint64(const struct lh_kv_elem *kvs, const char *key, int *error) {
    _get_kv_value(uint64_t, strtoull(str, &pEnd, 0));
}

float lh_kv_float(const struct lh_kv_elem *kvs, const char *key, int *error) {
    _get_kv_value(float, strtof(str, &pEnd));
}

double lh_kv_double(const struct lh_kv_elem *kvs, const char *key, int *error) {
    _get_kv_value(double, strtod(str, &pEnd));
}


