#pragma once
int userdb_has_id(const char* path, int id, char* name_out, int name_out_sz, char* role_out, int role_out_sz);
int userdb_max_id(const char* path);
int userdb_append(const char* path, int id, const char* name, const char* role);
