//
//  lwhttpd.c
//
//  Created by Li Wei on 13-1-28.
//  Copyright (c) 2013 Li Wei. All rights reserved.
//

#include "lh_httpd.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#define FRAME_TIMEOUT (1/60.f)
static int _listener = 0;
static struct fd_state *_state[FD_SETSIZE];
char _root_dir[512] = {0};

static struct callback_elem {
    struct callback_elem *next;
    char *path;
    request_callback callback;
} *callback_head = NULL;;

static enum running_state {
    rs_stopped,
    rs_running,
    rs_stopping,
} rs_state = rs_stopped;


struct url_param {
    struct url_param *next;
    const char *key;
    const char *value;
};

struct http_response_body {
    char buf[1024];
    int len;
};

enum io_tatus{
    s_ok,
    s_error,
    s_disconnect,
};

struct fd_state {
    char read_buf[16384];
    size_t n_read;
    
    bool writing;
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
    state->writing = false;
    state->pf = NULL;
    return state;
}

static void free_fd_state(struct fd_state *state) {
    if (state->pf)
        fclose(state->pf);
    free(state);
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

static int write_response_header(struct fd_state *state, size_t content_len, const char* mime) {
    assert(state && mime);
    char buf[512] = {0};
    
    const char* fmt =
        "HTTP/1.1 200 OK\r\n" \
        "Content-Type: %s\r\n" \
        "Content-Length: %u\r\n\r\n";
    
    snprintf(buf, sizeof(buf), fmt, mime, content_len);
    write_to_buf(state, buf, strlen(buf));
    
    return 0;
}

static void write_response_error(struct fd_state *state) {
    const char* msg = "HTTP/1.1 404 Not Found\r\nContent-Length:0\r\n\r\n";
    write_to_buf(state, msg, strlen(msg));
}

struct callback_elem *alloc_callback_elem(const char *path, request_callback callback) {
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

bool call_callback(struct fd_state *state, const char *path, const struct url_param *param) {
    assert(state && path);
    struct callback_elem *elem = callback_head;
    while (elem) {
        if (strcmp(path, elem->path) == 0) {
            struct http_response_body response_body;
            response_body.buf[0] = 0;
            response_body.len = 0;
            elem->callback(param, &response_body);
            write_response_header(state, response_body.len, "application/json");
            if (response_body.len > 0)
                write_to_buf(state, response_body.buf, response_body.len);
            return true;
        }
        elem = elem->next;
    }
    return false;
}

static void free_url_params(struct url_param* param){
    while (param) {
        struct url_param *next = param->next;
        free(param);
        param = next;
    }
}

static int parseRequest(struct fd_state *state) {
    assert(state && state->read_buf);
    char *path = strchr(state->read_buf, '/');
    if (path == NULL)
        return -1;
    
    char *space = strchr(path, ' ');
    if (space) {
        *space = 0;
    }
    char *question = strchr(path, '?');
    if (question)
        *question = 0;
    urldecode(path);
    
    char *sparam = NULL;
    struct url_param *param = NULL;
    if (question) {
        sparam = question + 1;
        char *key = sparam;
        while (1) {
            char *eq_mark = strchr(key, '=');
            if (eq_mark == NULL)
                break;
            char *value = eq_mark + 1;
            struct url_param *newparam = malloc(sizeof(struct url_param));
            newparam->next = param;
            newparam->key = key;
            newparam->value = value;
            param = newparam;
            
            char *and_mark = strchr(value, '&');
            *eq_mark = 0;
            urldecode(key);
            if (and_mark)
                *and_mark = 0;
            urldecode(value);
            if (and_mark == NULL)
                break;
            key = and_mark + 1;
        }
    }
    
    bool iscalled = call_callback(state, path, param);
    free_url_params(param);
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
            write_response_header(state, ftell(state->pf), mime);
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
            //printf("%s", state->read_buf);
            parseRequest(state);
            
            httpend += strlen(endtoken);
            int remain = state->read_buf + state->n_read - httpend;
            if (remain > 0) {
                memmove(state->read_buf, httpend, remain);
            }
            state->n_read = remain;
            state->writing = true;
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
                    state->writing = false;
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
int server_start(unsigned short port, const char* root_dir) {
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

void server_loop() {
    while (rs_state == rs_running)
        server_select(-1);
}

void server_select(int timeout) {
    if (rs_state == rs_stopping) {
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
        clear_callback();
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
            if (_state[i]->writing) {
                FD_SET(i, &writeset);
            } else {
                FD_SET(i, &readset);
            }
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
        if (FD_ISSET(i, &readset)) {
            status = do_read(i, _state[i]);
            
        }
        if (status == s_ok && (FD_ISSET(i, &writeset) || _state[i]->writing)) {
            status = do_write(i, _state[i]);
        }
        if (status == s_error || status == s_disconnect) {
            free_fd_state(_state[i]);
            _state[i] = NULL;
            close(i);
        }
    }
}

void server_stop() {
    if (rs_state != rs_running)
        return;
    rs_state = rs_stopping;
}

void append_to_response(struct http_response_body *body, const char *src) {
    assert(body && src);
    int remain = sizeof(body->buf) - body->len;
    size_t len = strlen(src);
    if (remain > 0 && len < remain) {
        strcat(body->buf, src);
        body->len += len;
    }
}

void register_callback(const char *path, request_callback callback) {
    assert(path && callback);
    struct callback_elem *elem = alloc_callback_elem(path, callback);
    if (callback_head)
        elem->next = callback_head;
    callback_head = elem;
}

void clear_callback() {
    struct callback_elem *elem = callback_head;
    struct callback_elem *next;
    while (elem) {
        next = elem->next;
        free_callback_elem(elem);
        elem = next;
    }
    callback_head = NULL;
}

const char* get_param_string(const struct url_param *param, const char *key, bool *error) {
    assert(key);
    if (!param) {
        if (error)
            *error = true;
        return NULL;
    }
    while (param) {
        if (strcmp(key, param->key) == 0) {
            return param->value;
        }
        param = param->next;
    }
    *error = true;
    return NULL;
}

#define _get_param_value(type, func) \
    const char* str = get_param_string(param, key, NULL); \
    if (!str) { \
        if (error) \
            *error = true; \
        return 0; \
    } \
    char* pEnd = NULL; \
    type n = func; \
    if ((*str != '\0' && *pEnd == '\0')) \
        return n; \
    if (error) \
        *error = true; \
    return 0;

int32_t get_param_int32(const struct url_param *param, const char *key, bool *error) {
    _get_param_value(int32_t, strtol(str, &pEnd, 0));
}

uint32_t get_param_uint32(const struct url_param *param, const char *key, bool *error) {
    _get_param_value(uint32_t, strtoul(str, &pEnd, 0));
}

int64_t get_param_int64(const struct url_param *param, const char *key, bool *error) {
    _get_param_value(int64_t, strtoll(str, &pEnd, 0));
}

uint64_t get_param_uint64(const struct url_param *param, const char *key, bool *error) {
    _get_param_value(uint64_t, strtoull(str, &pEnd, 0));
}

float get_param_float(const struct url_param *param, const char *key, bool *error) {
    _get_param_value(float, strtof(str, &pEnd));
}

double get_param_double(const struct url_param *param, const char *key, bool *error) {
    _get_param_value(double, strtod(str, &pEnd));
}


