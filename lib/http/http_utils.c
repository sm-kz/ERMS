#include "http_utils.h"
#include "ipc_utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <netdb.h>
#include <errno.h>

/**
 * 解析 http 请求的 method
 */
enum req_method http_get_request_method(const char *request) {
    if (!strncmp(request, "GET", 3))
        return REQUEST_GET;
    else if (!strncmp(request, "POST", 4))
        return REQUEST_POST;
    else
        return REQUEST_INVALID;
}

/**
 * 解析 http 请求的 target
 */
size_t http_get_request_target(const char *request, char **target, size_t *size) {
    if (request == NULL || target == NULL || size == NULL)
        return 0;

    char *s = strchr(request, ' ');
    if (s == NULL)
        return 0;
    ++s;

    char *e = strchr(s, ' ');
    if (e == NULL)
        return 0;

    size_t target_size = e - s + 1;

    if (target_size > *size) {
        char *temp = realloc(*target, target_size);
        if (temp == NULL)
            return 0;
        *target = temp;
        *size = target_size;
    }
    
    memcpy(*target, s, target_size - 1);
    (*target)[target_size - 1] = '\0';

    return target_size;
}

int http_path_match(const char *target, const char *path) {
    size_t len = strlen(path);

    return strncmp(target, path, len) == 0 &&
           (target[len] == '\0' || target[len] == '?');
}

char *http_get_body(const char *buf) {
    if (buf == NULL)
        return NULL;

    char *s = strstr(buf, "\r\n\r\n");
    if (s == NULL)
        return NULL;
    s += 4;

    return s;
}

ssize_t http_send_response(int fd,
                           const int status_code,
                           const char *status_text,
                           const char *content_type,
                           const char *body) {
    char response[1024];
    int len = snprintf(response, sizeof(response),
                    "HTTP/1.1 %d %s\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "Access-Control-Allow-Origin: *\r\n"    // 跨域读取开发板返回的数据
                    "\r\n"
                    "%s",
                    status_code, 
                    status_text, 
                    content_type,
                    strlen(body), 
                    body);

    return send_all(fd, response, strlen(response));
}

#define DOWNLOAD_BUF_SIZE   8192
#define HTTP_HEADER_MAX     8192

int http_download(char *ip, int port, char *file, char *save) {
    int ret = -1;
    int sock = -1;
    FILE *fp = NULL;
    struct addrinfo hints = {0}, *res = NULL, *p;
    char buffer[DOWNLOAD_BUF_SIZE];
    char header[HTTP_HEADER_MAX + 1];
    char request[1024];

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(ip, port_str, &hints, &res) != 0)
        return -1;

    for (p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0)
            continue;

        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0)
            break;
        
        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);

    if (sock < 0)
        return -1;

    struct timeval timeout = {15, 0};   // 15s 超时时间
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    int req_len = snprintf(request, sizeof(request), 
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Connection: close\r\n"
        "\r\n", file, ip, port_str);

    if (req_len < 0 || req_len >= sizeof(request))
        goto cleanup;

    /* 发送请求 */
    if (send_all(sock, request, req_len) < 0)
        goto cleanup;

    /* 读取 http 响应头 */
    size_t body_offset = 0;
    size_t header_len = 0;
    ssize_t n = 0;

    while (1) {
        n = recv(sock, header + header_len, HTTP_HEADER_MAX - header_len, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            goto cleanup;

        header_len += n;
        header[header_len] = '\0';

        char *end = strstr(header, "\r\n\r\n");
        if (end) {
            body_offset = (size_t)(end - header) + 4;
            break;
        }

        if (header_len == HTTP_HEADER_MAX)
            goto cleanup;
    }

    /* 检查 http 状态码 */
    int status = 0;
    if (sscanf(header, "HTTP/%*d.%*d %d", &status) != 1 ||
        status != 200) {
        fprintf(stderr, "HTTP status: %d\r\n", status);
        goto cleanup;
    }

    /* 解析响应头 */
    header[body_offset - 2] = '\0';

    char *line = strstr(header, "\r\n");
    if (!line)
        goto cleanup;
    
    line += 2;

    size_t content_len = 0;
    while (line) {
        char *next = strstr(line, "\r\n");
        if (!next)
            break;

        *next = '\0';

        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            char *value = line + 15;
            while (*value == ' ' || *value == '\t')
                value++;
            
            char *endptr;
            errno = 0;
            long long len = strtoll(value, &endptr, 10);

            if (errno || endptr == value || *endptr != '\0' || len < 0)
                goto cleanup;

            content_len = len;
        }

        line = next + 2;
    }

    if (content_len == 0) {
        fprintf(stderr, "Missing Content-Length\r\n");
        goto cleanup;
    }

    /* 打开需要写入的文件 */
    fp = fopen(save, "wb");
    if (!fp) {
        perror("fopen");
        goto cleanup;
    }

    /* 接收头部时可能接收到了部分 body */ 
    size_t initial_size = header_len - body_offset;

    if (initial_size > content_len)
        goto cleanup;

    size_t download = 0;
    if (initial_size > 0) {
        if (fwrite(header + body_offset, 1, initial_size, fp) != initial_size)
            goto cleanup;

        download += initial_size;
    }

    /* 下载剩余的部分 */
    while (download < content_len) {
        size_t remaining = (size_t)(content_len - download);
        size_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

        n = recv(sock, buffer, want, 0);
        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0) {
            fprintf(stderr, "Download interrupted\r\n");
            goto cleanup;
        }

        if (fwrite(buffer, 1, n, fp) != n)
            goto cleanup;

        download += n;
    }

    if (fflush(fp))
        goto cleanup;

    printf("Downloaded: %zu bytes\r\n", download);
    ret = 0;
    
cleanup:
    if (fp && fclose(fp))
        ret = -1;
    if (sock >= 0)
        close(sock);
    if (ret)
        unlink(save);

    return ret;
}