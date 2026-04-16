// object.c - Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions:     object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#if defined(__has_include)
#  if __has_include(<openssl/evp.h>)
#    include <openssl/evp.h>
#    define PES_USE_OPENSSL 1
#  endif
#endif

#ifndef PES_USE_OPENSSL
#define PES_USE_OPENSSL 0
#endif

// ---------------------------------------------------------------------------
// PROVIDED

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

#if !PES_USE_OPENSSL
typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t buffer[64];
    size_t buffer_len;
} Sha256Ctx;

static const uint32_t sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static uint32_t rotr32(uint32_t value, unsigned int bits) {
    return (value >> bits) | (value << (32U - bits));
}

static uint32_t load_be32(const uint8_t *src) {
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void store_be32(uint32_t value, uint8_t *dst) {
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static void store_be64(uint64_t value, uint8_t *dst) {
    for (int i = 7; i >= 0; i--) {
        dst[i] = (uint8_t)value;
        value >>= 8;
    }
}

static void sha256_transform(Sha256Ctx *ctx, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = load_be32(block + (size_t)i * 4);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + sha256_k[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(Sha256Ctx *ctx) {
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->bit_len = 0;
    ctx->buffer_len = 0;
}

static void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len) {
    while (len > 0) {
        size_t take = 64 - ctx->buffer_len;
        if (take > len) take = len;

        memcpy(ctx->buffer + ctx->buffer_len, data, take);
        ctx->buffer_len += take;
        ctx->bit_len += (uint64_t)take * 8U;
        data += take;
        len -= take;

        if (ctx->buffer_len == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }
}

static void sha256_final(Sha256Ctx *ctx, uint8_t out[32]) {
    ctx->buffer[ctx->buffer_len++] = 0x80;

    if (ctx->buffer_len > 56) {
        while (ctx->buffer_len < 64) {
            ctx->buffer[ctx->buffer_len++] = 0;
        }
        sha256_transform(ctx, ctx->buffer);
        ctx->buffer_len = 0;
    }

    while (ctx->buffer_len < 56) {
        ctx->buffer[ctx->buffer_len++] = 0;
    }

    store_be64(ctx->bit_len, ctx->buffer + 56);
    sha256_transform(ctx, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        store_be32(ctx->state[i], out + (size_t)i * 4);
    }
}
#endif

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
#if PES_USE_OPENSSL
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
#else
    Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)data, len);
    sha256_final(&ctx, id_out->hash);
#endif
}

// Get the filesystem path where an object should be stored.
// Format: .pes/objects/XX/YYYYYYYY...
// The first 2 hex chars form the shard directory; the rest is the filename.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ---------------------------------------------------------------------------
// TODO: Implement these

static const char *object_type_name(ObjectType type) {
    switch (type) {
        case OBJ_BLOB:   return "blob";
        case OBJ_TREE:   return "tree";
        case OBJ_COMMIT: return "commit";
        default:         return NULL;
    }
}

static int write_all(int fd, const void *data, size_t len) {
    const uint8_t *ptr = (const uint8_t *)data;

    while (len > 0) {
        ssize_t written = write(fd, ptr, len);
        if (written < 0) return -1;
        ptr += written;
        len -= (size_t)written;
    }

    return 0;
}

// Write an object to the store.
//
// Object format on disk:
//   "<type> <size>\0<data>"
//   where <type> is "blob", "tree", or "commit"
//   and <size> is the decimal string of the data length
//
// Returns 0 on success, -1 on error.
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    if (!id_out || (!data && len > 0)) return -1;

    const char *type_name = object_type_name(type);
    if (!type_name) return -1;

    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_name, len);
    if (header_len < 0 || (size_t)header_len + 1 >= sizeof(header)) return -1;

    size_t full_len = (size_t)header_len + 1 + len;
    uint8_t *full = malloc(full_len > 0 ? full_len : 1);
    if (!full) return -1;

    memcpy(full, header, (size_t)header_len + 1);
    if (len > 0) memcpy(full + header_len + 1, data, len);

    compute_hash(full, full_len, id_out);
    if (object_exists(id_out)) {
        free(full);
        return 0;
    }

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);

    char shard_dir[512];
    if (snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, hex) >= (int)sizeof(shard_dir)) {
        free(full);
        return -1;
    }

    if (mkdir(shard_dir, 0755) != 0 && errno != EEXIST) {
        free(full);
        return -1;
    }

    char final_path[512];
    object_path(id_out, final_path, sizeof(final_path));

    char tmp_path[512];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s/.tmp-%ld-XXXXXX", shard_dir, (long)getpid()) >= (int)sizeof(tmp_path)) {
        free(full);
        return -1;
    }

    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        free(full);
        return -1;
    }

    int rc = 0;
    if (fchmod(fd, 0644) != 0 ||
        write_all(fd, full, full_len) != 0 ||
        fsync(fd) != 0 ||
        close(fd) != 0) {
        rc = -1;
    }

    free(full);

    if (rc != 0) {
        unlink(tmp_path);
        return -1;
    }

    if (rename(tmp_path, final_path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    int dir_fd = open(shard_dir, O_RDONLY);
    if (dir_fd < 0) return -1;

    rc = fsync(dir_fd);
    close(dir_fd);
    return rc == 0 ? 0 : -1;
}

// Read an object from the store.
//
// The caller is responsible for calling free(*data_out).
// Returns 0 on success, -1 on error (file not found, corrupt, etc.).
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    if (!id || !type_out || !data_out || !len_out) return -1;

    *data_out = NULL;
    *len_out = 0;

    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }

    long file_size = ftell(f);
    if (file_size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    size_t raw_len = (size_t)file_size;
    uint8_t *raw = malloc(raw_len > 0 ? raw_len : 1);
    if (!raw) {
        fclose(f);
        return -1;
    }

    if (raw_len > 0 && fread(raw, 1, raw_len, f) != raw_len) {
        free(raw);
        fclose(f);
        return -1;
    }

    fclose(f);

    const uint8_t *null_byte = memchr(raw, '\0', raw_len);
    if (!null_byte) {
        free(raw);
        return -1;
    }

    size_t header_len = (size_t)(null_byte - raw);
    if (header_len >= 128) {
        free(raw);
        return -1;
    }

    char header[128];
    memcpy(header, raw, header_len);
    header[header_len] = '\0';

    char type_name[16];
    size_t data_len;
    if (sscanf(header, "%15s %zu", type_name, &data_len) != 2) {
        free(raw);
        return -1;
    }

    if (header_len + 1 > raw_len || raw_len - header_len - 1 != data_len) {
        free(raw);
        return -1;
    }

    ObjectID computed;
    compute_hash(raw, raw_len, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(raw);
        return -1;
    }

    if (strcmp(type_name, "blob") == 0) {
        *type_out = OBJ_BLOB;
    } else if (strcmp(type_name, "tree") == 0) {
        *type_out = OBJ_TREE;
    } else if (strcmp(type_name, "commit") == 0) {
        *type_out = OBJ_COMMIT;
    } else {
        free(raw);
        return -1;
    }

    uint8_t *payload = malloc(data_len > 0 ? data_len : 1);
    if (!payload) {
        free(raw);
        return -1;
    }

    if (data_len > 0) memcpy(payload, raw + header_len + 1, data_len);

    free(raw);
    *data_out = payload;
    *len_out = data_len;
    return 0;
}
