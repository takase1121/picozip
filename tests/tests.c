#include "greatest.h"
#include "picozip.h"

#if defined(_WIN32)
#define PICOZIP__WIN
#include <sys/utime.h>
#include <sys/stat.h>

#define utime _utime
#define stat _stat
#define utimbuf _utimbuf

#elif defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#define PICOZIP__UNIX
#include <utime.h>
#include <sys/stat.h>

#endif

static size_t num_alloc_success = -1;
static size_t num_write_success = -1;
static picozip_file *file = NULL;

typedef struct file_entry
{
    uint16_t flag, extra_field_len, comment_len;
    int check_mod_time;
    time_t mod_time;
    uint32_t crc32, size, local_offset;
    const char *const filename;
    const char *const extra_field;
    const char *const comment;
    const char *const data;
} file_entry;

/* utilities to convert POSIX time to DOS time */
static void time_t_dostime(time_t current_time, uint16_t *dos_date, uint16_t *dos_time)
{
    struct tm *tm = localtime(&current_time);
    if (tm->tm_year < 80)
    {
        /* clamp the timestamp to 1980-1-1 00:00:00 to avoid any underflow */
        tm->tm_year = 80;
        tm->tm_sec = tm->tm_min = tm->tm_hour = tm->tm_mon = 0;
        tm->tm_mday = 1;
    }
    *dos_time = (uint16_t)(((tm->tm_hour << 11) & 0xf800) | ((tm->tm_min << 5) & 0x7e0) | ((tm->tm_sec >> 1) & 0x1f));
    *dos_date = (uint16_t)((((tm->tm_year + 1900 - 1980) << 9) & 0xfe00) | (((tm->tm_mon + 1) << 5) & 0x1e0) | (tm->tm_mday & 0x1f));
}

TEST assert_zip_file(file_entry *entries, size_t entry_len, uint8_t *comment, size_t comment_len)
{
#define READ_LE32(D, O) ((uint32_t)(((D)[(O)] << 0) | ((D)[(O) + 1] << 8) | ((D)[(O) + 2] << 16) | ((D)[(O) + 3] << 24)))
#define READ_LE16(D, O) ((uint16_t)(((D)[(O)] << 0) | ((D)[(O) + 1] << 8)))

#define ZIP_MAGIC 0x04034b50
#define ZIP_CENTRAL_MAGIC 0x02014b50
#define ZIP_EOCD_MAGIC 0x06054b50
#define ZIP_DATADESC_MAGIC 0x08074b50

    uint8_t *data;
    size_t size, i, cd_start_offset, offset;
    uint16_t dos_date, dos_time;

    ASSERT_EQ(0, picozip_end(file));
    size = picozip_get_mem(file, (void **)&data);
    ASSERT_NEQ(NULL, data);

    /* check for local header and content */
    for (offset = i = 0; i < entry_len; i++)
    {
        ASSERT_EQ(ZIP_MAGIC, READ_LE32(data, offset));           /* magic */
        ASSERT_EQ(0x14, READ_LE16(data, offset + 4));            /* version (2.0) */
        ASSERT_EQ(entries[i].flag, READ_LE16(data, offset + 6)); /* flags */
        ASSERT_EQ(0, READ_LE16(data, offset + 8));               /* compression */
        if (entries[i].check_mod_time)
        {
            time_t_dostime(entries[i].mod_time, &dos_date, &dos_time);
            ASSERT_EQ(dos_time, READ_LE16(data, offset + 10)); /* modtime */
            ASSERT_EQ(dos_date, READ_LE16(data, offset + 12)); /* moddate */
        }
        if (entries[i].flag == (1 << 3))
        {
            /* when data descriptor is used, crc, comp and uncomp is 0 */
            ASSERT_EQ(0, READ_LE32(data, offset + 14)); /* crc32 */
            ASSERT_EQ(0, READ_LE32(data, offset + 18)); /* comp size */
            ASSERT_EQ(0, READ_LE32(data, offset + 22)); /* uncomp size */
        }
        else
        {
            /* check crc, size normally */
            ASSERT_EQ(entries[i].crc32, READ_LE32(data, offset + 14)); /* crc32 */
            ASSERT_EQ(entries[i].size, READ_LE32(data, offset + 18));  /* comp size */
            ASSERT_EQ(entries[i].size, READ_LE32(data, offset + 22));  /* uncomp size */
        }

        ASSERT_EQ(strlen(entries[i].filename), READ_LE16(data, offset + 26));                /* filename length */
        ASSERT_EQ(entries[i].extra_field_len, READ_LE16(data, offset + 28));                 /* extra field length */
        ASSERT_MEM_EQ(entries[i].filename, data + offset + 30, strlen(entries[i].filename)); /* filename */

        if (entries[i].extra_field)
            ASSERT_MEM_EQ(entries[i].extra_field, data + offset + 30 + strlen(entries[i].filename), entries[i].extra_field_len); /* extra fields */

        ASSERT_MEM_EQ(entries[i].data, data + offset + 30 + strlen(entries[i].filename) + entries[i].extra_field_len, entries[i].size); /* actual data */

        /* store offset for verification later */
        entries[i].local_offset = offset;
        offset += 30 + strlen(entries[i].filename) + entries[i].extra_field_len + entries[i].size;

        /* check if data descriptor is present */
        if (entries[i].flag == (1 << 3))
        {
            ASSERT_EQ(ZIP_DATADESC_MAGIC, READ_LE32(data, offset));
            ASSERT_EQ(entries[i].crc32, READ_LE32(data, offset + 4));
            ASSERT_EQ(entries[i].size, READ_LE32(data, offset + 8));
            ASSERT_EQ(entries[i].size, READ_LE32(data, offset + 12));
            offset += 16;
        }
    }
    cd_start_offset = offset;
    /* check for global header */
    for (i = 0; i < entry_len; i++)
    {
        ASSERT_EQ(ZIP_CENTRAL_MAGIC, READ_LE32(data, offset));   /* magic */
        ASSERT_EQ(0, READ_LE16(data, offset + 4));               /* version created */
        ASSERT_EQ(0x14, READ_LE16(data, offset + 6));            /* version (2.0) */
        ASSERT_EQ(entries[i].flag, READ_LE16(data, offset + 8)); /* flags */
        ASSERT_EQ(0, READ_LE16(data, offset + 10));              /* compression */
        if (entries[i].check_mod_time)
        {
            time_t_dostime(entries[i].mod_time, &dos_date, &dos_time);
            ASSERT_EQ(dos_time, READ_LE16(data, offset + 12)); /* modtime */
            ASSERT_EQ(dos_date, READ_LE16(data, offset + 14)); /* moddate */
        }
        ASSERT_EQ(entries[i].crc32, READ_LE32(data, offset + 16));                           /* crc32 */
        ASSERT_EQ(entries[i].size, READ_LE32(data, offset + 20));                            /* comp size */
        ASSERT_EQ(entries[i].size, READ_LE32(data, offset + 24));                            /* uncomp size */
        ASSERT_EQ(strlen(entries[i].filename), READ_LE16(data, offset + 28));                /* filename length */
        ASSERT_EQ(entries[i].extra_field_len, READ_LE16(data, offset + 30));                 /* extra field length */
        ASSERT_EQ(entries[i].comment_len, READ_LE16(data, offset + 32));                     /* file comment length */
        ASSERT_EQ(0, READ_LE16(data, offset + 34));                                          /* disk start*/
        ASSERT_EQ(0, READ_LE16(data, offset + 36));                                          /* internal attr */
        ASSERT_EQ(0, READ_LE32(data, offset + 38));                                          /* external attr */
        ASSERT_EQ(entries[i].local_offset, READ_LE32(data, offset + 42));                    /* local header offset */
        ASSERT_MEM_EQ(entries[i].filename, data + offset + 46, strlen(entries[i].filename)); /* filename */
        if (entries[i].extra_field)
            ASSERT_MEM_EQ(entries[i].extra_field, data + offset + 46 + strlen(entries[i].filename), entries[i].extra_field_len); /* extra fields */
        if (entries[i].comment)
            ASSERT_MEM_EQ(entries[i].comment, data + offset + 46 + strlen(entries[i].filename) + entries[i].extra_field_len, entries[i].comment_len); /* comment */
        offset += 46 + strlen(entries[i].filename) + entries[i].extra_field_len + entries[i].comment_len;
    }
    /* validate central header */
    ASSERT_EQ(ZIP_EOCD_MAGIC, READ_LE32(data, offset));                /* magic */
    ASSERT_EQ(0, READ_LE16(data, offset + 4));                         /* disk number */
    ASSERT_EQ(0, READ_LE16(data, offset + 6));                         /* disk number with CD */
    ASSERT_EQ(entry_len, READ_LE16(data, offset + 8));                 /* number of entries */
    ASSERT_EQ(entry_len, READ_LE16(data, offset + 10));                /* total number of entries */
    ASSERT_EQ(offset - cd_start_offset, READ_LE32(data, offset + 12)); /* cd size */
    ASSERT_EQ(cd_start_offset, READ_LE32(data, offset + 16));          /* cd offset */
    if (comment)
    {
        ASSERT_EQ(comment_len, READ_LE16(data, offset + 20)); /* comment length */
        ASSERT_MEM_EQ(comment, data + offset + 22, comment_len);
    }
    offset += 22 + comment_len;
    ASSERT_EQ(size, offset);

    PASS();
}

static void mem_setup_cb(void *data)
{
    picozip_new_mem(&file);
}

static void mem_teardown_cb(void *data)
{
    picozip_free_mem(file);
    file = NULL;
}

TEST test_picozip_new_mem(void)
{
    ASSERT_NEQ(NULL, file);
    PASS();
}

TEST test_picozip_new_mem_einval(void)
{
    ASSERT_NEQ(0, picozip_new_mem(NULL));
    PASS();
}

TEST test_picozip_end(void)
{
    size_t size;
    uint8_t *mem = NULL;

    ASSERT_EQ(PICOZIP_OK, picozip_end(file));
    size = picozip_get_mem(file, (void **)&mem);
    ASSERT_EQ(22, size); /* only the EOCD header */
    ASSERT_MEM_EQ((
                      "\x50\x4b\x05\x06" /* magic */
                      "\x00\x00"         /* disk number */
                      "\x00\x00"         /* disk number (CD) */
                      "\x00\x00"         /* number of records */
                      "\x00\x00"         /* total number of records */
                      "\x00\x00\x00\x00" /* CD size */
                      "\x00\x00\x00\x00" /* CD offset */
                      "\x00\x00"         /* comment length */
                      ),
                  mem, 22);
    PASS();
}

TEST test_picozip_end_einval(void)
{
    ASSERT_EQ(PICOZIP_EINVAL, picozip_end(NULL));
    PASS();
}

TEST test_picozip_end_ex(void)
{
    size_t size;
    uint8_t *mem = NULL;

    ASSERT_EQ(PICOZIP_OK, picozip_end_ex(file, "this is a comment", 17));
    size = picozip_get_mem(file, (void **)&mem);
    ASSERT_EQ(39, size); /* only the EOCD header + comment (17) */
    ASSERT_MEM_EQ((
                      "\x50\x4b\x05\x06"  /* magic */
                      "\x00\x00"          /* disk number */
                      "\x00\x00"          /* disk number (CD) */
                      "\x00\x00"          /* number of records */
                      "\x00\x00"          /* total number of records */
                      "\x00\x00\x00\x00"  /* CD size */
                      "\x00\x00\x00\x00"  /* CD offset */
                      "\x11\x00"          /* comment length */
                      "this is a comment" /* comment */
                      ),
                  mem, 39);
    PASS();
}

TEST test_picozip_end_ex_einval(void)
{
    ASSERT_EQ(PICOZIP_EINVAL, picozip_end_ex(NULL, "this is a comment", 17));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_end_ex(file, NULL, 17));
    PASS();
}

TEST test_picozip_new_entry_mem(void)
{
    file_entry entries[] = {
        {
            .filename = "test.txt",
            .flag = 0,
            .size = 11,
            .check_mod_time = 0, /* don't check for modtime */
            .mod_time = 0,
            .extra_field = NULL, /* don't check for extra fields */
            .extra_field_len = 9,
            .data = "hello world",
            .crc32 = 0xf2b5ee7a,
            .comment_len = 0,
            .comment = NULL,
        },
        {
            .filename = "magic.txt",
            .flag = 0,
            .size = 4,
            .check_mod_time = 0,
            .mod_time = 0,
            .extra_field = NULL, /* don't check for extra fields */
            .extra_field_len = 9,
            .data = "\x01\x15\x00\x04",
            .crc32 = 0x7B87E204,
            .comment_len = 0,
            .comment = NULL,
        },
    };
    ASSERT_EQ(0, picozip_new_entry_mem(file, "test.txt", (uint8_t *)"hello world", 11));
    ASSERT_EQ(0, picozip_new_entry_mem(file, "magic.txt", (uint8_t *)"\x01\x15\x00\x04", 4));
    CHECK_CALL(assert_zip_file(entries, 2, NULL, 0));
    PASS();
}

TEST test_picozip_new_entry_mem_einval(void)
{
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_mem(NULL, "test.txt", (uint8_t *)"hello world", 11));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_mem(file, NULL, (uint8_t *)"hello world", 11));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_mem(file, "test.txt", NULL, 11));
    PASS();
}

TEST test_picozip_new_entry_mem_ex(void)
{
    file_entry entries[] = {
        {
            .filename = "lorem.txt",
            .flag = 0,
            .size = 25,
            .check_mod_time = 1,
            .mod_time = 1730559952,
            .extra_field = "UT\x05\x00\x01\xD0\x3F\x26\x67", /* UT, 5, mod time set (1), 1730559952 */
            .extra_field_len = 9,
            .data = "lorem ipsum dolor si amet",
            .crc32 = 0x29AFAD85,
            .comment_len = 0,
            .comment = NULL,
        },
        {
            .filename = "magic.txt",
            .flag = 0,
            .size = 4,
            .check_mod_time = 1,
            .mod_time = 0,
            .extra_field = "UT\x05\x00\x01\x00\x00\x00\x00", /* UT, 5, mod time set (1), 0 */
            .extra_field_len = 9,
            .data = "\x01\x15\x00\x04",
            .crc32 = 0x7B87E204,
            .comment_len = 21,
            .comment = "this is a binary file",
        },
    };
    ASSERT_EQ(PICOZIP_OK, picozip_new_entry_mem_ex(file, "lorem.txt", (uint8_t *)"lorem ipsum dolor si amet", 25, 1730559952, NULL, 0));
    ASSERT_EQ(PICOZIP_OK, picozip_new_entry_mem_ex(file, "magic.txt", (uint8_t *)"\x01\x15\x00\x04", 4, 0, "this is a binary file", 21));
    CHECK_CALL(assert_zip_file(entries, 2, NULL, 0));
    PASS();
}

TEST test_picozip_new_entry_mem_ex_einval(void)
{
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_mem_ex(NULL, "test.txt", (uint8_t *)"hello world", 11, 0, "this is a comment", 17));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_mem_ex(file, NULL, (uint8_t *)"hello world", 11, 0, "this is a comment", 17));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_mem_ex(file, "test.txt", NULL, 11, 0, "this is a comment", 17));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_mem_ex(file, "test.txt", (uint8_t *)"hello world", 11, 0, NULL, 17));
    PASS();
}

TEST test_picozip_free_mem(void)
{
    picozip_file *file;
    ASSERT_EQ(PICOZIP_OK, picozip_new_mem(&file));
    ASSERT_NEQ(NULL, file);
    ASSERT_EQ(0, picozip_free_mem(file));
    PASS();
}

TEST test_picozip_free_mem_einval(void)
{
    ASSERT_EQ(PICOZIP_EINVAL, picozip_free_mem(NULL));
    PASS();
}

TEST test_picozip_new_entry_path(void)
{
    file_entry entries[] = {
        {
            .filename = "test.txt",
            .flag = 1 << 3,
            .size = 12,
            .check_mod_time = 1,
            .mod_time = 0,
            .extra_field = "UT\x05\x00\x01\x00\x00\x00\x00", /* UT, 5, mod time set (1), 0 */
            .extra_field_len = 9,
            .data = "hello world!",
            .crc32 = 0xFC4B3D92,
            .comment_len = 0,
            .comment = NULL,
        },
        {
            .filename = "test2.txt",
            .flag = 1 << 3,
            .size = 11,
            .check_mod_time = 1,
            .mod_time = 1730609280,
            .extra_field = "UT\x05\x00\x01\x80\x00\x27\x67", /* UT, 5, mod time set (1), 1730609280 */
            .extra_field_len = 9,
            .data = "zip library",
            .crc32 = 0x903E8D9F,
            .comment_len = 7,
            .comment = "comment",
        },
    };

    ASSERT_EQ(0, utime("tests/test.txt", (struct utimbuf *)&(struct utimbuf){.actime = entries[0].mod_time, .modtime = entries[0].mod_time}));
    ASSERT_EQ(0, utime("tests/test2.txt", (struct utimbuf *)&(struct utimbuf){.actime = entries[1].mod_time, .modtime = entries[1].mod_time}));
    ASSERT_EQ(PICOZIP_OK, picozip_new_entry_path(file, "test.txt", "tests/test.txt", NULL, 0));
    ASSERT_EQ(PICOZIP_OK, picozip_new_entry_path(file, "test2.txt", "tests/test2.txt", "comment", 7));
    CHECK_CALL(assert_zip_file(entries, 2, NULL, 0));
    PASS();
}

TEST test_picozip_new_entry_path_einval(void)
{
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_path(NULL, "test.txt", "tests/test.txt", NULL, 0));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_path(file, NULL, "tests/test.txt", NULL, 0));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_path(file, "test.txt", NULL, NULL, 0));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_entry_path(file, "test.txt", "tests/test.txt", NULL, 12));
    ASSERT_NEQ(PICOZIP_OK, picozip_new_entry_path(file, "test.txt", "invalid file.txt", NULL, 0));
    PASS();
}

TEST test_picozip_new_path(void)
{
    picozip_file *f;
    FILE *fptr;
    long size;

    ASSERT_EQ(PICOZIP_OK, picozip_new_path(&f, "test.zip", "wb"));
    ASSERT_EQ(PICOZIP_OK, picozip_end(f));
    ASSERT_EQ(PICOZIP_OK, picozip_free_path(f));

    fptr = fopen("test.zip", "rb");
    ASSERT_NEQ(NULL, fptr);
    ASSERT_EQ(0, fseek(fptr, 0, SEEK_END));
    size = ftell(fptr);
    fclose(fptr);
    ASSERT_EQ(22, size);
    PASS();
}

TEST test_picozip_new_path_einval()
{
    picozip_file *f;
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_path(NULL, "test.zip", "wb"));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_path(&f, NULL, "wb"));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new_path(&f, "test.zip", NULL));
    ASSERT_NEQ(PICOZIP_OK, picozip_new_path(&f, "tests", "wb"));
    PASS();
}

TEST test_picozip_free_path()
{
    picozip_file *f;
    ASSERT_EQ(PICOZIP_OK, picozip_new_path(&f, "test.zip", "wb"));
    ASSERT_EQ(PICOZIP_OK, picozip_free_path(f));
    PASS();
}

TEST test_picozip_free_path_einval()
{
    ASSERT_EQ(PICOZIP_EINVAL, picozip_free_path(NULL));
    PASS();
}

TEST test_picozip_get_mem()
{
    void *mem;
    ASSERT_EQ(PICOZIP_OK, picozip_end(file));
    ASSERT_EQ(22, picozip_get_mem(file, &mem));
    ASSERT_NEQ(NULL, mem);

    mem = NULL;
    ASSERT_EQ(0, picozip_get_mem(NULL, &mem));
    ASSERT_EQ(NULL, mem);

    ASSERT_EQ(0, picozip_get_mem(file, NULL));
    PASS();
}

static void *custom_alloc(void *userdata, size_t size)
{
    if (!num_alloc_success)
        return NULL;
    num_alloc_success--;
    return malloc(size);
}

static void custom_free(void *userdata, void *mem)
{
    free(mem);
}

static size_t custom_write(void *userdata, const void *mem, size_t size)
{
    if (!num_write_success)
        return 0;
    num_write_success--;
    return size;
}

TEST test_picozip_new()
{
    num_alloc_success = num_write_success = -1; /* unlimited */
    ASSERT_EQ(PICOZIP_OK, picozip_new(&file, custom_write, custom_alloc, custom_free, NULL));
    ASSERT_EQ(PICOZIP_OK, picozip_free(file));
    PASS();
}

TEST test_picozip_new_einval()
{
    num_alloc_success = num_write_success = -1; /* unlimited */
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new(NULL, custom_write, custom_alloc, custom_free, NULL));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new(&file, NULL, custom_alloc, custom_free, NULL));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new(&file, custom_write, NULL, custom_free, NULL));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_new(&file, custom_write, custom_alloc, NULL, NULL));

    num_alloc_success = 0; /* cannot allocate picozip_file */
    ASSERT_EQ(PICOZIP_ENOMEM, picozip_new(&file, custom_write, custom_alloc, custom_free, NULL));

    PASS();
}

TEST test_picozip_free()
{
    num_alloc_success = num_write_success = -1; /* unlimited */
    ASSERT_EQ(PICOZIP_OK, picozip_new(&file, custom_write, custom_alloc, custom_free, NULL));
    ASSERT_EQ(PICOZIP_EINVAL, picozip_free(NULL));
    ASSERT_EQ(PICOZIP_OK, picozip_free(file));
    PASS();
}

TEST test_picozip_alloc_error()
{
    num_alloc_success = num_write_success = -1; /* unlimited */
    ASSERT_EQ(PICOZIP_OK, picozip_new(&file, custom_write, custom_alloc, custom_free, NULL));

    num_alloc_success = 0; /* test entry list allocation */
    ASSERT_EQ(PICOZIP_ENOMEM, picozip_new_entry_mem(file, "test.txt", (uint8_t *)"hello", 5));

    num_alloc_success = 1; /* test entry allocation itself */
    ASSERT_EQ(PICOZIP_ENOMEM, picozip_new_entry_mem(file, "test.txt", (uint8_t *)"hello", 5));
    ASSERT_EQ(PICOZIP_OK, picozip_free(file));

    PASS();
}

TEST test_picozip_write_error()
{
    num_alloc_success = num_write_success = -1; /* unlimited */
    ASSERT_EQ(PICOZIP_OK, picozip_new(&file, custom_write, custom_alloc, custom_free, NULL));

    num_write_success = 0; /* test local entry (header) */
    ASSERT_EQ(PICOZIP_EIO, picozip_new_entry_mem(file, "test.txt", (uint8_t *)"hello", 5));

    num_write_success = 1; /* test local entry (comment) */
    ASSERT_EQ(PICOZIP_EIO, picozip_new_entry_mem(file, "test.txt", (uint8_t *)"hello", 5));

    num_write_success = 2; /* test local entry (content) */
    ASSERT_EQ(PICOZIP_EIO, picozip_new_entry_mem(file, "test.txt", (uint8_t *)"hello", 5));

    num_write_success = 0; /* test writing eocd (header) */
    ASSERT_EQ(PICOZIP_EIO, picozip_end(file));

    num_write_success = 1; /* test writing eocd (comment) */
    ASSERT_EQ(PICOZIP_EIO, picozip_end_ex(file, "test", 4));

    num_write_success = -1; /* create an entry to test writing cd entries */
    ASSERT_EQ(PICOZIP_OK, picozip_new_entry_mem(file, "test.txt", (uint8_t *)"hello", 5));

    num_write_success = 0; /* test writing cd entries (header) */
    ASSERT_EQ(PICOZIP_EIO, picozip_end(file));

    num_write_success = 1; /* test writing cd entries (metadata) */
    ASSERT_EQ(PICOZIP_EIO, picozip_end(file));
    ASSERT_EQ(PICOZIP_OK, picozip_free(file));

    PASS();
}

SUITE(picozip_mem_path_tests)
{
    SET_SETUP(mem_setup_cb, NULL);
    SET_TEARDOWN(mem_teardown_cb, NULL);

    RUN_TEST(test_picozip_end);
    RUN_TEST(test_picozip_end_einval);
    RUN_TEST(test_picozip_end_ex);
    RUN_TEST(test_picozip_end_ex_einval);
    RUN_TEST(test_picozip_free_mem);
    RUN_TEST(test_picozip_free_mem_einval);

    RUN_TEST(test_picozip_new_mem);
    RUN_TEST(test_picozip_new_mem_einval);
    RUN_TEST(test_picozip_new_entry_mem);
    RUN_TEST(test_picozip_new_entry_mem_einval);
    RUN_TEST(test_picozip_new_entry_mem_ex);
    RUN_TEST(test_picozip_new_entry_mem_ex_einval);

    RUN_TEST(test_picozip_new_entry_path);
    RUN_TEST(test_picozip_new_entry_path_einval);

    RUN_TEST(test_picozip_new_path);
    RUN_TEST(test_picozip_new_path_einval);
    RUN_TEST(test_picozip_free_path);
    RUN_TEST(test_picozip_free_path_einval);

    RUN_TEST(test_picozip_get_mem);
}

SUITE(picozip_tests)
{
    RUN_TEST(test_picozip_new);
    RUN_TEST(test_picozip_new_einval);
    RUN_TEST(test_picozip_free);
    RUN_TEST(test_picozip_alloc_error);
    RUN_TEST(test_picozip_write_error);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(picozip_tests);
    RUN_SUITE(picozip_mem_path_tests);
    GREATEST_MAIN_END();
}