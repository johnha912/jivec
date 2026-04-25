#ifndef JIVE_SYMBOL_TABLE_C
#define JIVE_SYMBOL_TABLE_C

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Hash table for our symbols. External chaining: each bucket is a singly
// linked list of Symbol nodes. The Symbol struct holds the lookup key and
// chain link; the per-symbol payload lives on Symbol_Data so the IR builder
// can hand back a stable pointer to a symbol's metadata via lookup_symbol.
//
// Each function gets its own Symbol_Table — locals don't leak between
// functions, and parameters are bound up front before the body is lowered.
//
// The slot model:
//   * locals (declared by `let`)  → slot 0, 1, 2, ... in declaration order
//   * parameters                  → slot 0, 1, 2, ... in declaration order
// `is_param` distinguishes the two so that codegen lays them out on
// different sides of the saved rbp. `symbol_frame_offset` does the
// translation slot → signed rbp-relative byte offset.

typedef struct Symbol_Data
{
    int  variable_slot;   // slot index within its kind (0, 1, 2, …)
    bool is_param;        // true: parameter (caller's frame); false: local
    Type type;
    Loc  loc;             // where the symbol was declared
} Symbol_Data;

typedef struct Symbol
{
    String         name;
    Symbol_Data    data;
    struct Symbol *next;  // next entry in this bucket's chain
} Symbol;

typedef struct Symbol_Table
{
    Symbol **symbols;       // array of bucket heads
    long     number_of_slots;
    long     entry_count;

    long     n_locals;      // count of declared locals so far
    long     n_params;      // count of declared parameters so far
} Symbol_Table;

// FNV-1a, mapped to a bucket index.
static unsigned long st_hash(String name, long number_of_slots)
{
    unsigned long h = 14695981039346656037UL;
    for (long i = 0; i < name.count; i++) {
        h ^= (unsigned char)name.data[i];
        h *= 1099511628211UL;
    }
    return h % (unsigned long)number_of_slots;
}

static int st_strings_equal(String a, String b)
{
    if (a.count != b.count) return 0;
    return memcmp(a.data, b.data, (size_t)a.count) == 0;
}

Symbol_Table make_symbol_table(long number_of_slots)
{
    Symbol_Table result = {0};
    result.symbols = (Symbol **)calloc((size_t)number_of_slots, sizeof(Symbol *));
    if (!result.symbols) {
        fprintf(stderr, "symbol_table: out of memory\n");
        exit(1);
    }
    result.number_of_slots = number_of_slots;
    return result;
}

void free_symbol_table(Symbol_Table *table)
{
    if (!table->symbols) return;
    for (long i = 0; i < table->number_of_slots; i++) {
        Symbol *node = table->symbols[i];
        while (node) {
            Symbol *next = node->next;
            free(node);
            node = next;
        }
    }
    free(table->symbols);
    table->symbols = NULL;
    table->number_of_slots = 0;
    table->entry_count = 0;
    table->n_locals = 0;
    table->n_params = 0;
}

Symbol_Data *lookup_symbol(Symbol_Table *table, String name)
{
    if (table->number_of_slots == 0) return NULL;
    unsigned long bucket = st_hash(name, table->number_of_slots);
    for (Symbol *node = table->symbols[bucket]; node; node = node->next) {
        if (st_strings_equal(node->name, name)) {
            return &node->data;
        }
    }
    return NULL;
}

// Re-link an already-allocated Symbol into its bucket; used by both
// insert_symbol (for fresh nodes) and grow_table (for rehashing).
static void link_into_bucket(Symbol_Table *table, Symbol *node)
{
    unsigned long bucket = st_hash(node->name, table->number_of_slots);
    node->next = table->symbols[bucket];
    table->symbols[bucket] = node;
}

void grow_table(Symbol_Table *table, long new_number_of_slots)
{
    Symbol **old_buckets = table->symbols;
    long     old_count   = table->number_of_slots;

    table->symbols = (Symbol **)calloc((size_t)new_number_of_slots, sizeof(Symbol *));
    if (!table->symbols) {
        fprintf(stderr, "symbol_table: out of memory\n");
        exit(1);
    }
    table->number_of_slots = new_number_of_slots;

    // Walk every old bucket and rehash each node into its new bucket. We
    // reuse the existing Symbol allocations, just relink the next pointers.
    for (long i = 0; i < old_count; i++) {
        Symbol *node = old_buckets[i];
        while (node) {
            Symbol *next = node->next;
            node->next = NULL;
            link_into_bucket(table, node);
            node = next;
        }
    }
    free(old_buckets);
}

bool insert_symbol(Symbol_Table *table, String name, Symbol_Data data)
{
    if (lookup_symbol(table, name) != NULL) return false;

    // Keep average chain length around 1: once entry_count would exceed the
    // bucket count, double the table.
    if (table->entry_count + 1 > table->number_of_slots) {
        grow_table(table, table->number_of_slots * 2);
    }

    Symbol *node = (Symbol *)malloc(sizeof(Symbol));
    if (!node) {
        fprintf(stderr, "symbol_table: out of memory\n");
        exit(1);
    }
    node->name = name;
    node->data = data;
    node->next = NULL;
    link_into_bucket(table, node);
    table->entry_count++;
    return true;
}

// Higher-level helpers used by the IR builder.

// Declare a `let`-introduced local. Returns NULL on duplicate (the caller
// reports the redeclaration with a precise location); otherwise returns the
// pointer to the inserted Symbol_Data so callers can read back details
// without a second lookup.
Symbol_Data *declare_local(Symbol_Table *table, String name, Type type, Loc loc)
{
    Symbol_Data data = {0};
    data.variable_slot = (int)table->n_locals;
    data.is_param      = false;
    data.type          = type;
    data.loc           = loc;
    if (!insert_symbol(table, name, data)) return NULL;
    table->n_locals++;
    return lookup_symbol(table, name);
}

// Declare a parameter at a specific index. The caller is expected to have
// set table->n_params up front (so frame offsets stay consistent even if a
// duplicate name short-circuits one of the inserts).
Symbol_Data *declare_param(Symbol_Table *table, String name, Type type,
                           long param_index, Loc loc)
{
    Symbol_Data data = {0};
    data.variable_slot = (int)param_index;
    data.is_param      = true;
    data.type          = type;
    data.loc           = loc;
    if (!insert_symbol(table, name, data)) return NULL;
    return lookup_symbol(table, name);
}

// Convert a slot+kind into the signed rbp-relative byte offset codegen
// wants. Locals sit below the saved rbp; parameters sit above the saved
// rbp + return address, with the first-declared param deepest.
long symbol_frame_offset(const Symbol_Table *table, const Symbol_Data *sym)
{
    if (sym->is_param) {
        return 8 + 8 * (table->n_params - sym->variable_slot);
    }
    return -8 * (sym->variable_slot + 1);
}

#endif
