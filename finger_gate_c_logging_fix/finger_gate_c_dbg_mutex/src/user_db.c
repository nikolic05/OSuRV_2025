#include "user_db.h"
#include <stdio.h>
#include <string.h>

int userdb_has_id(const char* path, int id, char* name_out, int name_out_sz, char* role_out, int role_out_sz) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        int fid = -1;
        char name[128] = {0}, role[64] = {0};
        if (sscanf(line, "%d,%127[^,],%63s", &fid, name, role) == 3) {
            if (fid == id) {
                strncpy(name_out, name, name_out_sz - 1);
                strncpy(role_out, role, role_out_sz - 1);
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

int userdb_max_id(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    int max_id = 0;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        int fid = -1;
        if (sscanf(line, "%d,", &fid) == 1) {
            if (fid > max_id) max_id = fid;
        }
    }
    fclose(f);
    return max_id;
}

int userdb_append(const char* path, int id, const char* name, const char* role) {
    FILE* f = fopen(path, "a");
    if (!f) return 0;
    fprintf(f, "%d,%s,%s\n", id, name, role);
    fclose(f);
    return 1;
}
