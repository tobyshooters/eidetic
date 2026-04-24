#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "db.h"

int
is_obj(char* path);
Cell*
cell_read_obj(char* path);
int
cell_write_obj(Cell* cell, char* out_path, char* mtl_path);

#endif
