#include <stdio.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#define READ_BUF_SIZE   8192
#define SHA256_HEX_SIZE 65

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --package FILE --version VERSION "
        "--url URL --output FILE\n", prog);
}

/* 计算文件 SHA256 */
static int file_sha256(const char *path, char hex[SHA256_HEX_SIZE]) {
    unsigned char buffer[READ_BUF_SIZE];
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen package");
        return -1;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        fclose(fp);
        return -1;
    }

    int ret = -1;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
        goto cleanup;

    size_t n;

    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, n) != 1)
            goto cleanup;
    }

    if (ferror(fp)) {
        perror("fread package");
        goto cleanup;
    }

    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1)
        goto cleanup;

    if (digest_len != 32)
        goto cleanup;

    for (unsigned int i = 0; i < digest_len; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);

    hex[64] = '\0';
    ret = 0;

cleanup:
    EVP_MD_CTX_free(ctx);
    fclose(fp);
    return ret;
}

/* 生成 OTA 版本信息 */
static int generate_manifest(
    const char *package,
    const char *version,
    const char *url,
    const char *output)
{
    struct stat st;

    if (stat(package, &st) < 0) {
        perror("stat package");
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "Package is not a regular file\n");
        return -1;
    }

    char sha256[SHA256_HEX_SIZE];

    if (file_sha256(package, sha256) < 0) {
        fprintf(stderr, "SHA-256 calculation failed\n");
        return -1;
    }

    const char *filename = strrchr(package, '/');
    filename = filename ? filename + 1 : package;

    FILE *fp = fopen(output, "w");
    if (!fp) {
        perror("fopen output");
        return -1;
    }

    int ret = fprintf(fp,
        "{\n"
        "    \"version\": \"%s\",\n"
        "    \"filename\": \"%s\",\n"
        "    \"url\": \"%s\",\n"
        "    \"size\": %" PRIdMAX ",\n"
        "    \"sha256\": \"%s\"\n"
        "}\n",
        version,
        filename,
        url,
        (intmax_t)st.st_size,
        sha256);

    if (ret < 0 || fclose(fp) != 0) {
        fprintf(stderr, "Failed to write manifest\n");
        return -1;
    }

    printf("OTA manifest generated: %s\n", output);
    printf("Version : %s\n", version);
    printf("Size    : %" PRIdMAX " bytes\n", (intmax_t)st.st_size);
    printf("SHA-256 : %s\n", sha256);

    return 0;
}

int main(int argc, char *argv[])
{
    const char *package = NULL;
    const char *version = NULL;
    const char *url = NULL;
    const char *output = NULL;

    static const struct option options[] = {
        {"package", required_argument, NULL, 'p'},
        {"version", required_argument, NULL, 'v'},
        {"url",     required_argument, NULL, 'u'},
        {"output",  required_argument, NULL, 'o'},
        {"help",    no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;

    while ((opt = getopt_long(
        argc, argv, "p:v:u:o:h", options, NULL)) != -1) {

        switch (opt) {
        case 'p':
            package = optarg;
            break;
        case 'v':
            version = optarg;
            break;
        case 'u':
            url = optarg;
            break;
        case 'o':
            output = optarg;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (!package || !version || !url || !output) {
        usage(argv[0]);
        return 1;
    }

    return generate_manifest(package, version, url, output) == 0
        ? EXIT_SUCCESS : EXIT_FAILURE;
}