// index.c - Staging area implementation
//
// Text format of .pes/index (one entry per line, sorted by path):
//
//   <mode-octal> <64-char-hex-hash> <mtime-seconds> <size> <path>
//
// PROVIDED functions: index_find, index_remove, index_status
// TODO functions:     index_load, index_save, index_add

#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>

// ---------------------------------------------------------------------------
// PROVIDED

// Find an index entry by path (linear scan).
IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

// Remove a file from the index.
// Returns 0 on success, -1 if path not in index.
int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        (size_t)remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

// Print the status of the working directory.
//
// Identifies files that are staged, unstaged (modified/deleted in working dir),
// and untracked (present in working dir but not in index).
// Returns 0.
int index_status(const Index *index) {
    printf("Staged changes:\n");
    int staged_count = 0;
    // Note: A true Git implementation deeply diffs against the HEAD tree here.
    // For this lab, displaying indexed files represents the staging intent.
    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        staged_count++;
    }
    if (staged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Unstaged changes:\n");
    int unstaged_count = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path);
            unstaged_count++;
        } else {
            if (st.st_mtime != (time_t)index->entries[i].mtime_sec ||
                st.st_size != (off_t)index->entries[i].size) {
                printf("  modified:   %s\n", index->entries[i].path);
                unstaged_count++;
            }
        }
    }
    if (unstaged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Untracked files:\n");
    int untracked_count = 0;
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0) continue;
            if (strcmp(ent->d_name, "pes") == 0) continue;
            if (strstr(ent->d_name, ".o") != NULL) continue;

            int is_tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    is_tracked = 1;
                    break;
                }
            }

            if (!is_tracked) {
                struct stat st;
                stat(ent->d_name, &st);
                if (S_ISREG(st.st_mode)) {
                    printf("  untracked:  %s\n", ent->d_name);
                    untracked_count++;
                }
            }
        }
        closedir(dir);
    }
    if (untracked_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    return 0;
}

// ---------------------------------------------------------------------------
// TODO: Implement these

static int compare_index_entries_by_path(const void *a, const void *b) {
    return strcmp(((const IndexEntry *)a)->path, ((const IndexEntry *)b)->path);
}

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// Load the index from .pes/index.
//
// Returns 0 on success, -1 on error.
int index_load(Index *index) {
    if (!index) return -1;

    index->count = 0;

    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) {
        return errno == ENOENT ? 0 : -1;
    }

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        if (index->count >= MAX_INDEX_ENTRIES) {
            fclose(f);
            return -1;
        }

        IndexEntry *entry = &index->entries[index->count];
        char hex[HASH_HEX_SIZE + 1];
        unsigned int mode;
        unsigned long long mtime_sec;
        unsigned int size;
        char path[sizeof(entry->path)];

        if (sscanf(line, "%o %64s %llu %u %511[^\n]",
                   &mode, hex, &mtime_sec, &size, path) != 5) {
            fclose(f);
            return -1;
        }

        if (hex_to_hash(hex, &entry->hash) != 0) {
            fclose(f);
            return -1;
        }

        entry->mode = mode;
        entry->mtime_sec = (uint64_t)mtime_sec;
        entry->size = size;
        snprintf(entry->path, sizeof(entry->path), "%s", path);
        index->count++;
    }

    fclose(f);
    return 0;
}

// Save the index to .pes/index atomically.
//
// Returns 0 on success, -1 on error.
int index_save(const Index *index) {
    if (!index) return -1;

    Index sorted = *index;
    qsort(sorted.entries, sorted.count, sizeof(IndexEntry), compare_index_entries_by_path);

    const char *tmp_path = INDEX_FILE ".tmp";
    FILE *f = fopen(tmp_path, "w");
    if (!f) return -1;

    for (int i = 0; i < sorted.count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted.entries[i].hash, hex);

        if (fprintf(f, "%o %s %" PRIu64 " %u %s\n",
                    sorted.entries[i].mode,
                    hex,
                    sorted.entries[i].mtime_sec,
                    sorted.entries[i].size,
                    sorted.entries[i].path) < 0) {
            fclose(f);
            unlink(tmp_path);
            return -1;
        }
    }

    if (fflush(f) != 0 || fsync(fileno(f)) != 0 || fclose(f) != 0) {
        unlink(tmp_path);
        return -1;
    }

    if (rename(tmp_path, INDEX_FILE) != 0) {
        unlink(tmp_path);
        return -1;
    }

    int dir_fd = open(PES_DIR, O_RDONLY);
    if (dir_fd < 0) return -1;

    int rc = fsync(dir_fd);
    close(dir_fd);
    return rc == 0 ? 0 : -1;
}

// Stage a file for the next commit.
//
// Returns 0 on success, -1 on error.
int index_add(Index *index, const char *path) {
    if (!index || !path) return -1;
    if (strlen(path) >= sizeof(index->entries[0].path)) return -1;

    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (!S_ISREG(st.st_mode)) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    size_t size = (size_t)st.st_size;
    uint8_t *data = malloc(size > 0 ? size : 1);
    if (!data) {
        fclose(f);
        return -1;
    }

    if (size > 0 && fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, data, size, &id) != 0) {
        free(data);
        return -1;
    }
    free(data);

    IndexEntry *entry = index_find(index, path);
    if (!entry) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        entry = &index->entries[index->count++];
    }

    entry->mode = (st.st_mode & S_IXUSR) ? 0100755 : 0100644;
    entry->hash = id;
    entry->mtime_sec = (uint64_t)st.st_mtime;
    entry->size = (uint32_t)st.st_size;
    snprintf(entry->path, sizeof(entry->path), "%s", path);

    return index_save(index);
}
