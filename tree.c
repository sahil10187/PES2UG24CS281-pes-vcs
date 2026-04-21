// tree.c — Tree object serialization and construction

#include "tree.h"
#include "index.h"
#include "pes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// Forward declaration (implemented in object.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── Mode Constants ─────────────────────────────────────────────────────────

#define MODE_FILE      0100644
#define MODE_EXEC      0100755
#define MODE_DIR       0040000

// ─── PROVIDED ───────────────────────────────────────────────────────────────

uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;

    if (S_ISDIR(st.st_mode))  return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1;

        char mode_str[16] = {0};
        size_t mode_len = space - ptr;
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = strtol(mode_str, NULL, 8);

        ptr = space + 1;

        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1;

        size_t name_len = null_byte - ptr;
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0';

        ptr = null_byte + 1;

        if (ptr + HASH_SIZE > end) return -1;
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

static TreeEntry *tree_find_entry(Tree *tree, const char *name) {
    for (int i = 0; i < tree->count; i++) {
        if (strcmp(tree->entries[i].name, name) == 0) {
            return &tree->entries[i];
        }
    }
    return NULL;
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    size_t max_size = tree->count * 296;
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];

        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += written + 1;

        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ─── IMPLEMENTATION ─────────────────────────────────────────────────────────

static int build_tree_for_prefix(const Index *index, const char *prefix, ObjectID *id_out) {
    Tree tree;
    tree.count = 0;

    size_t prefix_len = strlen(prefix);

    for (int i = 0; i < index->count; i++) {
        const IndexEntry *e = &index->entries[i];
        const char *relative = e->path;

        if (prefix_len > 0) {
            if (strncmp(e->path, prefix, prefix_len) != 0 || e->path[prefix_len] != '/') {
                continue;
            }
            relative = e->path + prefix_len + 1;
        }

        const char *slash = strchr(relative, '/');
        if (!slash) {
            if (tree_find_entry(&tree, relative) != NULL) {
                continue;
            }
            if (tree.count >= MAX_TREE_ENTRIES) return -1;

            TreeEntry *te = &tree.entries[tree.count++];
            snprintf(te->name, sizeof(te->name), "%s", relative);
            te->mode = e->mode;
            te->hash = e->hash;
            continue;
        }

        size_t dir_len = (size_t)(slash - relative);
        if (dir_len == 0 || dir_len >= sizeof(tree.entries[0].name)) {
            return -1;
        }

        char dirname[256];
        memcpy(dirname, relative, dir_len);
        dirname[dir_len] = '\0';

        if (tree_find_entry(&tree, dirname) != NULL) {
            continue;
        }
        if (tree.count >= MAX_TREE_ENTRIES) return -1;

        char child_prefix[512];
        if (prefix_len > 0) {
            if (snprintf(child_prefix, sizeof(child_prefix), "%s/%s", prefix, dirname) >= (int)sizeof(child_prefix)) {
                return -1;
            }
        } else {
            if (snprintf(child_prefix, sizeof(child_prefix), "%s", dirname) >= (int)sizeof(child_prefix)) {
                return -1;
            }
        }

        ObjectID child_id;
        if (build_tree_for_prefix(index, child_prefix, &child_id) != 0) {
            return -1;
        }

        TreeEntry *te = &tree.entries[tree.count++];
        snprintf(te->name, sizeof(te->name), "%s", dirname);
        te->mode = MODE_DIR;
        te->hash = child_id;
    }

    void *data = NULL;
    size_t len = 0;
    if (tree_serialize(&tree, &data, &len) != 0) {
        return -1;
    }

    int rc = object_write(OBJ_TREE, data, len, id_out);
    free(data);
    return rc;
}

int tree_from_index(ObjectID *id_out) {
    Index index;
    if (index_load(&index) != 0) {
        return -1;
    }
    return build_tree_for_prefix(&index, "", id_out);
}
// p2 start
// p2 traversal
// p2 link
// p2 fix
// p2 done
