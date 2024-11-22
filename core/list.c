#include "list.h"
#include <stdlib.h>
#include <string.h>

List *create_list() {
  List *list = malloc(sizeof(List));
  list->head = NULL;
  list->count = 0;
  return list;
}

void list_add(List *list, void *value, size_t count) {
  ListNode *head = list->head;
  ListNode *new_node = malloc(sizeof(ListNode));
  new_node->value = malloc(count);
  new_node->next = NULL;
  memcpy(new_node->value, value, count);
  if (head == NULL) {
    list->head = new_node;
    list->count++;
    return;
  }
  while (head->next != NULL)
    head = head->next;
  head->next = new_node;
  list->count++;
}

void list_remove(List *list, void *value, size_t count) {
  if(list == NULL || value == NULL)
    return;
  if (list->head == NULL)
    return;
  ListNode *head = list->head;
  if (head->value != NULL && memcmp(head->value, value, count) == 0) {
    list->head = head->next;
    free(head->value);
    free(head);
    list->count--;
    return;
  }
  while (head->next != NULL) {
    if (head->next->value != NULL && memcmp(head->next->value, value, count) == 0) {
      ListNode *aux = head->next;
      head->next = aux->next;
      free(aux->value);
      free(aux);
      list->count--;
      return;
    }
    head = head->next;
  }
}

uint8_t list_contains(List *list, void *value, size_t count) {
  ListNode *head = list->head;
  while (head != NULL) {
    if (memcmp(head->value, value, count) == 0)
      return 1;
    head = head->next;
  }
  return 0;
}

void _node_free(ListNode *node) {
  if (node == NULL)
    return;
  _node_free(node->next);
  free(node->value);
  free(node);
}

void list_destroy(List *list) {
  _node_free(list->head);
  free(list);
}
