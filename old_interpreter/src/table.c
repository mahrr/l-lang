/*
 * (table.c | 9 May 19 | Ahmad Maher)
 *
 * Hash table implementation.
 *
 * It's a chainded table consists of an array of linked lists, 
 * each list hold element blocks of data with a similar hash code.
 *
 * Insertion and retrival is a constant time operation assuming
 * a hash function with an uniformal distribution.
 *
 * Initailzation and deallocation is O(m) where m is the size of
 * the table.
 *
*/

#include <stdlib.h>
#include <stdint.h>

#include "array.h"
#include "table.h"

/* 
 * adds an element to a table entry, and
 * returns the entry.
 */
static Entry *entry_add(Entry *entry, Elem *elem) {
    Entry **p = &entry;
    
    while (*p)
        p = &(*p)->link;

    *p = malloc(sizeof (**p));
    (*p)->elem = elem;
    (*p)->link = NULL;

    return entry;
}

/* return the table element with specified key */
static Elem *get_elem(Table *table, const void *key) {
    uint64_t hash = table->hash(key);
    unsigned index = hash % table->size;

    Elem *elem;
    Entry *entry = table->entries[index];
    for ( ; entry; entry = entry->link) {
        elem = (Elem*)entry->elem;
        /* hash comparsion is done first, as it's likely faster
           than key comparsion, especially if the keys are strings. */
        if (hash == elem->hash && table->comp(elem->key, key))
            return elem;
    }

    return NULL;
}

void init_table(Table *table, int size, Hash_Fn hash,
                DFree_Fn free, Comp_Fn comp) {
    table->elems = 0;
    table->size = size;
    table->hash = hash;
    table->free = free;
    table->comp = comp;

    /* allocate array of list pointers */
    table->entries = (Entry**)malloc(sizeof(Entry*) * size);
    for (int i = 0; i < size; i++)
        table->entries[i] = NULL;

    ARR_INIT(&table->indexes, int);
}

int table_lookup(Table *table, const void *key) {
    if (get_elem(table, key) != NULL)
        return 1;

    /* element not found */
    return 0;
}

void *table_put(Table *table, const void *key, void *data) {
    /* check if an element with the same key exists */
    Elem *elem = get_elem(table, key);
    void *prev;

    if (elem != NULL) {
        prev = elem->data;  /* the old associated element */
        elem->data = data;  /* put the new element */
        return prev;        /* return the old element */
    }

    /* no element with the same key exists */
    uint64_t hash = table->hash(key);
    unsigned index = hash % table->size;

    /* register that index as a populated index */
    ARR_ADD(&table->indexes, index);

    /* allocate a new element block */
    elem = malloc(sizeof(Elem));
    elem->key = key;
    elem->hash = hash;
    elem->data = data;
    
    table->entries[index] = entry_add(table->entries[index], elem);
    table->elems++;

    return NULL;
}

void *table_get(Table *table, const void *key) {
    Elem *elem = get_elem(table, key);
    return elem ? elem->data : NULL;
}

void *table_remove(Table *table, const void *key) {
    uint64_t hash = table->hash(key);
    unsigned index = hash % table->size;

    Elem *elem;
    Entry **ep = &table->entries[index];
    for ( ; *ep; ep = &(*ep)->link) {
        elem = (Elem*)(*ep)->elem;
        if (hash == elem->hash && table->comp(elem->key, key)) {
            void *rem = elem->data;
            Entry *e = *ep;
            *ep = (*ep)->link;
            table->elems--;
            free(elem);
            free(e);
            return rem;
        }
    }

    /* elemnt not found */
    return NULL;
}

void free_table(Table *table) {
    /* deallocating the table elements */
    for (int i = 0; i < table->indexes.len; i++) {
        int index = table->indexes.elems[i];
        Entry *entry = table->entries[index];
        Entry *next;
        Elem *elem;
        for ( ; entry; entry = next) {
            elem = entry->elem;
            next = entry->link;
            if (table->free)
                table->free(elem->data);
            free(elem);
            free(entry);
        }
    }
    free(table->entries);
    ARR_FREE(&table->indexes);
}
