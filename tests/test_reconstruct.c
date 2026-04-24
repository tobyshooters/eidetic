#include "db.h"
#include "glyph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(Database* db, const char* label)
{
  printf("=== %s ===\n", label);
  for (int i = 0; i < db->row_count; i++) {
    printf("Row %d: ns='%s' y=%d h=%d cells=%d\n",
           i, db->rows[i].ns, db->rows[i].y_offset, db->rows[i].height,
           db->rows[i].cell_count);
    for (int j = 0; j < db->rows[i].cell_count; j++) {
      Cell* c = &db->rows[i].cells[j];
      if (c->tombstone) { printf("  cell %d: TOMBSTONE\n", j); continue; }
      printf("  cell %d: key='%s' val='%s' type=%d col=%d w=%d img=%dx%d\n",
             j, c->key, c->value, c->type, c->col, c->width,
             c->img_width, c->img_height);
    }
  }
  printf("\n");
}

int main(void)
{
  Database* db = calloc(1, sizeof(Database));
  db_init(db);

  // Test 1: single char namespace
  db_set(db, "i.val", "test");
  dump(db, "Test 1: single char ns");
  db_save(db, "test1.png");
  db_free(db);
  db_init(db);
  db_load(db, "test1.png");
  dump(db, "Test 1: after load");

  // Test 2: many cells in one row
  db_free(db);
  db_init(db);
  db_set(db, "a", "1");
  db_set(db, "b", "2");
  db_set(db, "c", "3");
  db_set(db, "d", "4");
  db_set(db, "e", "5");
  dump(db, "Test 2: many cells");
  db_save(db, "test2.png");
  db_free(db);
  db_init(db);
  db_load(db, "test2.png");
  dump(db, "Test 2: after load");

  // Test 3: deletion then save/load
  db_free(db);
  db_init(db);
  db_set(db, "a", "1");
  db_set(db, "b", "2");
  db_set(db, "c", "3");
  db_del(db, "b");
  dump(db, "Test 3: after delete");
  db_save(db, "test3.png");
  db_free(db);
  db_init(db);
  db_load(db, "test3.png");
  dump(db, "Test 3: after load");

  // Test 4: longer values
  db_free(db);
  db_init(db);
  db_set(db, "msg", "hello world foo bar");
  db_set(db, "num", "999999");
  dump(db, "Test 4: long values");
  db_save(db, "test4.png");
  db_free(db);
  db_init(db);
  db_load(db, "test4.png");
  dump(db, "Test 4: after load");

  // Test 5: image cell
  db_free(db);
  db_init(db);
  db_set(db, "title", "test");

  Cell* imgcell = calloc(1, sizeof(Cell));
  imgcell->type = VAL_IMAGE;
  imgcell->img_width = 8;
  imgcell->img_height = 8;
  imgcell->img_data = calloc(8 * 8 * 3, 1);
  for (int i = 0; i < 8*8*3; i++) imgcell->img_data[i] = (i % 3 == 0) ? 0 : 128;
  snprintf(imgcell->value, MAX_KEY, "test.png");
  db_set_cell(db, "pic", imgcell);
  cell_free_temp(imgcell);

  dump(db, "Test 5: with image");
  db_save(db, "test5.png");
  db_free(db);
  db_init(db);
  db_load(db, "test5.png");
  dump(db, "Test 5: after load");

  // Test 6: deeply nested namespace
  db_free(db);
  db_init(db);
  db_set(db, "a.b.c.d", "deep");
  dump(db, "Test 6: deep ns");
  db_save(db, "test6.png");
  db_free(db);
  db_init(db);
  db_load(db, "test6.png");
  dump(db, "Test 6: after load");

  db_free(db);
  free(db);
  return 0;
}
