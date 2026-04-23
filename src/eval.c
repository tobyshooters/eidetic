#include "eval.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char* cursor;

static void
skip_ws(void)
{
  while (*cursor && isspace(*cursor))
    cursor++;
}

static Node*
make_atom(char* text)
{
  Node* n = calloc(1, sizeof(Node));
  n->type = NODE_ATOM;
  snprintf(n->atom, MAX_TOKEN, "%s", text);
  return n;
}

static Node*
make_list(void)
{
  Node* n = calloc(1, sizeof(Node));
  n->type = NODE_LIST;
  return n;
}

static Node*
parse_expr(void)
{
  skip_ws();
  if (!*cursor)
    return NULL;

  if (*cursor == '(') {
    cursor++;
    Node* list = make_list();
    while (1) {
      skip_ws();
      if (!*cursor || *cursor == ')')
        break;
      Node* child = parse_expr();
      if (child && list->child_count < MAX_CHILDREN)
        list->children[list->child_count++] = child;
    }
    if (*cursor == ')')
      cursor++;
    return list;
  }

  char buf[MAX_TOKEN] = { 0 };
  int i = 0;
  while (*cursor && !isspace(*cursor) && *cursor != '(' && *cursor != ')') {
    if (i < MAX_TOKEN - 1)
      buf[i++] = *cursor;
    cursor++;
  }
  return make_atom(buf);
}

Node*
parse(char* input)
{
  cursor = input;
  return parse_expr();
}

void
node_free(Node* node)
{
  if (!node)
    return;
  for (int i = 0; i < node->child_count; i++)
    node_free(node->children[i]);
  free(node);
}

Cell*
eval(Node* node, Database* db)
{
  if (!node)
    return cell_make_nil();

  if (node->type == NODE_ATOM) {
    char* a = node->atom;
    char* end;
    long n = strtol(a, &end, 10);
    if (*end == '\0' && end != a)
      return cell_make_num((int)n);
    return cell_make_text(a);
  }

  if (node->child_count == 0)
    return cell_make_nil();

  Cell* head = eval(node->children[0], db);
  char* op = head->value;

  if (strcasecmp(op, "set") == 0 && node->child_count >= 3) {
    Cell* key = eval(node->children[1], db);
    Cell* val = eval(node->children[2], db);
    db_set_cell(db, key->value, val);
    cell_free_temp(head);
    cell_free_temp(key);
    return val;

  } else if (strcasecmp(op, "get") == 0 && node->child_count >= 2) {
    Cell* key = eval(node->children[1], db);
    Cell* found = db_get_cell(db, key->value);
    cell_free_temp(head);
    cell_free_temp(key);
    if (!found)
      return cell_make_nil();
    Cell* copy = calloc(1, sizeof(Cell));
    memcpy(copy, found, sizeof(Cell));
    copy->img_data = NULL;
    if (found->img_data) {
      int sz = found->img_width * found->img_height * 3;
      copy->img_data = malloc(sz);
      memcpy(copy->img_data, found->img_data, sz);
    }
    return copy;

  } else if (strcasecmp(op, "del") == 0 && node->child_count >= 2) {
    Cell* key = eval(node->children[1], db);
    db_del(db, key->value);
    cell_free_temp(head);
    cell_free_temp(key);
    return cell_make_nil();

  } else if (strcasecmp(op, "read") == 0 && node->child_count >= 2) {
    Cell* path = eval(node->children[1], db);
    Cell* img = cell_read_image(path->value);
    cell_free_temp(head);
    cell_free_temp(path);
    return img ? img : cell_make_nil();

  } else if (strcasecmp(op, "save") == 0) {
    Cell* fn = node->child_count >= 2 ? eval(node->children[1], db) : NULL;
    db_save(db, fn ? fn->value : NULL);
    cell_free_temp(head);
    Cell* result = cell_make_text(db->filename);
    cell_free_temp(fn);
    return result;

  } else if (strcasecmp(op, "load") == 0 && node->child_count >= 2) {
    Cell* fn = eval(node->children[1], db);
    db_load(db, fn->value);
    cell_free_temp(head);
    return fn;

  } else if (strcasecmp(op, "add") == 0 || strcasecmp(op, "+") == 0) {
    int sum = 0;
    for (int i = 1; i < node->child_count; i++) {
      Cell* v = eval(node->children[i], db);
      sum += cell_to_num(v);
      cell_free_temp(v);
    }
    cell_free_temp(head);
    return cell_make_num(sum);

  } else if (strcasecmp(op, "sub") == 0 || strcasecmp(op, "-") == 0) {
    cell_free_temp(head);
    if (node->child_count < 2)
      return cell_make_num(0);
    Cell* first = eval(node->children[1], db);
    int result = cell_to_num(first);
    cell_free_temp(first);
    for (int i = 2; i < node->child_count; i++) {
      Cell* v = eval(node->children[i], db);
      result -= cell_to_num(v);
      cell_free_temp(v);
    }
    return cell_make_num(result);

  } else if (strcasecmp(op, "mul") == 0 || strcasecmp(op, "*") == 0) {
    int result = 1;
    for (int i = 1; i < node->child_count; i++) {
      Cell* v = eval(node->children[i], db);
      result *= cell_to_num(v);
      cell_free_temp(v);
    }
    cell_free_temp(head);
    return cell_make_num(result);

  } else if (strcasecmp(op, "div") == 0 || strcasecmp(op, "/") == 0) {
    cell_free_temp(head);
    if (node->child_count < 3)
      return cell_make_num(0);
    Cell* a = eval(node->children[1], db);
    Cell* b = eval(node->children[2], db);
    int denom = cell_to_num(b);
    int result = denom == 0 ? 0 : cell_to_num(a) / denom;
    cell_free_temp(a);
    cell_free_temp(b);
    return cell_make_num(result);

  } else if (strcasecmp(op, "mod") == 0 || strcasecmp(op, "%") == 0) {
    cell_free_temp(head);
    if (node->child_count < 3)
      return cell_make_num(0);
    Cell* a = eval(node->children[1], db);
    Cell* b = eval(node->children[2], db);
    int denom = cell_to_num(b);
    int result = denom == 0 ? 0 : cell_to_num(a) % denom;
    cell_free_temp(a);
    cell_free_temp(b);
    return cell_make_num(result);

  } else if (strcasecmp(op, "cat") == 0) {
    char buf[MAX_KEY] = { 0 };
    for (int i = 1; i < node->child_count; i++) {
      Cell* v = eval(node->children[i], db);
      int len = strlen(buf);
      snprintf(buf + len, MAX_KEY - len, "%s", v->value);
      cell_free_temp(v);
    }
    cell_free_temp(head);
    return cell_make_text(buf);

  }

  return head;
}
