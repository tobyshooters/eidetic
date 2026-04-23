#include "db.h"
#include "glyph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>

#define IMG_DIR "images"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

void
image_alloc(Image* img, int width, int height)
{
  img->width = width;
  img->height = height;
  img->alloc_width = width * 2;
  img->alloc_height = height * 2;
  img->data = calloc(img->alloc_width * img->alloc_height * 3, 1);
  memset(img->data, 255, img->alloc_width * img->alloc_height * 3);
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
          img->data[idx] = img->data[idx + 1] = img->data[idx + 2] = 255;
        }
      }
    }
    if (height > old_height) {
      for (int y = old_height; y < height; y++) {
        for (int x = 0; x < width; x++) {
          int idx = (y * img->alloc_width + x) * 3;
          img->data[idx] = img->data[idx + 1] = img->data[idx + 2] = 255;
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
  memset(new_data, 255, new_alloc_w * new_alloc_h * 3);

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
  memset(img->data, 255, img->alloc_width * img->alloc_height * 3);
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
  return text_width(row->ns) + CELL_GAP + CELL_PAD;
}

static int
cell_val_height(Cell* cell)
{
  if (cell->type == VAL_IMAGE) {
    return cell->img_height + CELL_PAD;
  }
  return FONT_HEIGHT + CELL_PAD;
}

static int
cell_height(Cell* cell)
{
  return KEY_HEIGHT + cell_val_height(cell);
}

static int
line_height(Row* row, int line)
{
  int h = KEY_HEIGHT + FONT_HEIGHT + CELL_PAD;
  for (int i = 0; i < row->cell_count; i++) {
    if (!row->cells[i].tombstone && row->cells[i].line == line) {
      int ch = cell_height(&row->cells[i]);
      if (ch > h) {
        h = ch;
      }
    }
  }
  return h;
}

static int
row_lines(Row* row)
{
  int max_line = 0;
  for (int i = 0; i < row->cell_count; i++) {
    if (!row->cells[i].tombstone && row->cells[i].line > max_line) {
      max_line = row->cells[i].line;
    }
  }
  return max_line + 1;
}

static int
row_height(Row* row)
{
  int h = 0;
  for (int l = 0; l < row_lines(row); l++) {
    h += line_height(row, l);
  }
  return h > 0 ? h : KEY_HEIGHT + FONT_HEIGHT + CELL_PAD;
}

static void
layout_row(Row* row, int max_width)
{
  int label_w = ns_label_width(row);
  int col = label_w;
  int line = 0;

  for (int i = 0; i < row->cell_count; i++) {
    if (row->cells[i].tombstone) {
      continue;
    }
    if (col + CELL_GAP + row->cells[i].width > max_width && col > label_w) {
      line++;
      col = label_w;
    }
    row->cells[i].col = col + CELL_GAP;
    row->cells[i].line = line;
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
  int y = 0;
  for (int i = 0; i < db->row_count; i++) {
    layout_row(&db->rows[i], db->max_width);
    db->rows[i].y_offset = y;
    db->rows[i].height = row_height(&db->rows[i]);
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

  Row* row = &db->rows[db->row_count];
  memset(row, 0, sizeof(Row));
  strcpy(row->ns, ns);
  row->height = KEY_HEIGHT + FONT_HEIGHT + CELL_PAD;
  db->row_count++;

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
render_cell(Image* img, Cell* cell, int y_offset)
{
  int key_y = y_offset + 1;
  int val_y = y_offset + KEY_HEIGHT;
  write_text(img->data, img->alloc_width, cell->key, cell->col + 1, key_y,
             0, 0, 0);

  if (cell->type == VAL_IMAGE && cell->img_data) {
    int ox = cell->col + 1;
    int oy = val_y;
    for (int sy = 0; sy < cell->img_height; sy++) {
      for (int sx = 0; sx < cell->img_width; sx++) {
        int src = (sy * cell->img_width + sx) * 3;
        int dx = ox + sx;
        int dy = oy + sy;
        if (dx < img->alloc_width && dy < img->alloc_height) {
          int dst = (dy * img->alloc_width + dx) * 3;
          img->data[dst] = cell->img_data[src];
          img->data[dst + 1] = cell->img_data[src + 1];
          img->data[dst + 2] = cell->img_data[src + 2];
        }
      }
    }
  } else {
    write_text(img->data, img->alloc_width, cell->value, cell->col + 1, val_y,
               60, 60, 60);
  }
}

static void
render_row(Database* db, Row* row)
{
  layout_row(row, db->max_width);

  int rh = row_height(row);
  int needed_w = db->max_width;
  if (needed_w > db->img.width) {
    image_realloc(&db->img, needed_w, db->img.height);
  }

  int needed_h = row->y_offset + rh;
  if (needed_h > db->img.height) {
    image_realloc(&db->img, db->img.width, needed_h);
  }

  // Clear entire row region
  for (int y = row->y_offset; y < row->y_offset + rh && y < db->img.alloc_height; y++) {
    for (int x = 0; x < db->img.alloc_width; x++) {
      int idx = (y * db->img.alloc_width + x) * 3;
      db->img.data[idx] = db->img.data[idx + 1] = db->img.data[idx + 2] = 255;
    }
  }

  // Render namespace label
  int label_y = row->y_offset + 1;
  write_text(db->img.data, db->img.alloc_width, row->ns, 1, label_y,
             120, 120, 120);

  // Render cells at their line offsets
  int lines = row_lines(row);
  int* line_y = calloc(lines, sizeof(int));
  line_y[0] = row->y_offset;
  for (int l = 1; l < lines; l++) {
    line_y[l] = line_y[l - 1] + line_height(row, l - 1);
  }

  for (int i = 0; i < row->cell_count; i++) {
    if (!row->cells[i].tombstone) {
      render_cell(&db->img, &row->cells[i], line_y[row->cells[i].line]);
    }
  }

  free(line_y);
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

static char*
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

#define TEXT_WRAP 68

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

  int line_h = FONT_HEIGHT + 1;
  int w = TEXT_WRAP * (FONT_WIDTH + 1) + 1;
  int h = nlines * line_h + 1;
  uint8_t* pixels = malloc(w * h * 3);
  memset(pixels, 255, w * h * 3);

  for (int i = 0; i < nlines; i++) {
    write_text(pixels, w, wrapped[i], 0, i * line_h, 0, 0, 0);
  }

  Cell* c = calloc(1, sizeof(Cell));
  c->type = VAL_IMAGE;
  c->img_data = pixels;
  c->img_width = w;
  c->img_height = h;
  snprintf(c->value, MAX_KEY, "%s", path);
  return c;
}

static int
has_ext(char* path, char* ext)
{
  char* dot = strrchr(path, '.');
  return dot && strcasecmp(dot, ext) == 0;
}

Cell*
cell_read_image(char* path)
{
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
    cell->col = row_end(row) + CELL_GAP;
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
    cell->col = row_end(row) + CELL_GAP;
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

  return save_png(path, img->data + (y0 * img->alloc_width + x0) * 3,
                  tw, th, img->alloc_width);
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
  return 0;
}

