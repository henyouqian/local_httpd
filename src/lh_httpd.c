//
//  lwhttpd.c
//
//  Created by Li Wei on 13-1-28.
//  Copyright (c) 2013 Li Wei. All rights reserved.
//

#include "lh_http.h"
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
#include <stdbool.h>
#include <time.h>

#define FRAME_TIMEOUT (1/60.f)
static int _listener = 0;
static struct fd_state *_state[FD_SETSIZE];
static int _isRunning = 0;
char _root_dir[512] = {0};

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
    
    ;
    for (int i = 0; i < FD_SETSIZE; ++i)
        _state[i] = NULL;
    
    _isRunning = 1;
    return 0;
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

enum Status{
    s_ok,
    s_error,
    s_disconnect,
};

int parseRequest(struct fd_state *state) {
    assert(state && state->read_buf);
    const char *path = strchr(state->read_buf, '/');
    if (path == NULL)
        return -1;
    
    const char *param = NULL;
    char *space = strchr(path, ' ');
    if (space) {
        *space = 0;
    }
    char *question = strchr(path, '?');
    if (question) {
        *question = 0;
        param = question + 1;
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
    
    enum Status status = s_error;
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
            //FIXME:DO PARSE HTML
            printf("%s", state->read_buf);
            parseRequest(state);
            //FIXME END
            
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
    
    enum Status status = s_ok;
    
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
        
        if (clock() - t > CLOCKS_PER_SEC*FRAME_TIMEOUT){
            break;
        }
    }
    
    return status;
}

void server_select() {
    if (!_isRunning)
        return;
    
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
    
    struct timeval t = {0};
    if (select(maxfd+1, &readset, &writeset, &exset, &t) < 0) {
        perror("select");
        return;
    }
    
    if (FD_ISSET(_listener, &readset)) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int fd = accept(_listener, (struct sockaddr*)&ss, &slen);
        printf("accept\n");
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
        
        enum Status status = s_ok;
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
    _isRunning = 0;
}
