// save the names,
// so we don't ask for books from people who aren't on the server
// no need for destruct as we dont use it :D

#include "string.h"
#include "malloc.h"

typedef struct
{
    char *buffer;
    int len;
} names_t;

void names_init(names_t *names)
{
    names->buffer = malloc(2);
    names->buffer[0] = ' ';
    names->buffer[1] = '\0';
}

// void names_add(names_t *names, const char* name)
// {

// }

int names_find(names_t *names, const char* name)
{
    int len = strlen(name);
    char tmp[len + 3];
    tmp[0] = ' ';
    strcpy(tmp + 1, name);
    tmp[len + 2] = '\0';
    tmp[len + 1] = ' ';

    return strstr(names->buffer, tmp) != NULL;
}

// space separated names
void names_add_many(names_t *names, const char* many_names)
{
    size_t len = strlen(names->buffer) + strlen(many_names);
    names->buffer = realloc(names->buffer, len + 2);
    strcat(names->buffer, many_names);
    strcat(names->buffer, " ");
}

// int main(int argc, char const *argv[])
// {
//     names_t a;
//     names_init(&a);
//     names_add_many(&a, "a b c");
//     names_add_many(&a, "d e ff");
//     names_add_many(&a, "evans");

//     names_find(&a, "a");
//     names_find(&a, "b");
//     names_find(&a, "ff");
//     names_find(&a, "fff");
//     names_find(&a, "evan");
//     return 0;
// }