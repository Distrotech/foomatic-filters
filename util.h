
#ifndef util_h
#define util_h

/* TODO write replacements for strcasecmp() etc. for non-gnu systems */
#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>

extern const char* shellescapes;

int isempty(const char *string);
int prefixcmp(const char *str, const char *prefix);
int prefixcasecmp(const char *str, const char *prefix);

int startswith(const char *str, const char *prefix);
int endswith(const char *str, const char *postfix);

void strlower(char *dest, size_t destlen, const char *src);

/*
 * Like strncpy, but omits characters for which omit_func returns true
 * It also assures that dest is zero terminated.
 * Returns a pointer to the position in 'src' right after the last byte that has been copied.
 */
const char * strncpy_omit(char* dest, const char* src, size_t n, int (*omit_func)(int));

int omit_unprintables(int c);
int omit_shellescapes(int c);
int omit_specialchars(int c);
int omit_whitespace(int c);

/* TODO check for platforms which already have strlcpy and strlcat */

/* Copy at most size-1 characters from src to dest
   dest will always be \0 terminated (unless size == 0)
   returns strlen(src) */
size_t strlcpy(char *dest, const char *src, size_t size);
size_t strlcat(char *dest, const char *src, size_t size);

/* Replace all occurences of each of the characters in 'chars' by 'repl' */
void strrepl(char *str, const char *chars, char repl);

/* Replace all occurences of each of the characters in 'chars' by 'repl',
   but do not allow consecutive 'repl' chars */
void strrepl_nodups(char *str, const char *chars, char repl);

/* clears 'str' with \0s */
void strclr(char *str);

char * strnchr(const char *str, int c, size_t n);

void escapechars(char *dest, size_t size, const char *src, const char *esc_chars);

/* copies characters from 'src' to 'dest', until 'src' contains a character from 'stopchars'
   will not copy more than 'max' chars
   dest will be zero terminated in either case
   returns a pointer to the position right after the last byte that has been copied
*/
const char * strncpy_tochar(char *dest, const char *src, size_t max, const char *stopchars);

/* extracts the base name of 'path', i.e. only the filename, without path or extension */
void file_basename(char *dest, const char *path, size_t dest_size);

/* if 'path' is relative, prepend cwd */
void make_absolute_path(char *path, int len);


/* Dynamic string */
typedef struct {
    char *data;
    size_t len;
    size_t alloc;
} dstr_t;

dstr_t * create_dstr();
void free_dstr(dstr_t *ds);
void dstrclear(dstr_t *ds);
void dstrcpyf(dstr_t *ds, const char *src, ...);
void dstrcatf(dstr_t *ds, const char *src, ...);
size_t fgetdstr(dstr_t *ds, FILE *stream); /* returns number of characters read */

#endif
