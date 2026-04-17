#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

// Find an index entry by path (linear scan).
IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            return &index->entries[i];
        }
    }
    return NULL;
}

// Remove a file from the index.
int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {

            int remaining = index->count - i - 1;
            if (remaining > 0) {
                memmove(&index->entries[i],
                        &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            }

            index->count--;
            return index_save(index);
        }
    }

    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

// Print working directory status
int index_status(const Index *index) {

    printf("Staged changes:\n");
    int staged_count = 0;

    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        staged_count++;
    }

    if (staged_count == 0)
        printf("  (nothing to show)\n");

    printf("\nUnstaged changes:\n");
    int unstaged_count = 0;

    for (int i = 0; i < index->count; i++) {
        struct stat st;

        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path);
            unstaged_count++;
        } else {
            if (st.st_mtime != (time_t)index->entries[i].mtime_sec ||
                st.st_size != index->entries[i].size) {

                printf("  modified:   %s\n", index->entries[i].path);
                unstaged_count++;
            }
        }
    }

    if (unstaged_count == 0)
        printf("  (nothing to show)\n");

    printf("\nUntracked files:\n");
    int untracked_count = 0;

    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;

        while ((ent = readdir(dir)) != NULL) {

            // Skip unwanted files
            if (strcmp(ent->d_name, ".") == 0 ||
                strcmp(ent->d_name, "..") == 0 ||
                strcmp(ent->d_name, ".pes") == 0 ||
                strcmp(ent->d_name, "pes") == 0 ||
                strstr(ent->d_name, ".o") != NULL) {
                continue;
            }

            int is_tracked = 0;

            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    is_tracked = 1;
                    break;
                }
            }

            if (!is_tracked) {
                struct stat st;
                if (stat(ent->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                    printf("  untracked:  %s\n", ent->d_name);
                    untracked_count++;
                }
            }
        }

        closedir(dir);   // FIXED (was duplicated before)
    }

    if (untracked_count == 0)
        printf("  (nothing to show)\n");

    printf("\n");

    return 0;
}

// ─── IMPLEMENTATIONS ─────────────────────────────────────────────────────────

// Load index
int index_load(Index *index) {
    index->count = 0;

    FILE *fp = fopen(".pes/index", "r");
    if (!fp) return 0; // empty index is fine

    while (1) {
        IndexEntry *e = &index->entries[index->count];

        char hash_hex[HASH_HEX_SIZE + 1];

        int res = fscanf(fp, "%o %s %ld %ld %[^\n]\n",
                         &e->mode,
                         hash_hex,
                         &e->mtime_sec,
                         &e->size,
                         e->path);

        if (res != 5) break;

        hex_to_hash(hash_hex, &e->hash);
        index->count++;
    }

    fclose(fp);
    return 0;
}

// Save index
int index_save(const Index *index) {
    FILE *fp = fopen(".pes/index", "w");
    if (!fp) return -1;

    for (int i = 0; i < index->count; i++) {
        const IndexEntry *e = &index->entries[i];

        char hash_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&e->hash, hash_hex);

        fprintf(fp, "%o %s %ld %ld %s\n",
                e->mode,
                hash_hex,
                e->mtime_sec,
                e->size,
                e->path);
    }

    fclose(fp);
    return 0;
}

// Add file to index
int index_add(Index *index, const char *path) {

    struct stat st;
    if (stat(path, &st) != 0) return -1;

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    char *data = malloc(st.st_size);
    if (!data) {
        fclose(fp);
        return -1;
    }

    fread(data, 1, st.st_size, fp);
    fclose(fp);

    ObjectID hash;
    object_write(OBJ_BLOB, data, st.st_size, &hash);
    free(data);

    IndexEntry *e = index_find(index, path);

    if (!e) {
        e = &index->entries[index->count++];
    }

    e->mode = get_file_mode(path);
    e->hash = hash;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;

    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';

    return index_save(index);
}
// Phase3 commit 1
