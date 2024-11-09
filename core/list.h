#include <stdint.h>
#include <stdio.h>

typedef struct node {
  void *value;
  struct node *next;
} ListNode;

typedef struct {
  ListNode *head;
  size_t count;
} List;

List *create_list();
void list_add(List *list, void *value, size_t count);
uint8_t list_contains(List *list, void *value, size_t count);
void list_remove(List *list, void *value, size_t count);
void list_destroy(List *list);
