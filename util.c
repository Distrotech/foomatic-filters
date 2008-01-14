
#include "util.h"
#include "foomaticrip.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>


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
    return str ? (strncmp(str, prefix, strlen(prefix)) == 0) : 0;
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

const char * ignore_whitespace(const char *str)
{
    while (*str && isspace(*str))
        str++;
    return str;
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

    if (!src) {
        dest[0] = '\0';
        return 0;
    }
    
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

int is_true_string(const char *str)
{
    return str && (!strcmp(str, "1") || !strcasecmp(str, "Yes") ||
        !strcasecmp(str, "On") || !strcasecmp(str, "True"));
}

int is_false_string(const char *str)
{
    return str && (!strcmp(str, "0") || !strcasecmp(str, "No") ||
        !strcasecmp(str, "Off") || !strcasecmp(str, "False") ||
        !strcasecmp(str, "None"));
}

int digit(char c)
{
    if (c >= '0' && c <= '9')
        return (int)c - (int)'0';
    return -1;
}

int line_count(const char *str)
{
    int cnt = 0;
    while (*str) {
        if (*str == '\n')
            cnt++;
        str++;
    }
    return cnt;
}

int line_start(const char *str, int line_number)
{
    const char *p = str;
    while (*p && line_number > 0) {
        if (*p == '\n')
            line_number--;
        p++;
    }
    return p - str;
}

void unhexify(char *dest, size_t size, const char *src)
{
    char *pdest = dest;
    const char *psrc = src;
    long int n;
    char cstr[3];
    
    cstr[2] = '\0';

    while (*psrc && pdest - dest < size -1) {
        if (*psrc == '<') {
            psrc++;
            do {
                cstr[0] = *psrc++;
                cstr[1] = *psrc++;
                if (!isxdigit(cstr[0]) || !isxdigit(cstr[1])) {
                    printf("Error replacing hex notation in %s!\n", src);
                    break;
                }
                *pdest++ = (char)strtol(cstr, NULL, 16);
            } while (*psrc != '>');
            psrc++;
        }
        else 
            *pdest++ = *psrc++;
    }
    *pdest = '\0';
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

void dstrassure(dstr_t *ds, size_t alloc)
{
	if (ds->alloc < alloc) {
		ds->alloc = alloc;
		ds->data = realloc(ds->data, ds->alloc);
	}
}

void dstrcpy(dstr_t *ds, const char *src)
{
    size_t srclen = strlen(src);

    if (srclen >= ds->alloc) {
        do {
            ds->alloc *= 2;
        } while (srclen >= ds->alloc);
        ds->data = realloc(ds->data, ds->alloc);
    }

    strcpy(ds->data, src);
    ds->len = srclen;
}

void dstrncpy(dstr_t *ds, const char *src, size_t n)
{
    if (n +1 >= ds->alloc) {
        do {
            ds->alloc *= 2;
        } while (n +1 >= ds->alloc);
        ds->data = realloc(ds->data, ds->alloc);
    }

    strncpy(ds->data, src, n);
    ds->len = n;
    if (ds->data[ds->len] != '\0')
        ds->data[ds->len++] = '\0';
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
        ds->data = realloc(ds->data, ds->alloc);

        va_start(ap, src);
        vsnprintf(ds->data, ds->alloc, src, ap);
        va_end(ap);       
    }
    
    ds->len = srclen;
}

void dstrcat(dstr_t *ds, const char *src)
{
    size_t srclen = strlen(src);
    size_t newlen = ds->len + srclen;

    if (newlen >= ds->alloc) {
        do {
            ds->alloc *= 2;
        } while (newlen >= ds->alloc);
        ds->data = realloc(ds->data, ds->alloc);
    }

    memcpy(&ds->data[ds->len], src, srclen +1);
    ds->len = newlen;
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
        srclen = vsnprintf(&ds->data[ds->len], restlen, src, ap);
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
    
    while ((c = fgetc(stream)) != EOF) {
        if (ds->len +1 == ds->alloc) {
            ds->alloc *= 2;
            ds->data = realloc(ds->data, ds->alloc);
        }
        ds->data[ds->len++] = (char)c;
        cnt ++;
        if (c == '\n')
            break;
    }
    ds->data[ds->len] = '\0';
    return cnt;
}

void dstrreplace(dstr_t *ds, const char *find, const char *repl)
{
    char *p, *next;
    dstr_t *copy = create_dstr();
    size_t findlen = strlen(find);

    dstrcpy(copy, ds->data);
    dstrclear(ds);

    p = copy->data;
    while ((next = strstr(p, find))) {
        *next = '\0';
        next += findlen;
    
        dstrcatf(ds, "%s", p);
        dstrcatf(ds, "%s", repl);
		
		p = next;
    }

    dstrcatf(ds, "%s", p);
    free_dstr(copy);
}

void dstrprepend(dstr_t *ds, const char *str)
{
    dstr_t *copy = create_dstr();
    dstrcpy(copy, ds->data);
    dstrcpy(ds, str);
    dstrcatf(ds, "%s", copy->data);
    free_dstr(copy);
}

void dstrinsert(dstr_t *ds, int idx, const char *str)
{
    dstr_t *copy = create_dstr();
    size_t len = strlen(str);
    dstrcpy(copy, ds->data);

    if (idx >= ds->len)
        idx = ds->len;
    else if (idx < 0)
        idx = 0;

    if (ds->len + len >= ds->alloc) {
        do {
            ds->alloc *= 2;
        } while (ds->len + len >= ds->alloc);
        free(ds->data);
        ds->data = malloc(ds->alloc);
    }

    strncpy(ds->data, copy->data, idx);
    strcat(ds->data, str);
    strcat(ds->data, &copy->data[idx]);
    ds->len += len;
    free_dstr(copy);
}

void dstrremove(dstr_t *ds, int idx, size_t count)
{
    char *p1, *p2;
    
    if (idx + count >= ds->len)
        return;

    p1 = &ds->data[idx];
    p2 = &ds->data[idx + count];

    while (*p2) {
        *p1 = *p2;
        p1++;
        p2++;
    }
    *p1 = '\0';
}

void dstrcatline(dstr_t *ds, const char *str)
{
    const char *s = str;

    if (!*s)
        return;

    do {
        while (ds->len >= ds->alloc) {
            ds->alloc *= 2;
            ds->data = realloc(ds->data, ds->alloc);
        }
        ds->data[ds->len++] = *s == '\r' ? '\n' : *s;
        s++;
    } while (*s && *s != '\r' && *s != '\n');

    ds->data[ds->len++] = '\0';
}

int dstrendswith(dstr_t *ds, const char *str)
{
    int len = strlen(str);
    char *pstr;

    if (ds->len < len)
        return 0;
    pstr = &ds->data[ds->len - len];
    return strcmp(pstr, str) == 0;

}

void dstrfixnewlines(dstr_t *ds)
{
    if (ds->data[ds->len -1] == '\r') {
        ds->data[ds->len -1] = '\n';
    }
    else if (ds->data[ds->len -2] == '\r') {
        ds->data[ds->len -1] = '\n';
        ds->data[ds->len -2] = '\0';
        ds->len -= 1;
    }
}

void dstrremovenewline(dstr_t *ds)
{
    if (ds->data[ds->len -1] == '\r' || ds->data[ds->len -1] == '\n') {
        ds->data[ds->len -1] = '\0';
        ds->len -= 1;
    }
    else if (ds->data[ds->len -2] == '\r') {
        ds->data[ds->len -2] = '\0';
        ds->len -= 2;
    }
}

void dstrtrim_right(dstr_t *ds)
{
    if (!ds->len)
        return;
    
    while (isspace(ds->data[ds->len -1]))
        ds->len -= 1;
    ds->data[ds->len] = '\0';
}



/* 
 *  LIST
 */
 
list_t * list_create()
{
    list_t *l = malloc(sizeof(list_t));
    l->first = NULL;
    l->last = NULL;
    return l;
}

list_t * list_create_from_array(int count, void ** data)
{
    int i;
    list_t *l = list_create();
    
    for (i = 0; i < count; i++)
        list_append(l, data[i]);
        
    return l;
}

void list_free(list_t *list)
{
    listitem_t *i = list->first, *tmp;
    while (i) {
        tmp = i->next;
        free(i);
        i = tmp;
    }
}

size_t list_item_count(list_t *list)
{
    size_t cnt = 0;
    listitem_t *i;    
    for (i = list->first; i; i = i->next)
        cnt++;
    return cnt;
}

list_t * list_copy(list_t *list)
{
    list_t *l = list_create();
    listitem_t *i;

    for (i = list->first; i; i = i->next)
        list_append(l, i->data);
    return l;
}

void list_prepend(list_t *list, void *data)
{
    listitem_t *item;

    assert(list);
    
    item = malloc(sizeof(listitem_t));
    item->data = data;
    item->prev = NULL;
    
    if (list->first) {
        item->next = list->first;
        list->first->next = item;
        list->first = item;
    }
    else {
        item->next = NULL;
        list->first = item;
        list->last = item;
    }
}

void list_append(list_t *list, void *data)
{
    listitem_t *item;

    assert(list);
    
    item = malloc(sizeof(listitem_t));
    item->data = data;
    item->next = NULL;
    
    if (list->last) {
        item->prev = list->last;
        list->last->next = item;
        list->last = item;
    }
    else {
        item->prev = NULL;
        list->first = item;
        list->last = item;
    }    
}

void list_remove(list_t *list, listitem_t *item)
{
    assert(item);

    if (item->prev)
        item->prev->next = item->next;
    if (item->next)
        item->next->prev = item->prev;
    if (item == list->first)
        list->first = item->next;
    if (item == list->last)
        list->last = item->prev;

    free(item);
}

listitem_t * list_get(list_t *list, int idx)
{
    listitem_t *i;
    for (i = list->first; i && idx; i = i->next)
        idx--;
    return NULL;
}

listitem_t * arglist_find(list_t *list, const char *name)
{
    listitem_t *i;    
    for (i = list->first; i; i = i->next) {
        if (!strcmp((const char*)i->data, name))
            return i;
    }
    return NULL;
}

listitem_t * arglist_find_prefix(list_t *list, const char *name)
{
    listitem_t *i;    
    for (i = list->first; i; i= i->next) {
        if (!prefixcmp((const char*)i->data, name))
            return i;
    }
    return NULL;
}


char * arglist_get_value(list_t *list, const char *name)
{
    listitem_t *i;
    char *p;
          
    for (i = list->first; i; i = i->next) {
        if (i->next && !strcmp(name, (char*)i->data))
            return (char*)i->next->data;
        else if (!prefixcmp(name, (char*)i->data)) {
            p = &((char*)i->data)[strlen(name)];
            return *p == '=' ? p +1 : p;
        }
    }
    return NULL;
}

char * arglist_get(list_t *list, int idx)
{
    listitem_t *i = list_get(list, idx);
    return i ? (char*)i->data : NULL;
}

int arglist_remove(list_t *list, const char *name)
{
    listitem_t *i;
    char *i_name;
    
    for (i = list->first; i; i = i->next) {
        i_name = (char*)i->data;
        if (i->next && !strcmp(name, i_name)) {
            list_remove(list, i->next);
            list_remove(list, i);
            return 1;
        }
        else if (!prefixcmp(name, i_name)) {
            list_remove(list, i);
            return 1;
        }
    }
    return 0;
}

int arglist_remove_flag(list_t *list, const char *name)
{
    listitem_t *i = arglist_find(list, name);
    if (i) {
        list_remove(list, i);
        return 1;
    }
    return 0;
}

