# picozip

picozip.h is a header-only library that lets you create a ZIP file.
It implements the bare minimum (no compression, extended timestamp attribute),
so the library is relatively small compared to other libraries such as
[miniz](https://github.com/richgel999/miniz),
[minizip-ng](https://github.com/zlib-ng/minizip-ng),
or [minizip (zlib)](https://github.com/madler/zlib/tree/develop/contrib/minizip).

## Usage

To use the library, simply include picozip.h and define `PICOZIP_IMPLEMENTATION`
in one of the files that includes picozip.h.
Alternatively, you can use the repository as a [meson](https://mesonbuild.com/index.html) subproject,
with an appropriate wrap file.

```c
/** Error types. */
#define PICOZIP_OK 0
#define PICOZIP_EINVAL EINVAL
#define PICOZIP_ENOMEM ENOMEM
#define PICOZIP_EIO EIO

/** Callbacks to allocate, free and write data. */
typedef void *(*picozip_alloc_callback)(void *userdata, size_t size);
typedef void (*picozip_free_callback)(void *userdata, void *mem);
typedef size_t (*picozip_write_callback)(void *userdata, const void *mem, size_t size);

/** Stores data for a ZIP file. */
typedef struct picozip__file picozip_file;

/** Library functions */
extern int picozip_new(picozip_file **ofile,
                       picozip_write_callback write_cb,
                       picozip_alloc_callback alloc_cb,
                       picozip_free_callback free_cb,
                       void *userdata);
extern int picozip_new_entry_mem(picozip_file *file, const char *const path,
                                 const uint8_t *data, size_t size);
extern int picozip_new_entry_mem_ex(picozip_file *file, const char *const path,
                                    const uint8_t *data, size_t size, time_t mod_time,
                                    const char *const comment, size_t comment_len);
extern int picozip_end(picozip_file *file);
extern int picozip_end_ex(picozip_file *file, const char *const comment, size_t comment_len);
extern int picozip_free(picozip_file *file);

/** Convenience functions */
extern int picozip_new_mem(picozip_file **ofile);
size_t picozip_get_mem(picozip_file *file, void **mem);
extern int picozip_free_mem(picozip_file *file);

/** File IO functions */
#ifndef PICOZIP_NO_STDIO
extern int picozip_new_file(picozip_file **ofile, FILE *fptr);
extern int picozip_new_path(picozip_file **ofile, const char *const path, const char *const mode);
extern int picozip_new_entry_path(picozip_file *file, const char *const path,
                                  const char *const comment, size_t comment_len);
extern int picozip_new_entry_file(picozip_file *file, const char *const path, FILE *fptr,
                                  const char *const comment, size_t comment_len);
extern int picozip_free_path(picozip_file *file);
#endif
```

To create a ZIP file in memory, you can use `picozip_new_mem()`.
To create a ZIP file on the filesystem, you can use `picozip_new_file()` or `picozip_new_path()`.
For advanced use cased, you can use `picozip_new()` directly with application defined callbacks.

After obtaining a `picozip_file` pointer, you can add files or directory to it with
`picozip_new_entry_mem()`. This function allows you to specify a path and the contents.
You can also use this function to create directories by passing `NULL` or `0` for the content
and size parameters and append a forward slash (`/`) to the path.
Other functions such as `picozip_new_entry_path()`, `picozip_new_entry_file()`
and `picozip_new_entry_mem_ex()` allows you to add files directly from the filesystem or
customize other data such as modification time and comments.

To finalize the ZIP file, use `picozip_end()`.
This will write the appropriate data structures to the output.
`picozip_end_ex()` can be used to specify a comment for the ZIP file itself.
After calling `picozip_end()`, you can call `picozip_free()` to free the resources associated
with the ZIP file. ZIP files created with `picozip_new_mem()` and `picozip_new_path()` should
be freed with `picozip_free_mem()` and `picozip_free_path()` respectively.

> [!IMPORTANT]  
> Calling `picozip_free()` does not call `picozip_end()`, neither does calling `picozip_end()`
> calls `picozip_free()`. You must call the two functions in the correct sequence.

## License

This library is licensed under the MIT license.