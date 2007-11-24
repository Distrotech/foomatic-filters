
#include "util.h"
#include "foomaticrip.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>


const char* shellescapes = "|<>&!$\'\"#*?()[]{}";

int prefixcmp(const char *str, const char *prefix)
{
    return strncmp(str, prefix, strlen(prefix));
}

int prefixcasecmp(const char *str, const char *prefix)
{
    return strncasecmp(str, prefix, strlen(prefix));
}

int startswith(const char *str, const char *prefix)
{
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

int endswith(const char *str, const char *postfix)
{
    int slen = strlen(str);
    int plen = strlen(postfix);
    const char *pstr;

    if (slen < plen)
        return 0;

    pstr = &str[slen - plen];
    return strcmp(str, postfix) == 0;
}

void strlower(char *dest, size_t destlen, const char *src)
{
    char *pdest = dest;
    const char *psrc = src;
    while (*psrc && --destlen > 0)
    {
        *pdest = tolower(*psrc);
        pdest++;
        psrc++;
    }
    *pdest = '\0';
}

int isempty(const char *string)
{
    return string && string[0] == '\0';
}

const char * strncpy_omit(char* dest, const char* src, size_t n, int (*omit_func)(int))
{
    const char* psrc = src;
    char* pdest = dest;
    int cnt = n -1;
    if (!pdest)
        return NULL;
    if (psrc) {
        while (*psrc != 0 && cnt > 0) {
            if (!omit_func(*psrc)) {
                *pdest = *psrc;
                pdest++;
                cnt--;
            }
            psrc++;
        }
    }
    *pdest = '\0';
    return psrc;
}
int omit_unprintables(int c) { return c>= '\x00' && c <= '\x1f'; }
int omit_shellescapes(int c) { return strchr(shellescapes, c) != NULL; }
int omit_specialchars(int c) { return omit_unprintables(c) || omit_shellescapes(c); }
int omit_whitespace(int c) { return c == ' ' || c == '\t'; }

size_t strlcpy(char *dest, const char *src, size_t size)
{
    char *pdest = dest;
    const char *psrc = src;
    
    if (size) {
        while (--size && (*pdest++ = *psrc++) != '\0');
        *pdest = '\0';
    }
    if (!size)
        while (*psrc++);
    return (psrc - src -1);
}

size_t strlcat(char *dest, const char *src, size_t size)
{
    char *pdest = dest;
    const char *psrc = src;
    size_t i = size;
    size_t len;
    
    while (--i && *pdest)
        pdest++;
    len = pdest - dest;

    if (!i)
        return strlen(src) + len;
    
    while (i-- && *psrc)
        *pdest++ = *psrc++;
    *pdest = '\0';
    
    return len + (psrc - src);
}

void strrepl(char *str, const char *chars, char repl)
{
    char *p = str;
    
    while (*p) {
        if (strchr(chars, *p))
            *p = repl;
        p++;
    }
}

void strrepl_nodups(char *str, const char *chars, char repl)
{
    char *pstr = str;
    char *p = str;
    int prev = 0;

    while (*pstr) {
        if (strchr(chars, *pstr) || *pstr == repl) {
            if (!prev) {
                *p = repl;
                p++;
                prev = 1;
            }
        }
        else {
            *p = *pstr;
            p++;
            prev = 0;
        }
        pstr++;
    }
    *p = '\0';
}

void strclr(char *str)
{
    while (*str) {
        *str = '\0';
        str++;
    }
}

char * strnchr(const char *str, int c, size_t n)
{
    char *p = (char*)str;
    
    while (*p && --n > 0) {
        if (*p == (char)c)
            return p;
        p++;
    }
    return p;
}

void escapechars(char *dest, size_t size, const char *src, const char *esc_chars)
{
    const char *psrc = src;

    while (*psrc && --size > 0) {
        if (strchr(esc_chars, *psrc))
            *dest++ = '\\';
        *dest++ = *psrc++;
    }
}

const char * strncpy_tochar(char *dest, const char *src, size_t max, const char *stopchars)
{
    const char *psrc = src;
    char *pdest = dest;
    while (*psrc && --max > 0 && !strchr(stopchars, *psrc)) {
        *pdest = *psrc;
        pdest++;
        psrc++;
    }
    *pdest = '\0';
    return psrc +1;
}

void file_basename(char *dest, const char *path, size_t dest_size)
{
    const char *p = strrchr(path, '/');
    char *pdest = dest;
    if (!pdest)
        return;
    if (p)
        p += 1;
    else
        p = path;
    while (*p != 0 && *p != '.' && --dest_size > 0) {
        *pdest++ = *p++;
    }
    *pdest = '\0';
}

void make_absolute_path(char *path, int len)
{
    char *tmp, *cwd;
    
    if (path[0] != '/') {
        tmp = malloc(len +1);
        strlcpy(tmp, path, len);
        
        cwd = malloc(len);
        getcwd(cwd, len);
        strlcpy(path, cwd, len);
        strlcat(path, "/", len);
        strlcat(path, tmp, len);
        
        free(tmp);
        free(cwd);
    }
}

/* 
 * Dynamic strings
 */
dstr_t * create_dstr()
{
    dstr_t *ds = malloc(sizeof(dstr_t));
    ds->len = 0;
    ds->alloc = 32;
    ds->data = malloc(ds->alloc);
    ds->data[0] = '\0';
    return ds;
}

void free_dstr(dstr_t *ds)
{
    free(ds->data);
    free(ds);
}

void dstrclear(dstr_t *ds)
{
    ds->len = 0;
    ds->data[0] = '\0';
}

void dstrcpyf(dstr_t *ds, const char *src, ...)
{
    va_list ap;
    size_t srclen;
    
    va_start(ap, src);
    srclen = vsnprintf(ds->data, ds->alloc, src, ap);
    va_end(ap);

    if (srclen >= ds->alloc) {
        do {
            ds->alloc *= 2;
        } while (srclen >= ds->alloc);

        va_start(ap, src);
        vsnprintf(ds->data, ds->alloc, src, ap);
        va_end(ap);       
    }
    
    ds->len = srclen;
}

void dstrcatf(dstr_t *ds, const char *src, ...)
{
    va_list ap;
    size_t restlen = ds->alloc - ds->len;
    size_t srclen;
    
    va_start(ap, src);
    srclen = vsnprintf(&ds->data[ds->len], restlen, src, ap);
    va_end(ap);
    
    if (srclen >= restlen) {
        do {
            ds->alloc *= 2;
            restlen = ds->alloc - ds->len;
        } while (srclen >= restlen);
        ds->data = realloc(ds->data, ds->alloc);
        
        va_start(ap, src);
        vsnprintf(&ds->data[ds->len], restlen, src, ap);
        va_end(ap);
    }
    
    ds->len += srclen;
}

size_t fgetdstr(dstr_t *ds, FILE *stream)
{
    int c;
    size_t cnt = 0;

    ds->len = 0;
    if (ds->alloc == 0) {
        ds->alloc = 256;
        ds->data = malloc(ds->alloc);
    }

    while ((c = fgetc(stream) != EOF)) {
        if (ds->len +1 == ds->alloc) {
            ds->alloc *= 2;
            ds->data = realloc(ds->data, ds->alloc);
        }
        ds->data[ds->len++] = (char)c;
        cnt ++;
        if (c == '\n')
            break;
    }
    ds->data[ds->len++] = '\0';
    return cnt;
}
