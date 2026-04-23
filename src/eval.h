#ifndef EVAL_H
#define EVAL_H

#include "db.h"

#define MAX_CHILDREN 32
#define MAX_TOKEN 256

typedef enum
{
  NODE_ATOM,
  NODE_LIST
} NodeType;

typedef struct Node
{
  NodeType type;
  char atom[MAX_TOKEN];
  struct Node* children[MAX_CHILDREN];
  int child_count;
} Node;

Node*
parse(char* input);
Cell*
eval(Node* node, Database* db);
void
node_free(Node* node);

#endif
