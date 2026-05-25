#define _POSIX_C_SOURCE 200809L
#include "db.h"
#include "audio.h"
#include "geometry.h"
#include "glyph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#define IMG_DIR "images"

Theme theme = {
  .bg         = { 255, 255, 255 },
  .text       = {   0,   0,   0 },
  .text_dim   = {  60,  60,  60 },
  .text_faint = { 120, 120, 120 },
  .scroll     = { 180, 180, 180 },
  .cursor     = {   0,  56, 255 },
  .command    = {   0,  56, 255 },
  .process    = { 200, 120,   0 },
  .process_done = {  0, 160,  0 },
  .grid       = { 200, 220, 255 },
};

void
theme_load(char* path)
{
  FILE* f = fopen(path, "r");
  if (!f) {
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char name[64];
    int r, g, b;
    if (sscanf(line, "%63s %d %d %d", name, &r, &g, &b) != 4) {
      continue;
    }
    uint8_t* dst = NULL;
    if (strcmp(name, "bg") == 0) { dst = theme.bg; }
    else if (strcmp(name, "text") == 0) { dst = theme.text; }
    else if (strcmp(name, "text_dim") == 0) { dst = theme.text_dim; }
    else if (strcmp(name, "text_faint") == 0) { dst = theme.text_faint; }
    else if (strcmp(name, "scroll") == 0) { dst = theme.scroll; }
    else if (strcmp(name, "cursor") == 0) { dst = theme.cursor; }
    else if (strcmp(name, "command") == 0) { dst = theme.command; }
    else if (strcmp(name, "process") == 0) { dst = theme.process; }
    else if (strcmp(name, "process_done") == 0) { dst = theme.process_done; }
    else if (strcmp(name, "grid") == 0) { dst = theme.grid; }
    if (dst) {
      dst[0] = (uint8_t)r;
      dst[1] = (uint8_t)g;
      dst[2] = (uint8_t)b;
    }
  }
  fclose(f);
}

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static void
fill_bg(uint8_t* data, int npixels)
{
  for (int i = 0; i < npixels; i++) {
    data[i * 3] = theme.bg[0];
    data[i * 3 + 1] = theme.bg[1];
    data[i * 3 + 2] = theme.bg[2];
  }
}

void
image_alloc(Image* img, int width, int height)
{
  img->width = width;
  img->height = height;
  img->alloc_width = width * 2;
  img->alloc_height = height * 2;
  img->data = calloc(img->alloc_width * img->alloc_height * 3, 1);
  fill_bg(img->data, img->alloc_width * img->alloc_height);
}

void
image_realloc(Image* img, int width, int height)
{
  if (width <= img->alloc_width && height <= img->alloc_height) {
    int old_width = img->width;
    int old_height = img->height;

    if (width > old_width) {
      for (int y = 0; y < old_height; y++) {
        for (int x = old_width; x < width; x++) {
          int idx = (y * img->alloc_width + x) * 3;
          img->data[idx] = theme.bg[0];
          img->data[idx + 1] = theme.bg[1];
          img->data[idx + 2] = theme.bg[2];
        }
      }
    }
    if (height > old_height) {
      for (int y = old_height; y < height; y++) {
        for (int x = 0; x < width; x++) {
          int idx = (y * img->alloc_width + x) * 3;
          img->data[idx] = theme.bg[0];
          img->data[idx + 1] = theme.bg[1];
          img->data[idx + 2] = theme.bg[2];
        }
      }
    }
    img->width = width;
    img->height = height;
    return;
  }

  int new_alloc_w = MAX(img->alloc_width, width) * 2;
  int new_alloc_h = MAX(img->alloc_height, height) * 2;
  uint8_t* new_data = calloc(new_alloc_w * new_alloc_h * 3, 1);
  fill_bg(new_data, new_alloc_w * new_alloc_h);

  for (int y = 0; y < img->height; y++) {
    for (int x = 0; x < img->width; x++) {
      int old_idx = (y * img->alloc_width + x) * 3;
      int new_idx = (y * new_alloc_w + x) * 3;
      new_data[new_idx] = img->data[old_idx];
      new_data[new_idx + 1] = img->data[old_idx + 1];
      new_data[new_idx + 2] = img->data[old_idx + 2];
    }
  }

  free(img->data);
  img->data = new_data;
  img->width = width;
  img->height = height;
  img->alloc_width = new_alloc_w;
  img->alloc_height = new_alloc_h;
}

void
image_free(Image* img)
{
  free(img->data);
  img->data = NULL;
}

void
image_clear(Image* img)
{
  fill_bg(img->data, img->alloc_width * img->alloc_height);
}

void
image_bounds(Image* img, int* x0, int* y0, int* x1, int* y1)
{
  *x0 = img->width;
  *y0 = img->height;
  *x1 = 0;
  *y1 = 0;
  for (int y = 0; y < img->height; y++) {
    for (int x = 0; x < img->width; x++) {
      int idx = (y * img->alloc_width + x) * 3;
      if (img->data[idx] < 255 || img->data[idx + 1] < 255 ||
          img->data[idx + 2] < 255) {
        if (x < *x0) { *x0 = x; }
        if (y < *y0) { *y0 = y; }
        if (x > *x1) { *x1 = x; }
        if (y > *y1) { *y1 = y; }
      }
    }
  }
  if (*x1 < *x0) {
    *x0 = *y0 = 0;
    *x1 = 1;
    *y1 = 1;
  }
}

static int
text_width(char* text)
{
  return strlen(text) * (FONT_WIDTH + 1);
}

static int
ns_label_width(Row* row)
{
  return text_width(row->ns) + CELL_PAD;
}

static int
img_scaled(int dim, float scale)
{
  int s = (int)(dim * scale);
  return s > 0 ? s : 1;
}

#define PATH_HEIGHT (FONT_HEIGHT + 2)

static int
grid_align(int v, int grid)
{
  return ((v + grid - 1) / grid) * grid;
}

static int
cell_val_height(Cell* cell, float scale)
{
  int gy = FONT_HEIGHT + 2;
  if (cell->type == VAL_IMAGE || cell->type == VAL_COMMAND) {
    return PATH_HEIGHT + grid_align(img_scaled(cell->img_height, scale), gy);
  }
  if (cell->type == VAL_PROCESS) {
    return gy * 3;
  }
  return gy;
}

static int
cell_height(Cell* cell, float scale)
{
  return KEY_HEIGHT + cell_val_height(cell, scale);
}

static int
row_height(Row* row, float scale)
{
  int h = KEY_HEIGHT + FONT_HEIGHT + 2;
  for (int i = 0; i < row->cell_count; i++) {
    if (!row->cells[i].tombstone) {
      int ch = cell_height(&row->cells[i], scale);
      if (ch > h)
        h = ch;
    }
  }
  return h;
}

static int
cell_display_width(Cell* cell, float scale)
{
  int vw;
  if (cell->type == VAL_IMAGE || cell->type == VAL_COMMAND) {
    int pw = text_width(cell->value);
    int iw = img_scaled(cell->img_width, scale);
    vw = MAX(pw, iw);
  } else if (cell->type == VAL_PROCESS) {
    char pid_buf[32];
    snprintf(pid_buf, sizeof(pid_buf), "%d", cell->args);
    char state[MAX_KEY] = "";
    char cmd_name[MAX_KEY] = "";
    char* tab = strchr(cell->value, '\t');
    if (tab) {
      int slen = tab - cell->value;
      if (slen >= MAX_KEY) slen = MAX_KEY - 1;
      memcpy(state, cell->value, slen);
      state[slen] = '\0';
      snprintf(cmd_name, MAX_KEY, "%s", tab + 1);
    } else {
      snprintf(state, MAX_KEY, "%s", cell->value);
    }
    vw = text_width(pid_buf);
    int sw = text_width(state);
    int cw = text_width(cmd_name);
    if (sw > vw) vw = sw;
    if (cw > vw) vw = cw;
  } else {
    vw = text_width(cell->value);
  }
  return MAX(text_width(cell->key), vw) + CELL_PAD * 2;
}

static void
layout_row(Row* row, int max_width, int label_w, float scale)
{
  (void)max_width;
  int col = label_w;

  for (int i = 0; i < row->cell_count; i++) {
    if (row->cells[i].tombstone) {
      continue;
    }
    row->cells[i].width = cell_display_width(&row->cells[i], scale);
    row->cells[i].col = col;
    col = row->cells[i].col + row->cells[i].width;
  }
}

static void
cell_free_internal(Cell* cell)
{
  if (cell->img_data) {
    free(cell->img_data);
    cell->img_data = NULL;
  }
}

Cell*
cell_make_text(char* text)
{
  Cell* c = calloc(1, sizeof(Cell));
  c->type = VAL_TEXT;
  strncpy(c->value, text, MAX_KEY - 1);
  return c;
}

Cell*
cell_make_num(int n)
{
  Cell* c = calloc(1, sizeof(Cell));
  c->type = VAL_NUM;
  c->num = n;
  sprintf(c->value, "%d", n);
  return c;
}

Cell*
cell_make_nil(void)
{
  Cell* c = calloc(1, sizeof(Cell));
  c->type = VAL_NIL;
  return c;
}

void
cell_free_temp(Cell* cell)
{
  if (cell) {
    cell_free_internal(cell);
    free(cell);
  }
}

Cell*
cell_copy(Cell* src)
{
  Cell* c = calloc(1, sizeof(Cell));
  memcpy(c, src, sizeof(Cell));
  c->img_data = NULL;
  if (src->img_data) {
    int sz = src->img_width * src->img_height * 3;
    c->img_data = malloc(sz);
    memcpy(c->img_data, src->img_data, sz);
  }
  return c;
}

int
cell_to_num(Cell* cell)
{
  if (cell->type == VAL_NUM) {
    return cell->num;
  }
  return atoi(cell->value);
}

static void
cell_copy_value(Cell* dst, Cell* src)
{
  strcpy(dst->value, src->value);
  dst->num = src->num;
  dst->type = src->type;
  dst->args = src->args;
  dst->img_width = src->img_width;
  dst->img_height = src->img_height;
  if (src->img_data) {
    int sz = src->img_width * src->img_height * 3;
    dst->img_data = malloc(sz);
    memcpy(dst->img_data, src->img_data, sz);
  } else {
    dst->img_data = NULL;
  }
}

void
db_init(Database* db)
{
  memset(db, 0, sizeof(Database));
  db->max_width = ROW_DEFAULT_WIDTH;
  db->img_scale = 1.0f;
  image_alloc(&db->img, db->max_width, 64);
  strcpy(db->filename, "db.png");
}

void
db_free(Database* db)
{
  for (int i = 0; i < db->row_count; i++) {
    for (int j = 0; j < db->rows[i].cell_count; j++) {
      cell_free_internal(&db->rows[i].cells[j]);
    }
  }
  image_free(&db->img);
}

static int
ns_is_under(char* ns, char* prefix)
{
  int len = strlen(prefix);
  if (strcmp(ns, prefix) == 0) {
    return 1;
  }
  if (strncmp(ns, prefix, len) == 0 && ns[len] == '.') {
    return 1;
  }
  return 0;
}

static void
parse_key(char* key, char* ns, char* local)
{
  char* dot = strrchr(key, '.');
  if (dot) {
    int ns_len = dot - key;
    strncpy(ns, key, ns_len);
    ns[ns_len] = '\0';
    strcpy(local, dot + 1);
  } else {
    strcpy(ns, "home");
    strcpy(local, key);
  }
}

static void
recompute_row_offsets(Database* db)
{
  int max_lw = 0;
  for (int i = 0; i < db->row_count; i++) {
    int lw = ns_label_width(&db->rows[i]);
    if (lw > max_lw) {
      max_lw = lw;
    }
  }
  db->label_width = max_lw;

  int y = 0;
  for (int i = 0; i < db->row_count; i++) {
    layout_row(&db->rows[i], db->max_width, db->label_width, db->img_scale);
    db->rows[i].y_offset = y;
    db->rows[i].height = row_height(&db->rows[i], db->img_scale);
    y += db->rows[i].height + ROW_GAP;
  }
  if (y > db->img.height) {
    image_realloc(&db->img, db->img.width, y);
  }
}

static Row*
find_or_create_row(Database* db, char* ns)
{
  for (int i = 0; i < db->row_count; i++) {
    if (strcmp(db->rows[i].ns, ns) == 0) {
      return &db->rows[i];
    }
  }

  for (int i = db->row_count; i > 0; i--) {
    db->rows[i] = db->rows[i - 1];
  }
  db->row_count++;

  Row* row = &db->rows[0];
  memset(row, 0, sizeof(Row));
  strcpy(row->ns, ns);
  row->height = KEY_HEIGHT + FONT_HEIGHT + 2;

  recompute_row_offsets(db);
  return row;
}

static Cell*
find_cell(Row* row, char* local)
{
  for (int i = 0; i < row->cell_count; i++) {
    if (!row->cells[i].tombstone && strcmp(row->cells[i].key, local) == 0) {
      return &row->cells[i];
    }
  }
  return NULL;
}

static int
row_end(Row* row)
{
  int end = 0;
  for (int i = 0; i < row->cell_count; i++) {
    if (!row->cells[i].tombstone) {
      int cell_end = row->cells[i].col + row->cells[i].width;
      if (cell_end > end) {
        end = cell_end;
      }
    }
  }
  return end;
}

static void
render_cell(Image* img, Cell* cell, int y_offset, float scale)
{
  int key_y = y_offset + 1;
  int val_y = y_offset + KEY_HEIGHT + 1;
  uint8_t* kc = theme.text;
  if (cell->type == VAL_COMMAND) { kc = theme.command; }
  else if (cell->type == VAL_PROCESS) {
    kc = cell->num > 0 ? theme.process : theme.process_done;
  }
  write_text(img->data, img->alloc_width, cell->key, cell->col + 1, key_y,
             kc[0], kc[1], kc[2]);

  if ((cell->type == VAL_IMAGE || cell->type == VAL_COMMAND) && cell->img_data) {
    write_text(img->data, img->alloc_width, cell->value, cell->col + 1, val_y,
               theme.text_dim[0], theme.text_dim[1], theme.text_dim[2]);
    int dw = img_scaled(cell->img_width, scale);
    int dh = img_scaled(cell->img_height, scale);
    int ox = cell->col + 1;
    int oy = val_y + PATH_HEIGHT;
    for (int dy = 0; dy < dh; dy++) {
      for (int dx = 0; dx < dw; dx++) {
        int sx = (int)(dx / scale);
        int sy = (int)(dy / scale);
        if (sx >= cell->img_width) { sx = cell->img_width - 1; }
        if (sy >= cell->img_height) { sy = cell->img_height - 1; }
        int src = (sy * cell->img_width + sx) * 3;
        int px = ox + dx;
        int py = oy + dy;
        if (px < img->alloc_width && py < img->alloc_height) {
          int dst = (py * img->alloc_width + px) * 3;
          img->data[dst] = cell->img_data[src];
          img->data[dst + 1] = cell->img_data[src + 1];
          img->data[dst + 2] = cell->img_data[src + 2];
        }
      }
    }
  } else if (cell->type == VAL_PROCESS) {
    int line_h = FONT_HEIGHT + 2;
    char pid_buf[32];
    snprintf(pid_buf, sizeof(pid_buf), "%d", cell->args);
    char state[MAX_KEY] = "";
    char cmd_name[MAX_KEY] = "";
    char* tab = strchr(cell->value, '\t');
    if (tab) {
      int slen = tab - cell->value;
      if (slen >= MAX_KEY) slen = MAX_KEY - 1;
      memcpy(state, cell->value, slen);
      state[slen] = '\0';
      snprintf(cmd_name, MAX_KEY, "%s", tab + 1);
    } else {
      snprintf(state, MAX_KEY, "%s", cell->value);
    }
    uint8_t* pc = cell->num > 0 ? theme.process : theme.process_done;
    write_text(img->data, img->alloc_width, pid_buf, cell->col + 1, val_y,
               theme.text_dim[0], theme.text_dim[1], theme.text_dim[2]);
    write_text(img->data, img->alloc_width, state, cell->col + 1,
               val_y + line_h, pc[0], pc[1], pc[2]);
    write_text(img->data, img->alloc_width, cmd_name, cell->col + 1,
               val_y + line_h * 2,
               theme.text_faint[0], theme.text_faint[1], theme.text_faint[2]);
  } else {
    write_text(img->data, img->alloc_width, cell->value, cell->col + 1, val_y,
               theme.text_dim[0], theme.text_dim[1], theme.text_dim[2]);
  }
}

static void
render_row(Database* db, Row* row)
{
  float scale = db->img_scale;
  layout_row(row, db->max_width, db->label_width, scale);

  int rh = row_height(row, scale);
  int needed_w = row_end(row) + CELL_PAD;
  if (needed_w < db->max_width) {
    needed_w = db->max_width;
  }
  if (needed_w > db->img.width) {
    image_realloc(&db->img, needed_w, db->img.height);
  }

  int needed_h = row->y_offset + rh;
  if (needed_h > db->img.height) {
    image_realloc(&db->img, db->img.width, needed_h);
  }

  for (int y = row->y_offset; y < row->y_offset + rh && y < db->img.alloc_height; y++) {
    for (int x = 0; x < db->img.alloc_width; x++) {
      int idx = (y * db->img.alloc_width + x) * 3;
      db->img.data[idx] = theme.bg[0];
      db->img.data[idx + 1] = theme.bg[1];
      db->img.data[idx + 2] = theme.bg[2];
    }
  }

  int label_y = row->y_offset + 1;
  write_text(db->img.data, db->img.alloc_width, row->ns, 1, label_y,
             theme.text_faint[0], theme.text_faint[1], theme.text_faint[2]);

  for (int i = 0; i < row->cell_count; i++) {
    if (!row->cells[i].tombstone) {
      render_cell(&db->img, &row->cells[i], row->y_offset, scale);
    }
  }

  if (strcmp(row->ns, "stack") == 0 && row->cell_count > 0) {
    int val_y = row->y_offset + KEY_HEIGHT + 1;
    for (int i = 0; i < row->cell_count; i++) {
      if (row->cells[i].tombstone) {
        continue;
      }
      ValType vt = row->cells[i].type;
      if (vt == VAL_IMAGE || vt == VAL_COMMAND || vt == VAL_PROCESS) {
        continue;
      }
      float t = (float)(row->cell_count - 1 - i) / (float)row->cell_count;
      uint8_t r = theme.text_dim[0] + (uint8_t)(t * (theme.bg[0] - theme.text_dim[0]));
      uint8_t g = theme.text_dim[1] + (uint8_t)(t * (theme.bg[1] - theme.text_dim[1]));
      uint8_t b = theme.text_dim[2] + (uint8_t)(t * (theme.bg[2] - theme.text_dim[2]));
      write_text(db->img.data, db->img.alloc_width,
                 row->cells[i].value, row->cells[i].col + 1, val_y,
                 r, g, b);
    }
  }
}

static void
render_all(Database* db)
{
  image_clear(&db->img);
  recompute_row_offsets(db);
  for (int i = 0; i < db->row_count; i++) {
    render_row(db, &db->rows[i]);
  }
}

void
db_render(Database* db)
{
  if (db->row_count > 0) {
    render_all(db);
  }
}

char*
resolve_path(char* path, char* out, int outsize)
{
  struct stat st;
  if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
    snprintf(out, outsize, "%s", path);
    return out;
  }
  snprintf(out, outsize, "%s/%s", IMG_DIR, path);
  if (stat(out, &st) == 0 && S_ISREG(st.st_mode)) {
    return out;
  }
  return NULL;
}

#define TEXT_WRAP 79

static Cell*
cell_read_text_file(char* path)
{
  char resolved[512];
  if (!resolve_path(path, resolved, sizeof(resolved))) {
    return NULL;
  }
  FILE* f = fopen(resolved, "r");
  if (!f) {
    return NULL;
  }

  char wrapped[1024][TEXT_WRAP + 1];
  int nlines = 0;
  char raw[1024];

  while (nlines < 1024 && fgets(raw, sizeof(raw), f)) {
    int len = strlen(raw);
    if (len > 0 && raw[len - 1] == '\n') {
      raw[--len] = '\0';
    }

    if (len == 0) {
      wrapped[nlines][0] = '\0';
      nlines++;
      continue;
    }

    int pos = 0;
    while (pos < len && nlines < 1024) {
      int chunk = len - pos;
      if (chunk <= TEXT_WRAP) {
        memcpy(wrapped[nlines], raw + pos, chunk);
        wrapped[nlines][chunk] = '\0';
        nlines++;
        break;
      }
      int brk = TEXT_WRAP;
      while (brk > 0 && raw[pos + brk] != ' ') {
        brk--;
      }
      if (brk == 0) {
        brk = TEXT_WRAP;
      }
      memcpy(wrapped[nlines], raw + pos, brk);
      wrapped[nlines][brk] = '\0';
      nlines++;
      pos += brk;
      if (raw[pos] == ' ') {
        pos++;
      }
    }
  }
  fclose(f);

  if (nlines == 0) {
    return NULL;
  }

  int line_h = FONT_HEIGHT + 2;
  int w = TEXT_WRAP * (FONT_WIDTH + 1) + 1;
  int h = nlines * line_h + 1;
  uint8_t* pixels = malloc(w * h * 3);
  memset(pixels, 255, w * h * 3);

  for (int i = 0; i < nlines; i++) {
    write_text(pixels, w, wrapped[i], 0, i * line_h,
               theme.text[0], theme.text[1], theme.text[2]);
  }

  Cell* c = calloc(1, sizeof(Cell));
  c->type = VAL_IMAGE;
  c->img_data = pixels;
  c->img_width = w;
  c->img_height = h;
  snprintf(c->value, MAX_KEY, "%s", path);
  return c;
}

int
has_ext(char* path, char* ext)
{
  char* dot = strrchr(path, '.');
  return dot && strcasecmp(dot, ext) == 0;
}

Cell*
cell_read_image(char* path)
{
  if (is_audio(path)) {
    return cell_read_audio(path);
  }

  if (is_obj(path)) {
    return cell_read_obj(path);
  }

  if (has_ext(path, ".sh")) {
    return cell_read_command(path);
  }

  if (has_ext(path, ".txt") || has_ext(path, ".md")) {
    return cell_read_text_file(path);
  }

  char resolved[512];
  if (!resolve_path(path, resolved, sizeof(resolved))) {
    return cell_read_text_file(path);
  }
  int w, h, channels;
  uint8_t* pixels = stbi_load(resolved, &w, &h, &channels, 3);
  if (!pixels) {
    return cell_read_text_file(path);
  }

  Cell* c = calloc(1, sizeof(Cell));
  c->type = VAL_IMAGE;
  c->img_data = pixels;
  c->img_width = w;
  c->img_height = h;
  snprintf(c->value, MAX_KEY, "%s", path);
  return c;
}

Cell*
cell_read_command(char* path)
{
  char resolved[512];
  if (!resolve_path(path, resolved, sizeof(resolved))) {
    return NULL;
  }
  FILE* f = fopen(resolved, "r");
  if (!f) {
    return NULL;
  }

  int max_arg = 0;
  char wrapped[1024][TEXT_WRAP + 1];
  int nlines = 0;
  char raw[1024];

  while (nlines < 1024 && fgets(raw, sizeof(raw), f)) {
    int len = strlen(raw);
    if (len > 0 && raw[len - 1] == '\n') {
      raw[--len] = '\0';
    }

    for (int ci = 0; ci < len - 1; ci++) {
      if (raw[ci] != '$') {
        continue;
      }
      char d = raw[ci + 1];
      if (d == '{' && ci + 2 < len && raw[ci + 2] >= '1' && raw[ci + 2] <= '9') {
        d = raw[ci + 2];
      }
      if (d >= '1' && d <= '9') {
        int n = d - '0';
        if (n > max_arg) {
          max_arg = n;
        }
      }
    }

    if (len == 0) {
      wrapped[nlines][0] = '\0';
      nlines++;
      continue;
    }

    int pos = 0;
    while (pos < len && nlines < 1024) {
      int chunk = len - pos;
      if (chunk <= TEXT_WRAP) {
        memcpy(wrapped[nlines], raw + pos, chunk);
        wrapped[nlines][chunk] = '\0';
        nlines++;
        break;
      }
      int brk = TEXT_WRAP;
      while (brk > 0 && raw[pos + brk] != ' ') {
        brk--;
      }
      if (brk == 0) {
        brk = TEXT_WRAP;
      }
      memcpy(wrapped[nlines], raw + pos, brk);
      wrapped[nlines][brk] = '\0';
      nlines++;
      pos += brk;
      if (raw[pos] == ' ') {
        pos++;
      }
    }
  }
  fclose(f);

  if (nlines == 0) {
    return NULL;
  }

  int max_len = 0;
  for (int i = 0; i < nlines; i++) {
    int len = strlen(wrapped[i]);
    if (len > max_len) {
      max_len = len;
    }
  }
  if (max_len == 0) {
    max_len = 1;
  }

  int line_h = FONT_HEIGHT + 2;
  int w = max_len * (FONT_WIDTH + 1) + 1;
  int h = nlines * line_h + 1;
  uint8_t* pix = malloc(w * h * 3);
  memset(pix, 255, w * h * 3);

  for (int i = 0; i < nlines; i++) {
    write_text(pix, w, wrapped[i], 0, i * line_h,
               theme.text[0], theme.text[1], theme.text[2]);
  }

  Cell* c = calloc(1, sizeof(Cell));
  c->type = VAL_COMMAND;
  c->img_data = pix;
  c->img_width = w;
  c->img_height = h;
  c->args = max_arg;
  snprintf(c->value, MAX_KEY, "%s", path);
  return c;
}

Cell*
cell_make_process(char* cmd_name, pid_t pid)
{
  Cell* c = calloc(1, sizeof(Cell));
  c->type = VAL_PROCESS;
  c->args = (int)pid;
  snprintf(c->value, MAX_KEY, "running\t%s", cmd_name);
  strncpy(c->key, cmd_name, MAX_KEY - 1);
  return c;
}

int
db_set(Database* db, char* key, char* value)
{
  char ns[MAX_KEY], local[MAX_KEY];
  parse_key(key, ns, local);

  Row* row = find_or_create_row(db, ns);
  Cell* cell = find_cell(row, local);

  if (!cell) {
    cell = &row->cells[row->cell_count++];
    memset(cell, 0, sizeof(Cell));
    strcpy(cell->key, local);
    cell->col = row_end(row);
  }

  cell_free_internal(cell);

  char* end;
  long n = strtol(value, &end, 10);
  if (*end == '\0' && end != value) {
    cell->type = VAL_NUM;
    cell->num = (int)n;
  } else {
    cell->type = VAL_TEXT;
  }
  strcpy(cell->value, value);
  cell->width = MAX(text_width(local), text_width(value)) + CELL_PAD * 2;

  render_all(db);
  return 0;
}

int
db_set_cell(Database* db, char* key, Cell* src)
{
  char ns[MAX_KEY], local[MAX_KEY];
  parse_key(key, ns, local);

  Row* row = find_or_create_row(db, ns);
  Cell* cell = find_cell(row, local);

  if (!cell) {
    cell = &row->cells[row->cell_count++];
    memset(cell, 0, sizeof(Cell));
    strcpy(cell->key, local);
    cell->col = row_end(row);
  }

  cell_free_internal(cell);
  cell_copy_value(cell, src);

  int vw = (cell->type == VAL_IMAGE) ? cell->img_width
                                     : text_width(cell->value);
  cell->width = MAX(text_width(local), vw) + CELL_PAD * 2;

  render_all(db);
  return 0;
}

Cell*
db_get_cell(Database* db, char* key)
{
  char ns[MAX_KEY], local[MAX_KEY];
  parse_key(key, ns, local);

  for (int i = 0; i < db->row_count; i++) {
    if (strcmp(db->rows[i].ns, ns) != 0) {
      continue;
    }
    return find_cell(&db->rows[i], local);
  }
  return NULL;
}

static int
db_del_row(Database* db, char* prefix)
{
  int found = 0;
  for (int i = 0; i < db->row_count;) {
    if (!ns_is_under(db->rows[i].ns, prefix)) {
      i++;
      continue;
    }
    for (int j = 0; j < db->rows[i].cell_count; j++) {
      cell_free_internal(&db->rows[i].cells[j]);
    }
    for (int j = i; j < db->row_count - 1; j++) {
      db->rows[j] = db->rows[j + 1];
    }
    db->row_count--;
    found = 1;
  }
  if (found) {
    render_all(db);
  }
  return found ? 0 : -1;
}

int
db_del(Database* db, char* key)
{
  // No dot: try deleting from home first, then try as namespace
  if (!strchr(key, '.')) {
    char ns[] = "home";
    for (int i = 0; i < db->row_count; i++) {
      if (strcmp(db->rows[i].ns, ns) != 0) {
        continue;
      }
      Cell* cell = find_cell(&db->rows[i], key);
      if (cell) {
        cell_free_internal(cell);
        cell->tombstone = true;
        render_all(db);
        return 0;
      }
    }
    return db_del_row(db, key);
  }

  char ns[MAX_KEY], local[MAX_KEY];
  parse_key(key, ns, local);

  for (int i = 0; i < db->row_count; i++) {
    if (strcmp(db->rows[i].ns, ns) != 0) {
      continue;
    }
    Cell* cell = find_cell(&db->rows[i], local);
    if (!cell) {
      return -1;
    }
    cell_free_internal(cell);
    cell->tombstone = true;
    render_all(db);
    return 0;
  }
  return -1;
}

int
db_move_row(Database* db, char* ns, int pos)
{
  int src = -1;
  for (int i = 0; i < db->row_count; i++) {
    if (strcmp(db->rows[i].ns, ns) == 0) {
      src = i;
      break;
    }
  }
  if (src < 0) {
    return -1;
  }
  int offset = 0;
  if (db->row_count > 0 && strcmp(db->rows[0].ns, "stack") == 0) {
    offset = 1;
  }
  pos += offset;
  if (pos < offset) {
    pos = offset;
  }
  if (pos >= db->row_count) {
    pos = db->row_count - 1;
  }
  if (src == pos) {
    return 0;
  }

  Row tmp = db->rows[src];
  if (src < pos) {
    for (int i = src; i < pos; i++) {
      db->rows[i] = db->rows[i + 1];
    }
  } else {
    for (int i = src; i > pos; i--) {
      db->rows[i] = db->rows[i - 1];
    }
  }
  db->rows[pos] = tmp;
  render_all(db);
  return 0;
}

static int
save_png(char* path, uint8_t* data, int width, int height, int stride)
{
  uint8_t* packed = malloc(width * height * 3);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int src = (y * stride + x) * 3;
      int dst = (y * width + x) * 3;
      packed[dst] = data[src];
      packed[dst + 1] = data[src + 1];
      packed[dst + 2] = data[src + 2];
    }
  }
  int ok = stbi_write_png(path, width, height, 3, packed, width * 3);
  free(packed);
  return ok ? 0 : -1;
}

int
db_save(Database* db, char* filename)
{
  if (filename) {
    strcpy(db->filename, filename);
  }

  float saved_scale = db->img_scale;
  if (db->img_scale != 1.0f) {
    db->img_scale = 1.0f;
    render_all(db);
  }

  mkdir(IMG_DIR, 0755);

  Image* img = &db->img;
  int x0, y0, x1, y1;
  image_bounds(img, &x0, &y0, &x1, &y1);

  int pad = 2;
  x0 = x0 > pad ? x0 - pad : 0;
  y0 = y0 > pad ? y0 - pad : 0;
  x1 = x1 + pad < img->width ? x1 + pad : img->width - 1;
  y1 = y1 + pad < img->height ? y1 + pad : img->height - 1;

  int tw = x1 - x0 + 1;
  int th = y1 - y0 + 1;

  char path[512];
  snprintf(path, sizeof(path), "%s/%s", IMG_DIR, db->filename);

  int result = save_png(path, img->data + (y0 * img->alloc_width + x0) * 3,
                        tw, th, img->alloc_width);

  if (saved_scale != 1.0f) {
    db->img_scale = saved_scale;
    render_all(db);
  }

  return result;
}

static int
is_dark(uint8_t* data, int alloc_width, int x, int y)
{
  int idx = (y * alloc_width + x) * 3;
  int thr = 55;
  return abs(data[idx] - theme.bg[0]) > thr ||
         abs(data[idx + 1] - theme.bg[1]) > thr ||
         abs(data[idx + 2] - theme.bg[2]) > thr;
}

static void
db_reconstruct(Database* db)
{
  Image* img = &db->img;
  db->row_count = 0;

  int row_y[MAX_ROWS];
  char row_ns[MAX_ROWS][MAX_KEY];
  int nrows = 0;

  for (int y = 0; y + FONT_HEIGHT <= img->height && nrows < MAX_ROWS; y++) {
    char* text = read_text(img->data, img->alloc_width, img->height, 1, y);
    if (!text) {
      continue;
    }
    row_y[nrows] = y;
    strncpy(row_ns[nrows], text, MAX_KEY - 1);
    row_ns[nrows][MAX_KEY - 1] = '\0';
    nrows++;
    int sy = y + KEY_HEIGHT + FONT_HEIGHT + 2;
    int clear_run = 0;
    int found_gap = 0;
    for (; sy < img->height; sy++) {
      int row_clear = 1;
      for (int sx = 0; sx < img->width; sx++) {
        if (is_dark(img->data, img->alloc_width, sx, sy)) {
          row_clear = 0;
          break;
        }
      }
      if (row_clear) {
        clear_run++;
      } else {
        if (found_gap) {
          break;
        }
        clear_run = 0;
      }
      if (clear_run >= ROW_GAP) {
        found_gap = 1;
      }
    }
    y = sy - 2;
  }

  if (nrows == 0) {
    return;
  }

  int max_lw = 0;
  for (int i = 0; i < nrows; i++) {
    int lw = text_width(row_ns[i]) + CELL_PAD;
    if (lw > max_lw) {
      max_lw = lw;
    }
  }
  db->label_width = max_lw;

  for (int r = 0; r < nrows; r++) {
    Row* row = &db->rows[db->row_count++];
    memset(row, 0, sizeof(Row));
    strcpy(row->ns, row_ns[r]);
    row->y_offset = row_y[r] - 1;

    int row_bottom;
    if (r + 1 < nrows) {
      row_bottom = row_y[r + 1] - 1 - ROW_GAP;
    } else {
      row_bottom = img->height;
    }
    row->height = row_bottom - row->y_offset;

    int key_y = row_y[r];
    int val_y = row->y_offset + KEY_HEIGHT + 1;

    int key_x[MAX_CELLS];
    char key_name[MAX_CELLS][MAX_KEY];
    int nkeys = 0;

    int x = max_lw;
    while (x + FONT_WIDTH <= img->width && nkeys < MAX_CELLS) {
      char* key = read_text(img->data, img->alloc_width, img->height, x, key_y);
      if (!key) {
        x++;
        continue;
      }
      key_x[nkeys] = x;
      strncpy(key_name[nkeys], key, MAX_KEY - 1);
      key_name[nkeys][MAX_KEY - 1] = '\0';
      nkeys++;
      x += text_width(key);
    }

    for (int k = 0; k < nkeys; k++) {
      Cell* cell = &row->cells[row->cell_count];
      memset(cell, 0, sizeof(Cell));
      strcpy(cell->key, key_name[k]);
      cell->col = key_x[k] - 1;

      int kw = text_width(key_name[k]);
      int vx = key_x[k];
      int vy = val_y;

      int cell_px;
      if (k + 1 < nkeys) {
        cell_px = key_x[k + 1] - cell->col - CELL_PAD;
      } else {
        cell_px = img->width - vx;
      }

      char* val = read_text_n(img->data, img->alloc_width, img->height,
                              vx, vy, cell_px);
      if (val && val[0]) {
        strncpy(cell->value, val, MAX_KEY - 1);

        int img_y = vy + PATH_HEIGHT;
        int has_pixels = 0;
        int cell_right;
        if (k + 1 < nkeys) {
          cell_right = key_x[k + 1];
        } else {
          cell_right = img->width;
        }
        for (int sy = img_y; sy < row_bottom && sy < img->height && !has_pixels; sy++) {
          for (int sx = vx; sx < cell_right && sx < img->width; sx++) {
            if (is_dark(img->data, img->alloc_width, sx, sy)) {
              has_pixels = 1;
              break;
            }
          }
        }

        if (has_pixels) {
          if (has_ext(cell->value, ".sh")) {
            cell->type = VAL_COMMAND;
          } else {
            cell->type = VAL_IMAGE;
          }

          int ix1 = vx, iy1 = img_y;
          for (int sy = img_y; sy < row_bottom && sy < img->height; sy++) {
            for (int sx = vx; sx < cell_right && sx < img->width; sx++) {
              if (is_dark(img->data, img->alloc_width, sx, sy)) {
                if (sx > ix1) { ix1 = sx; }
                if (sy > iy1) { iy1 = sy; }
              }
            }
          }

          int iw = ix1 - vx + 1;
          int ih = iy1 - img_y + 1;
          if (iw > 0 && ih > 0) {
            cell->img_width = iw;
            cell->img_height = ih;
            cell->img_data = malloc(iw * ih * 3);
            for (int sy = 0; sy < ih; sy++) {
              for (int sx = 0; sx < iw; sx++) {
                int si = ((img_y + sy) * img->alloc_width + vx + sx) * 3;
                int di = (sy * iw + sx) * 3;
                cell->img_data[di] = img->data[si];
                cell->img_data[di + 1] = img->data[si + 1];
                cell->img_data[di + 2] = img->data[si + 2];
              }
            }
          }
          int pw = text_width(cell->value);
          cell->width = MAX(kw, MAX(pw, iw)) + CELL_PAD * 2;
        } else {
          char* end;
          long n = strtol(cell->value, &end, 10);
          if (*end == '\0' && end != cell->value) {
            cell->type = VAL_NUM;
            cell->num = (int)n;
          } else {
            cell->type = VAL_TEXT;
          }
          cell->width = MAX(kw, text_width(cell->value)) + CELL_PAD * 2;
        }
      } else {
        cell->type = VAL_IMAGE;
        cell->width = kw + CELL_PAD * 2;
      }

      row->cell_count++;
    }
  }
}

int
db_load(Database* db, char* filename)
{
  char resolved[512];
  if (!resolve_path(filename, resolved, sizeof(resolved))) {
    return -1;
  }
  int w, h, channels;
  uint8_t* loaded = stbi_load(resolved, &w, &h, &channels, 3);
  if (!loaded) {
    return -1;
  }

  for (int i = 0; i < db->row_count; i++) {
    for (int j = 0; j < db->rows[i].cell_count; j++) {
      cell_free_internal(&db->rows[i].cells[j]);
    }
  }
  db->row_count = 0;

  if (db->img.data) {
    image_free(&db->img);
  }
  image_alloc(&db->img, w, h);

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int src = (y * w + x) * 3;
      int dst = (y * db->img.alloc_width + x) * 3;
      db->img.data[dst] = loaded[src];
      db->img.data[dst + 1] = loaded[src + 1];
      db->img.data[dst + 2] = loaded[src + 2];
    }
  }

  stbi_image_free(loaded);
  strcpy(db->filename, filename);

  db_reconstruct(db);

  mkdir(IMG_DIR, 0755);
  for (int i = 0; i < db->row_count; i++) {
    for (int j = 0; j < db->rows[i].cell_count; j++) {
      Cell* c = &db->rows[i].cells[j];
      if (c->type == VAL_IMAGE && c->img_data && c->value[0]) {
        char resolved_img[512];
        if (!resolve_path(c->value, resolved_img, sizeof(resolved_img))) {
          char path[512];
          snprintf(path, sizeof(path), "%s/%s", IMG_DIR, c->value);
          stbi_write_png(path, c->img_width, c->img_height, 3,
                         c->img_data, c->img_width * 3);
        }
        int cvlen = strlen(c->value);
        if (cvlen > 8 && strcmp(c->value + cvlen - 8, ".obj.png") == 0) {
          char obj_path[512];
          snprintf(obj_path, sizeof(obj_path), "%s/%.*s",
                   IMG_DIR, cvlen - 4, c->value);
          cell_write_obj(c, obj_path, NULL);
        }
      }
    }
  }

  render_all(db);
  return 0;
}

void
db_load_commands(Database* db)
{
  DIR* dir = opendir("commands");
  if (!dir) {
    return;
  }
  struct dirent* ent;
  while ((ent = readdir(dir)) != NULL) {
    if (!has_ext(ent->d_name, ".sh")) {
      continue;
    }
    char path[512];
    snprintf(path, sizeof(path), "commands/%s", ent->d_name);
    Cell* cmd = cell_read_command(path);
    if (!cmd) {
      continue;
    }
    char name[64];
    strncpy(name, ent->d_name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    char* dot = strrchr(name, '.');
    if (dot) {
      *dot = '\0';
    }
    char key[MAX_KEY];
    snprintf(key, sizeof(key), "sys.%s", name);
    db_set_cell(db, key, cmd);
    cell_free_temp(cmd);
  }
  closedir(dir);
}

void
db_sync_stack(Database* db, Cell** items, int count)
{
  for (int i = 0; i < db->row_count; i++) {
    if (strcmp(db->rows[i].ns, "stack") == 0) {
      for (int j = 0; j < db->rows[i].cell_count; j++) {
        cell_free_internal(&db->rows[i].cells[j]);
      }
      for (int j = i; j < db->row_count - 1; j++) {
        db->rows[j] = db->rows[j + 1];
      }
      db->row_count--;
      break;
    }
  }

  for (int i = db->row_count; i > 0; i--) {
    db->rows[i] = db->rows[i - 1];
  }
  db->row_count++;

  Row* row = &db->rows[0];
  memset(row, 0, sizeof(Row));
  strcpy(row->ns, "stack");

  for (int i = 0; i < count && i < MAX_CELLS; i++) {
    Cell* cell = &row->cells[row->cell_count++];
    memset(cell, 0, sizeof(Cell));
    sprintf(cell->key, "%d", i);
    cell_copy_value(cell, items[i]);
  }

  render_all(db);
}

int
db_load_stack(Database* db, Cell** items, int max_items)
{
  for (int i = 0; i < db->row_count; i++) {
    if (strcmp(db->rows[i].ns, "stack") != 0) {
      continue;
    }
    Row* row = &db->rows[i];
    int count = 0;
    for (int j = 0; j < row->cell_count && count < max_items; j++) {
      if (!row->cells[j].tombstone) {
        items[count++] = cell_copy(&row->cells[j]);
      }
    }
    return count;
  }
  return 0;
}

int
process_poll(Cell** items, int count)
{
  int changed = 0;
  for (int i = 0; i < count; i++) {
    Cell* c = items[i];
    if (c->type != VAL_PROCESS || c->num <= 0) {
      continue;
    }
    int status;
    pid_t result = waitpid((pid_t)c->args, &status, WNOHANG);
    if (result <= 0) {
      continue;
    }
    char output[MAX_KEY] = { 0 };
    int n = read(c->num, output, sizeof(output) - 1);
    if (n > 0) {
      output[n] = '\0';
      while (n > 0 && (output[n - 1] == '\n' || output[n - 1] == '\r')) {
        output[--n] = '\0';
      }
    }
    close(c->num);
    c->num = 0;
    char* tab = strchr(c->value, '\t');
    char cmd_name[MAX_KEY] = "";
    if (tab) {
      strncpy(cmd_name, tab + 1, MAX_KEY - 1);
    }
    if (output[0]) {
      snprintf(c->value, MAX_KEY, "done:%s\t%s", output, cmd_name);
    } else {
      snprintf(c->value, MAX_KEY, "done\t%s", cmd_name);
    }
    changed = 1;
  }
  return changed;
}
