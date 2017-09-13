typedef struct names_s
{
    char *buffer;
} names_t;

void names_init(names_t *names);
int names_find(names_t *names, const char* name);
void names_add_many(names_t *names, const char* many_names);
