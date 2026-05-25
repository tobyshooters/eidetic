#ifndef DB_H
#define DB_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define MAX_KEY 128
#define MAX_ROWS 64
#define MAX_CELLS 256

#define CELL_PAD 4
#define KEY_HEIGHT 8
#define ROW_GAP 8
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
  VAL_COMMAND,
  VAL_PROCESS,
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
  int args;
  int col;
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
  int label_width;
  float img_scale;
  char filename[256];
} Database;

typedef struct
{
  uint8_t bg[3];
  uint8_t text[3];
  uint8_t text_dim[3];
  uint8_t text_faint[3];
  uint8_t scroll[3];
  uint8_t cursor[3];
  uint8_t command[3];
  uint8_t process[3];
  uint8_t process_done[3];
  uint8_t grid[3];
} Theme;

extern Theme theme;

void
theme_load(char* path);

void
image_alloc(Image* img, int width, int height);
void
image_realloc(Image* img, int width, int height);
void
image_free(Image* img);
void
image_clear(Image* img);
void
image_bounds(Image* img, int* x0, int* y0, int* x1, int* y1);

void
db_init(Database* db);
void
db_free(Database* db);
void
db_render(Database* db);

int
db_set(Database* db, char* key, char* value);
int
db_set_cell(Database* db, char* key, Cell* src);
Cell*
db_get_cell(Database* db, char* key);
int
db_del(Database* db, char* key);
int
db_move_row(Database* db, char* ns, int pos);

Cell*
cell_make_text(char* text);
Cell*
cell_make_num(int n);
Cell*
cell_make_nil(void);
Cell*
cell_copy(Cell* src);
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
Cell*
cell_read_command(char* path);
Cell*
cell_make_process(char* cmd_name, pid_t pid);

char*
resolve_path(char* path, char* out, int outsize);
int
has_ext(char* path, char* ext);

void
db_load_commands(Database* db);
void
db_sync_stack(Database* db, Cell** items, int count);
int
db_load_stack(Database* db, Cell** items, int max_items);

int
process_poll(Cell** items, int count);

#endif
