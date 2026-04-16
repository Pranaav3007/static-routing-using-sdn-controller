// tree.c - Tree object serialization and construction
//
// PROVIDED functions: get_file_mode, tree_parse, tree_serialize
// TODO functions:     tree_from_index
//
// Binary tree format (per entry, concatenated with no separators):
//   "<mode-as-ascii-octal> <name>\0<32-byte-binary-hash>"

#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#if defined(__GNUC__)
extern int index_load(Index *index) __attribute__((weak));
#endif

// ---------------------------------------------------------------------------
// Mode Constants

#define MODE_FILE      0100644
#define MODE_EXEC      0100755
#define MODE_DIR       0040000

// ---------------------------------------------------------------------------
// PROVIDED

// Determine the object mode for a filesystem path.
uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;

    if (S_ISDIR(st.st_mode))  return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

// Parse binary tree data into a Tree struct safely.
// Returns 0 on success, -1 on parse error.
int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1;

        char mode_str[16] = {0};
        size_t mode_len = (size_t)(space - ptr);
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = strtol(mode_str, NULL, 8);

        ptr = space + 1;

        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1;

        size_t name_len = (size_t)(null_byte - ptr);
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0';

        ptr = null_byte + 1;

        if (ptr + HASH_SIZE > end) return -1;
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }

    return ptr == end ? 0 : -1;
}

static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

// Serialize a Tree struct into binary format for storage.
// Caller must free(*data_out).
// Returns 0 on success, -1 on error.
int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    size_t max_size = tree->count > 0 ? (size_t)tree->count * 296 : 1;
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];

        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += (size_t)written + 1;

        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ---------------------------------------------------------------------------
// TODO: Implement these

static int compare_index_entries(const void *a, const void *b) {
    return strcmp(((const IndexEntry *)a)->path, ((const IndexEntry *)b)->path);
}

static int append_tree_entry(Tree *tree, uint32_t mode, const ObjectID *hash, const char *name) {
    if (tree->count >= MAX_TREE_ENTRIES) return -1;

    TreeEntry *entry = &tree->entries[tree->count++];
    entry->mode = mode;
    entry->hash = *hash;
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    return 0;
}

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

static int write_tree_level(const IndexEntry *entries, int count,
                            const char *prefix, size_t prefix_len,
                            ObjectID *id_out) {
    Tree tree = {0};

    for (int i = 0; i < count; ) {
        const char *path = entries[i].path;
        if (strncmp(path, prefix, prefix_len) != 0) return -1;

        const char *rest = path + prefix_len;
        const char *slash = strchr(rest, '/');

        if (!slash) {
            if (append_tree_entry(&tree, entries[i].mode, &entries[i].hash, rest) != 0) {
                return -1;
            }
            i++;
            continue;
        }

        size_t dir_len = (size_t)(slash - rest);
        if (dir_len == 0 || dir_len >= sizeof(tree.entries[0].name)) return -1;

        char dir_name[256];
        memcpy(dir_name, rest, dir_len);
        dir_name[dir_len] = '\0';

        char child_prefix[512];
        int child_prefix_len = snprintf(child_prefix, sizeof(child_prefix),
                                        "%s%.*s/", prefix, (int)dir_len, rest);
        if (child_prefix_len < 0 || child_prefix_len >= (int)sizeof(child_prefix)) return -1;

        int start = i;
        i++;
        while (i < count &&
               strncmp(entries[i].path, child_prefix, (size_t)child_prefix_len) == 0) {
            i++;
        }

        ObjectID child_id;
        if (write_tree_level(entries + start, i - start,
                             child_prefix, (size_t)child_prefix_len,
                             &child_id) != 0) {
            return -1;
        }

        if (append_tree_entry(&tree, MODE_DIR, &child_id, dir_name) != 0) {
            return -1;
        }
    }

    if (tree.count == 0) {
        return object_write(OBJ_TREE, "", 0, id_out);
    }

    void *raw;
    size_t raw_len;
    if (tree_serialize(&tree, &raw, &raw_len) != 0) return -1;

    int rc = object_write(OBJ_TREE, raw, raw_len, id_out);
    free(raw);
    return rc;
}

// Build a tree hierarchy from the current index and write all tree
// objects to the object store.
//
// Returns 0 on success, -1 on error.
int tree_from_index(ObjectID *id_out) {
    if (!id_out) return -1;
#if defined(__GNUC__)
    if (!index_load) return -1;
#endif

    Index index;
    if (index_load(&index) != 0) return -1;

    if (index.count == 0) {
        return object_write(OBJ_TREE, "", 0, id_out);
    }

    qsort(index.entries, index.count, sizeof(IndexEntry), compare_index_entries);
    return write_tree_level(index.entries, index.count, "", 0, id_out);
}
