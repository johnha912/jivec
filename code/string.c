#ifndef JIVE_STRING_C
#define JIVE_STRING_C

#include <string.h>

typedef struct String
{
    char *data;
    long count;
} String;

#define PRINT_STRING(s) (int)(s).count, (s).data
#define str_lit(s) (String){(char *)(s), sizeof(s) - 1}

static inline String str_from_cstr(const char *cstr)
{
    String result = {(char *)cstr, (long)strlen(cstr)};
    return result;
}

#endif
