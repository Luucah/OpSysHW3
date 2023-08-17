#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
/*
  definitions of a node and singly linked list class.
  in theory, all functions in here are thread safe over the structure of a list.
  each node represents a currently running thread (tid),
  with a socket descriptor (clientsd) representing the tcp connection
  used to communicate with the wordle player.
  A node also contains a pointer to the next node (next) in the list,
  which is null if the node is not in a list or the tail of a list.
*/

struct List {
    struct Node *head;
    struct Node *tail;
    int size;
    pthread_mutex_t mutex;
};

struct Node {
    int clientsd;
    pthread_t tid;
    struct Node *next;
};

// Returns a pointer to the node created.
struct Node *newNode(int csd, pthread_t thread) {
    struct Node *newnode = calloc(1, sizeof(struct Node));
    newnode->clientsd = csd;
    newnode->tid = thread;
    newnode->next = NULL;
    return newnode;
}

// Creates a new list, and the new node,
struct List *newList() {
    struct List *lst = calloc(1, sizeof(struct List));
    lst->head = lst->tail = NULL;
    lst->size = 0;
    pthread_mutex_init(&lst->mutex, NULL);
    return lst;
}

// Returns the newly added tail of the list.
struct Node *push_back(struct List *lst, int csd, pthread_t thread) {
    struct Node *node = newNode(csd, thread);
    struct Node *tmp;
    if (lst == NULL)
        return NULL;

    // if the list is empty, the new node is the head and tail.
    if (lst->size == 0)
        pthread_mutex_init(&lst->mutex, NULL);

    pthread_mutex_lock(&lst->mutex);
    if (lst->size == 0) {
        lst->head = node;
        lst->tail = node;
        lst->size++;
    } else {
        struct Node *tmp = lst->tail;
        tmp->next = node;
        lst->tail = node;
        lst->size++;
    }
    tmp = lst->tail;
    pthread_mutex_unlock(&lst->mutex);
    return tmp;
}

// Removes the node representing the specified thread from the list.
// Frees the memory allocated to that node, and closes the socket descriptor.
// Returns true if there was a node in the list with a matching thread ID, and
// false otherwise
bool removeList(struct List *lst, pthread_t thread) {
    if (lst == NULL)
        return false;
    pthread_mutex_lock(&lst->mutex);
    if (lst->size == 1) {
        close(lst->head->clientsd);
        free(lst->head);
        lst->head = lst->tail = NULL;
        lst->size = 0;
        pthread_mutex_unlock(&lst->mutex);
        pthread_mutex_destroy(&lst->mutex);
        return true;
    }

    struct Node *tmp;
    if (lst->head->tid == thread) {
        tmp = lst->head;
        lst->head = lst->head->next;
        close(tmp->clientsd);
        free(tmp);
        lst->size--;
        pthread_mutex_unlock(&lst->mutex);
        return true;
    }
    struct Node *prev = lst->head;
    struct Node *ptr = lst->head->next;
    while (ptr != NULL) {
        if (ptr->tid == thread) {
            tmp = ptr->next;
            close(ptr->clientsd);
            free(ptr);
            prev->next = tmp;
            lst->size--;
            pthread_mutex_unlock(&lst->mutex);
            return true;
        }
        prev = ptr;
        ptr = ptr->next;
    }
    pthread_mutex_unlock(&lst->mutex);
    return false;
}
