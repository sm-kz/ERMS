#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <getopt.h>
#include <openssl/sha.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/dir.h>
#include <stdarg.h>
#include "cJSON.h"
#include "http_utils.h"
#include "version.h"

#define PID_FILE        "/var/run/erms.pid"
#define PROG_ENTRY      "./start.sh"

char *file = NULL;
char ip[16]= {0};
int port = 0;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --url URL\n", prog);
}

#define path_join(parent, child) do { \
    size_t len = strlen(parent); \
    if (parent[len - 1] != '/') { \
        if (sizeof(parent) < strlen(parent) + strlen(child) + 2) { \
            break; \
        } \
        parent[len] = '/'; \
        parent[len + 1] = '\0'; \
    } \
    strcat(parent, child); \
} while (0)

static int calculate_sha256(const char *filename, char hash[65])
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen sha");
        return -1;
    }

    SHA256_CTX ctx;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned char buffer[8192];
    size_t n;

    if (SHA256_Init(&ctx) != 1) {
        fclose(fp);
        return -1;
    }

    /* 分块读取文件 */
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (SHA256_Update(&ctx, buffer, n) != 1) {
            fclose(fp);
            return -1;
        }
    }

    if (ferror(fp)) {
        perror("fread");
        fclose(fp);
        return -1;
    }

    fclose(fp);

    if (SHA256_Final(digest, &ctx) != 1)
        return -1;

    /* 32 字节摘要转换为 64 字符十六进制字符串 */
    int i;
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hash + i * 2, "%02x", digest[i]);
    }

    hash[64] = '\0';

    return 0;
}

static int run_shell_cmd(int wait_child, char *cmd, ...) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        char *argv[64];
        int i = 0;

        argv[i++] = cmd;

        va_list ap;
        va_start(ap, cmd);
        const char *p;
        while (i < sizeof(argv) / sizeof(argv[0]) - 1 && (p = va_arg(ap, const char *)) != NULL)
            argv[i++] = (char *)p;

        va_end(ap);
        argv[i] = NULL;

        execvp(cmd, argv);   // 只有错误才会返回
        perror("execlp");
        _exit(127); // 命令未找到
    }

    if (!wait_child)
        return 0;

    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;

        perror("waitpid");
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "%s failed\n", cmd);
        return -1;
    }

    return 0;
}


int main(int argc, char **argv) {
    char *url = NULL;

    const struct option opts[] = {
        {"url", required_argument, NULL, 'u'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "u:h", opts, NULL)) != -1) {
        switch(opt) {
        case 'u':
            url = optarg;

            char *ip_pos = strstr(url, "://"); 
            if (!ip_pos) {
                fprintf(stderr, "Invalid url\r\n");
                exit(EXIT_FAILURE);
            }
            ip_pos += 3;

            char *port_pos = strchr(ip_pos, ':');
            if (!port_pos) {
                fprintf(stderr, "no port exists in url\r\n");
                exit(EXIT_FAILURE);
            }
            port_pos += 1;

            file = strchr(port_pos, '/');
            if (!file) {
                fprintf(stderr, "no file exists in url\r\n");
                exit(EXIT_FAILURE);
            }

            strncpy(ip, ip_pos, port_pos - ip_pos - 1);
            *file = '\0';
            sscanf(port_pos, "%d", &port);
            *file = '/';
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (!url) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    /* 1. 获取远程 json */
    if (http_download(ip, port, file, "/tmp/remote_version.json")) {
        fprintf(stderr, "HTTP download failed\r\n");
        exit(EXIT_FAILURE);
    }

    char buffer[8192];
    FILE *fp = fopen("/tmp/remote_version.json", "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open /tmp/remote_version.json\r\n");
        exit(EXIT_FAILURE);
    }
    unlink("/tmp/remote_version.json");

    size_t n = fread(buffer, 1, sizeof(buffer), fp);
    buffer[n] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        fprintf(stderr, "json syntax error\r\n");
        exit(EXIT_FAILURE);
    }

    /* 2. 比较版本 */
    cJSON *version_item = cJSON_GetObjectItem(root, "version");
    if (!version_item) {
        fprintf(stderr, "bad version file\r\n");
        exit(EXIT_FAILURE);
    }

    if (!strcmp(APP_VERSION, version_item->valuestring))
        exit(EXIT_SUCCESS);

    /* 3. http 下载 tar.gz */
    char cwd[128];
    char archive[180] = {0};
    if (!getcwd(cwd, sizeof(cwd)))
        exit(EXIT_SUCCESS);

    strncpy(archive, cwd, strlen(cwd));
    cJSON *filename_item = cJSON_GetObjectItem(root, "filename");
    if (!filename_item) {
        fprintf(stderr, "bad version file\r\n");
        exit(EXIT_FAILURE);
    }

    path_join(archive, filename_item->valuestring);

    http_download(ip, port, filename_item->valuestring, archive);

    /* 4. SHA-256 校验 */
    char sha[65];
    if (calculate_sha256(archive, sha))
        exit(EXIT_SUCCESS);

    cJSON *sha_item = cJSON_GetObjectItem(root, "sha256");
    if (!sha_item) {
        fprintf(stderr, "bad version file\r\n");
        goto cleanup;
    }

    if (strcmp(sha, sha_item->valuestring)) {
        fprintf(stderr, "Downloaded file has a sha256 string which "
                        "doesn't conform with the target item in version.json\r\n");
        goto cleanup;
    }
        
    /* 5. 解压并安装新版本 */
    char stage_dir[128] = "/tmp/ota_stage";

    if (run_shell_cmd(1, "mkdir", "-p", stage_dir, NULL))
        goto cleanup;

    if (run_shell_cmd(1, "tar", "-xzf", archive, "-C", stage_dir, NULL))
        goto cleanup;

    /* 6. 重启并检查运行状态 */
    int need_restart = 1;
    FILE *pidf = fopen(PID_FILE, "r");
    if (!pidf) {
        if (errno != ENOENT) {  // 程序在运行但 pid 文件打开失败
            perror("fopen pid");
            goto cleanup;
        }
        // 程序未运行
        need_restart = 0;
    } else {
        pid_t pid;
        if (fread(&pid, sizeof(pid), 1, pidf) != 1) {
            fprintf(stderr, "Bad pid file\r\n");
            goto cleanup;
        }
    
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }

    // 用所有解压的新文件将旧文件覆盖
    path_join(stage_dir, "erms/.");
    if (run_shell_cmd(1, "cp", "-ar", stage_dir, cwd, NULL))
        goto cleanup;
    
    // 重新启动
    if (need_restart) {
        pid_t newp = fork();

        if (newp == 0)
            return run_shell_cmd(0, PROG_ENTRY, NULL);

        waitpid(newp, NULL, 0);
    }

    run_shell_cmd(1, "rm", "-rf", "/tmp/ota_stage", NULL);
    unlink(archive);
    exit(EXIT_SUCCESS);

cleanup:
    run_shell_cmd(1, "rm", "-rf", "/tmp/ota_stage", NULL);
    unlink(archive);
    exit(EXIT_FAILURE);
}