#include "picozip.h"
#include <string.h>
#include <stdio.h>

#ifndef PICOZIP_IMPLEMENTATION
#define PICOZIP_IMPLEMENTATION
#endif

int main(int argc, char **argv)
{
    int err;
    picozip_file *f;

    if ((err = picozip_new_path(&f, "simple.zip", "wb")) != PICOZIP_OK)
    {
        printf("picozip_new_path(): %s\n", strerror(err));
        return 1;
    }

    if ((err = picozip_new_entry_mem(f, "test.txt", (const uint8_t *)"hello world!", 12)) != PICOZIP_OK)
    {
        printf("picozip_new_entry_mem(): %s\n", strerror(err));
        return 1;
    }

    if ((err = picozip_new_entry_mem(f, "empty folder/", NULL, 0)) != PICOZIP_OK)
    {
        printf("picozip_new_entry_mem(): %s\n", strerror(err));
        return 1;
    }

    if ((err = picozip_new_entry_mem_ex(f, "lorem.txt", (const uint8_t *)"hello world!", 12, 0, "this is a comment", 17)) != PICOZIP_OK)
    {
        printf("picozip_new_entry_mem_ex(): %s\n", strerror(err));
        return 1;
    }

    if ((err = picozip_end_ex(f, "this is a file comment", 22)) != PICOZIP_OK)
    {
        printf("picozip_end_ex(): %s\n", strerror(err));
        return 1;
    }

    picozip_free_path(f);

    return 0;
}