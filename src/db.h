#ifndef DB_H
#define DB_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_KEY 128
#define MAX_ROWS 64
#define MAX_CELLS 256

#define CELL_PAD 2
#define CELL_GAP 1
#define KEY_HEIGHT 8
#define ROW_GAP 2
#define ROW_DEFAULT_WIDTH 320

typedef struct
{
  int width, height;
  int alloc_width, alloc_height;
  uint8_t* data;
} Image;

typedef enum
{
  VAL_TEXT,
  VAL_NUM,
  VAL_IMAGE,
  VAL_NIL
} ValType;

typedef struct
{
  char key[MAX_KEY];
  char value[MAX_KEY];
  int num;
  ValType type;
  uint8_t* img_data;
  int img_width, img_height;
  int col;
  int line;
  int width;
  bool tombstone;
} Cell;

typedef struct
{
  char ns[MAX_KEY];
  int y_offset;
  int height;
  Cell cells[MAX_CELLS];
  int cell_count;
} Row;

typedef struct
{
  Image img;
  Row rows[MAX_ROWS];
  int row_count;
  int max_width;
  char filename[256];
} Database;

void
db_reflow(Database* db);

void
image_alloc(Image* img, int width, int height);
void
image_realloc(Image* img, int width, int height);
void
image_free(Image* img);
void
image_clear(Image* img);

void
db_init(Database* db);
void
db_free(Database* db);

int
db_set(Database* db, char* key, char* value);
int
db_set_cell(Database* db, char* key, Cell* src);
char*
db_get(Database* db, char* key);
Cell*
db_get_cell(Database* db, char* key);
int
db_del(Database* db, char* key);

Cell*
cell_make_text(char* text);
Cell*
cell_make_num(int n);
Cell*
cell_make_nil(void);
void
cell_free_temp(Cell* cell);
int
cell_to_num(Cell* cell);

int
db_save(Database* db, char* filename);
int
db_load(Database* db, char* filename);

Cell*
cell_read_image(char* path);

#endif
