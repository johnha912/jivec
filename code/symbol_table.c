#ifndef JIVE_SYMBOL_TABLE_C
#define JIVE_SYMBOL_TABLE_C

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Hash table mapping a variable name (String) to its compile-time info.
// Each function gets its own Symbol_Table; the IR generator uses it to
// resolve `let` declarations into stack slots, bind parameters to their
// caller-supplied slots, and reject any `set` or identifier reference
// that names a variable nothing has declared yet.
//
// Open addressing with linear probing. The table grows (doubles) once it
// reaches 50% load so probes stay short. Capacity is always a power of two
// so we can mask the hash with (capacity - 1) instead of dividing.
//
// Each symbol records its rbp-relative byte offset:
//   * locals (`let`)  → negative offset (below the saved rbp)
//   * parameters      → positive offset (above the saved rbp + return addr)

typedef struct Symbol
{
    bool   occupied;
    String name;
    Type   type;
    long   frame_offset;   // signed bytes from rbp
} Symbol;

typedef struct Symbol_Table
{
    Symbol *items;
    long    count;
    long    capacity;
    long    n_locals;      // number of `let`-declared locals so far
} Symbol_Table;

// FNV-1a 64-bit. Good enough for short identifier strings.
static unsigned long st_fnv1a(const char *data, long count)
{
    unsigned long hash = 14695981039346656037UL;
    for (long i = 0; i < count; i++) {
        hash ^= (unsigned char)data[i];
        hash *= 1099511628211UL;
    }
    return hash;
}

static int st_strings_equal(String a, String b)
{
    if (a.count != b.count) return 0;
    return memcmp(a.data, b.data, (size_t)a.count) == 0;
}

// Forward declaration — grow needs to re-insert through the raw path.
static Symbol *symbol_table_insert_raw(Symbol_Table *table, Symbol sym);

static void symbol_table_grow(Symbol_Table *table, long new_capacity)
{
    Symbol *old_items = table->items;
    long    old_capacity = table->capacity;

    table->items = (Symbol *)calloc((size_t)new_capacity, sizeof(Symbol));
    if (!table->items) { fprintf(stderr, "symbol_table: out of memory\n"); exit(1); }
    table->capacity = new_capacity;
    table->count = 0;

    for (long i = 0; i < old_capacity; i++) {
        if (old_items[i].occupied) {
            symbol_table_insert_raw(table, old_items[i]);
        }
    }
    free(old_items);
}

static void symbol_table_init(Symbol_Table *table)
{
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
    table->n_locals = 0;
    symbol_table_grow(table, 16);
}

static void symbol_table_free(Symbol_Table *table)
{
    free(table->items);
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
    table->n_locals = 0;
}

static Symbol *symbol_table_lookup(Symbol_Table *table, String name)
{
    if (table->capacity == 0) return NULL;
    unsigned long h = st_fnv1a(name.data, name.count);
    long mask = table->capacity - 1;
    long index = (long)(h & (unsigned long)mask);
    for (long probes = 0; probes < table->capacity; probes++) {
        Symbol *slot = &table->items[index];
        if (!slot->occupied) return NULL;
        if (st_strings_equal(slot->name, name)) return slot;
        index = (index + 1) & mask;
    }
    return NULL;
}

// Lower-level insert. Caller must guarantee free space exists and that no
// entry with the same name is already present.
static Symbol *symbol_table_insert_raw(Symbol_Table *table, Symbol sym)
{
    unsigned long h = st_fnv1a(sym.name.data, sym.name.count);
    long mask = table->capacity - 1;
    long index = (long)(h & (unsigned long)mask);
    for (;;) {
        Symbol *slot = &table->items[index];
        if (!slot->occupied) {
            *slot = sym;
            slot->occupied = true;
            table->count++;
            return slot;
        }
        index = (index + 1) & mask;
    }
}

// Generic declare. Returns NULL if a symbol with this name already exists,
// so the caller can report the redeclaration with a precise location.
static Symbol *symbol_table_declare(Symbol_Table *table, String name, Type type, long frame_offset)
{
    if (symbol_table_lookup(table, name)) return NULL;

    if ((table->count + 1) * 2 > table->capacity) {
        long new_cap = table->capacity == 0 ? 16 : table->capacity * 2;
        symbol_table_grow(table, new_cap);
    }

    Symbol sym = {0};
    sym.name = name;
    sym.type = type;
    sym.frame_offset = frame_offset;
    return symbol_table_insert_raw(table, sym);
}

// Each `let` lives 8 bytes deeper in the local-variable area. Offsets are
// negative because locals sit below the saved rbp.
static Symbol *symbol_table_declare_local(Symbol_Table *table, String name, Type type)
{
    long offset = -8 * (table->n_locals + 1);
    Symbol *sym = symbol_table_declare(table, name, type, offset);
    if (sym) table->n_locals++;
    return sym;
}

// Parameters live in the caller's frame, above the saved rbp + return addr.
// Args are pushed left-to-right at the call site, so the first-declared param
// (lowest index) sits deepest in the stack and last-declared (highest index)
// sits at [rbp+16].
static Symbol *symbol_table_declare_param(Symbol_Table *table, String name, Type type,
                                          long param_index, long n_params)
{
    long offset = 8 + 8 * (n_params - param_index);
    return symbol_table_declare(table, name, type, offset);
}

#endif
