#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>

#include "line_protocol_parser.h"

//#define LP_DEBUG

/* Enable verbose debug printouts*/
#ifdef LP_DEBUG
#define LP_DEBUG_PRINT printf
#else
#define LP_DEBUG_PRINT(...)
#endif

/* (Used to override malloc/free in Python C extension) */
#ifndef LP_MALLOC
#define LP_MALLOC malloc
#endif
#ifndef LP_FREE
#define LP_FREE free
#endif

#define LP_MIN_TIMESTAMP (-9223372036854775806LL)
#define LP_MAX_TIMESTAMP 9223372036854775806LL
#define LP_MAX_KEY_LENGTH 65535

/* Used to indicate different components of a line */
enum _LP_Part {
    LP_MEASUREMENT,
    LP_TAG_KEY,
    LP_TAG_VALUE,
    LP_FIELD_KEY,
    LP_FIELD_VALUE
};

static int
can_escape_character(enum _LP_Part part, char next_char)
{
    switch (part) {
        case LP_MEASUREMENT:
            return next_char == ',' || next_char == ' ';
        case LP_TAG_KEY:
        case LP_TAG_VALUE:
        case LP_FIELD_KEY:
            return next_char == ',' || next_char == ' ' || next_char == '=';
        case LP_FIELD_VALUE:
            return next_char == '"';
    }

    return 0;
}

static int
is_scan_escaped(const char *line, size_t start, size_t index)
{
    /* InfluxDB v1.2's non-quoted scanners only look one byte back when
       deciding whether a structural delimiter is escaped. Keep quoted
       strings on the stricter parity-aware path, but match upstream here. */
    (void) start;
    return index > start && line[index - 1] == '\\';
}

static size_t
skip_whitespace(const char *line, size_t start, size_t end)
{
    /* InfluxDB v1.2 tolerated extra top-level whitespace before the
       measurement and before the fields/timestamp blocks. Keep that
       relaxation scoped to those explicit skip sites, not inside tokens. */
    while (start < end && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }

    return start;
}

static size_t
trim_trailing_spaces(const char *line, size_t start, size_t end)
{
    while (end > start && line[end - 1] == ' ') {
        end--;
    }

    return end;
}

/* Create a new key-value pair container */
static struct LP_Item*
new_item(void)
{
    struct LP_Item *output = NULL;
    output = LP_MALLOC(sizeof(*output));
    if (output == NULL) {
        return NULL;
    }
    output->key = NULL;
    output->type = LP_STRING;
    output->value.s = NULL;
    output->next_item = NULL;
    return output;
}

static void
free_item(struct LP_Item *item)
{
    struct LP_Item *tmp = NULL;
    while (item != NULL) {
        tmp = item->next_item;
        LP_FREE(item->key);
        if (item->type == LP_STRING) {
            LP_FREE(item->value.s);
        }
        LP_FREE(item);
        item = tmp;
    }
}

/* Assign the measurement string to the LP_Point struct */
static int
set_measurement(struct LP_Point *point, const char *line, size_t start, size_t end)
{
    size_t i, j;

    point->measurement = LP_MALLOC(end - start + 1);
    if (point->measurement == NULL) {
        return 0;
    }
    i = 0;
    for (j = start; j < end; j++) {
        if (line[j] == '\\' && j + 1 < end &&
            can_escape_character(LP_MEASUREMENT, line[j + 1])) {
            continue;
        }
        point->measurement[i] = line[j];
        i++;
    }
    point->measurement[i] = '\0';
    return 1;
}

static int
set_key(struct LP_Item *item, const char *line, size_t start, size_t end,
        enum _LP_Part part)
{
    size_t i, j;

    item->key = LP_MALLOC(end - start + 1);
    if (item->key == NULL) {
        return 0;
    }
    i = 0;
    for (j = start; j < end; j++) {
        if (line[j] == '\\' && j + 1 < end &&
            can_escape_character(part, line[j + 1])) {
            continue;
        }
        item->key[i] = line[j];
        i++;
    }
    item->key[i] = '\0';
    return 1;
}

static int
set_value(struct LP_Item *item, const char *line, size_t start, size_t end,
          enum _LP_Part part)
{
    size_t i, j;

    item->type = LP_STRING;
    item->value.s = LP_MALLOC(end - start + 1);
    if (item->value.s == NULL) {
        return 0;
    }
    i = 0;
    for (j = start; j < end; j++) {
        if (line[j] == '\\' && j + 1 < end && part != LP_FIELD_VALUE &&
            can_escape_character(part, line[j + 1])) {
            continue;
        }
        item->value.s[i] = line[j];
        i++;
    }
    item->value.s[i] = '\0';
    return 1;
}

static struct LP_Point*
new_point(void)
{
    struct LP_Point *output = NULL;
    output = LP_MALLOC(sizeof(*output));
    if (output == NULL) {
        return NULL;
    }
    output->measurement = NULL;
    output->fields = NULL;
    output->tags = NULL;
    output->has_time = false;
    output->time = 0;
    output->next_point = NULL;
    return output;
}

void
LP_free_point(struct LP_Point *point)
{
    struct LP_Point *tmp = NULL;
    while (point != NULL) {
        tmp = point->next_point;
        LP_FREE(point->measurement);
        free_item(point->fields);
        free_item(point->tags);
        LP_FREE(point);
        point = tmp;
    }
}

static size_t
search_measurement_end(const char *line, size_t start, size_t end)
{
    size_t i = start;

    if (start >= end || line[start] == ',' || line[start] == ' ') {
        return 0;
    }

    for (; i < end; i++) {
        LP_DEBUG_PRINT("search measurement: %c\n", line[i]);
        if (line[i] == '\n' || line[i] == '\r') {
            return 0;
        }
        if ((line[i] == ' ' || line[i] == ',') &&
            is_scan_escaped(line, start, i) == 0) {
            return i;
        }
    }

    return 0;
}

static size_t
search_tag_key_equal(const char *line, size_t start, size_t end)
{
    size_t i = start;

    if (start >= end || line[start] == ' ' || line[start] == ',' ||
        line[start] == '=') {
        return 0;
    }

    for (; i < end; i++) {
        LP_DEBUG_PRINT("search tag key: %c\n", line[i]);
        if (line[i] == '\n' || line[i] == '\r') {
            return 0;
        }
        if (line[i] == '=' && is_scan_escaped(line, start, i) == 0) {
            return i;
        }
        if ((line[i] == ' ' || line[i] == ',') &&
            is_scan_escaped(line, start, i) == 0) {
            return 0;
        }
    }

    return 0;
}

static size_t
search_tag_value_end(const char *line, size_t start, size_t end)
{
    size_t i = start;

    if (start >= end || line[start] == ' ' || line[start] == ',' ||
        line[start] == '=') {
        return 0;
    }

    for (; i < end; i++) {
        LP_DEBUG_PRINT("search tag value: %c\n", line[i]);
        if (line[i] == '\n' || line[i] == '\r') {
            return 0;
        }
        if (line[i] == '=' && is_scan_escaped(line, start, i) == 0) {
            return 0;
        }
        if ((line[i] == ' ' || line[i] == ',') &&
            is_scan_escaped(line, start, i) == 0) {
            return i;
        }
    }

    return 0;
}

static size_t
search_field_key_equal(const char *line, size_t start, size_t end)
{
    size_t i = start;

    if (start >= end || line[start] == ' ' || line[start] == ',' ||
        line[start] == '=') {
        return 0;
    }

    while (i < end) {
        LP_DEBUG_PRINT("search field key: %c\n", line[i]);
        if (line[i] == '\n' || line[i] == '\r') {
            return 0;
        }
        /* Match InfluxDB v1.2 scanFields(): a backslash escapes the next
           byte while scanning the fields block, regardless of what that
           byte is. That differs from the measurement/tag scanners. */
        if (line[i] == '\\' && i + 1 < end) {
            i += 2;
            continue;
        }
        if (line[i] == '=') {
            return i;
        }
        if (line[i] == ' ' || line[i] == ',') {
            return 0;
        }
        i++;
    }

    return 0;
}

static size_t
search_field_value_end(const char *line, size_t start, size_t end)
{
    size_t i = start;
    int quoted = 0;

    if (start >= end || line[start] == ' ' || line[start] == ',') {
        return 0;
    }

    while (i < end) {
        LP_DEBUG_PRINT("search field value: %c\n", line[i]);
        if (!quoted && (line[i] == '\n' || line[i] == '\r')) {
            return 0;
        }
        if (line[i] == '\\' && i + 1 < end &&
            (line[i + 1] == '"' || line[i + 1] == '\\')) {
            i += 2;
            continue;
        }
        if (line[i] == '"') {
            quoted = !quoted;
            i++;
            continue;
        }
        if (!quoted && (line[i] == ' ' || line[i] == ',')) {
            return i;
        }
        i++;
    }

    if (quoted) {
        return 0;
    }

    LP_DEBUG_PRINT("reached end of line and we're parsing a field value\n");
    return end;
}

static int
tag_key_exists(const struct LP_Item *head, const char *key)
{
    while (head != NULL) {
        if (strcmp(head->key, key) == 0) {
            return 1;
        }
        head = head->next_item;
    }

    return 0;
}

static int
strict_parse_float_token(const char *s, double *out)
{
    size_t i = 0;
    int saw_digit = 0;
    int saw_dot = 0;
    int saw_exp = 0;
    int saw_exp_digit = 0;
    double candidate = 0.0;
    char *endptr = NULL;

    if (s[0] == '\0' || s[0] == '+') {
        return 0;
    }
    if (s[0] == '-') {
        i++;
    }
    if (s[i] == '\0') {
        return 0;
    }

    for (; s[i] != '\0'; i++) {
        if (isdigit((unsigned char) s[i])) {
            saw_digit = 1;
            if (saw_exp) {
                saw_exp_digit = 1;
            }
            continue;
        }
        if (s[i] == '.') {
            if (saw_dot || saw_exp) {
                return 0;
            }
            saw_dot = 1;
            continue;
        }
        if (s[i] == 'e' || s[i] == 'E') {
            if (saw_exp || !saw_digit) {
                return 0;
            }
            saw_exp = 1;
            saw_exp_digit = 0;
            continue;
        }
        if (s[i] == '+' || s[i] == '-') {
            if (i == 0 || (s[i - 1] != 'e' && s[i - 1] != 'E')) {
                return 0;
            }
            continue;
        }
        return 0;
    }

    if (!saw_digit || (saw_exp && !saw_exp_digit)) {
        return 0;
    }

    errno = 0;
    candidate = strtod(s, &endptr);
    if (errno == ERANGE || endptr == s || *endptr != '\0' ||
        isfinite(candidate) == 0) {
        return 0;
    }

    *out = candidate;
    return 1;
}

static int
strict_parse_integer_token(const char *s, signed long long *out)
{
    size_t i = 0;
    size_t length = strlen(s);
    signed long long candidate = 0;
    char *endptr = NULL;

    if (length < 2 || s[length - 1] != 'i' || s[0] == '+') {
        return 0;
    }
    if (s[0] == '-') {
        i++;
    }
    if (i == length - 1) {
        return 0;
    }
    for (; i < length - 1; i++) {
        if (!isdigit((unsigned char) s[i])) {
            return 0;
        }
    }

    errno = 0;
    candidate = strtoll(s, &endptr, 10);
    if (errno == ERANGE || endptr != s + length - 1 || *endptr != 'i') {
        return 0;
    }

    *out = candidate;
    return 1;
}

static int
strict_parse_uinteger_token(const char *s, unsigned long long *out)
{
    size_t i = 0;
    size_t length = strlen(s);
    unsigned long long candidate = 0;
    char *endptr = NULL;

    if (length < 2 || s[length - 1] != 'u') {
        return 0;
    }
    for (; i < length - 1; i++) {
        if (!isdigit((unsigned char) s[i])) {
            return 0;
        }
    }

    errno = 0;
    candidate = strtoull(s, &endptr, 10);
    if (errno == ERANGE || endptr != s + length - 1 || *endptr != 'u') {
        return 0;
    }

    *out = candidate;
    return 1;
}

static int
strict_parse_boolean_token(const char *s, int *out)
{
    switch (s[0]) {
        case 't':
            if (strcmp(s, "t") == 0 || strcmp(s, "true") == 0) {
                *out = 1;
                return 1;
            }
            break;
        case 'T':
            if (strcmp(s, "T") == 0 || strcmp(s, "TRUE") == 0 ||
                strcmp(s, "True") == 0) {
                *out = 1;
                return 1;
            }
            break;
        case 'f':
            if (strcmp(s, "f") == 0 || strcmp(s, "false") == 0) {
                *out = 0;
                return 1;
            }
            break;
        case 'F':
            if (strcmp(s, "F") == 0 || strcmp(s, "FALSE") == 0 ||
                strcmp(s, "False") == 0) {
                *out = 0;
                return 1;
            }
            break;
        default:
            break;
    }

    return 0;
}

static void
decode_quoted_string_inplace(char *s)
{
    size_t length = strlen(s);
    size_t i = 1;
    size_t output_index = 0;
    size_t end = length - 1;

    while (i < end) {
        if (s[i] == '\\' && i + 1 < end && s[i + 1] == '\\') {
            s[output_index] = '\\';
            output_index++;
            i += 2;
            continue;
        }
        if (s[i] == '\\' && i + 1 < end && s[i + 1] == '"') {
            s[output_index] = '"';
            output_index++;
            i += 2;
            continue;
        }
        s[output_index] = s[i];
        output_index++;
        i++;
    }
    s[output_index] = '\0';
}

static int
strict_parse_timestamp(const char *s, size_t len, long long *out)
{
    char timestamp[32];
    size_t i = 0;
    long long candidate = 0;
    char *endptr = NULL;

    if (len == 0 || len >= sizeof(timestamp)) {
        return 0;
    }
    memcpy(timestamp, s, len);
    timestamp[len] = '\0';

    if (timestamp[0] == '+') {
        return 0;
    }
    if (timestamp[0] == '-') {
        i++;
    }
    if (i == len) {
        return 0;
    }
    for (; i < len; i++) {
        if (!isdigit((unsigned char) timestamp[i])) {
            return 0;
        }
    }

    errno = 0;
    candidate = strtoll(timestamp, &endptr, 10);
    if (errno == ERANGE || endptr != timestamp + len) {
        return 0;
    }
    if (candidate < LP_MIN_TIMESTAMP || candidate > LP_MAX_TIMESTAMP) {
        return 0;
    }

    *out = candidate;
    return 1;
}

/* Convert the field value string to correct type */
static int
parse_value(struct LP_Item* item)
{
    double candidate_d = 0;
    signed long long candidate_i = 0;
    unsigned long long candidate_u = 0ULL;
    int candidate_b = 0;
    size_t length = 0;

    if (item->type != LP_STRING){
        return 0;
    }

    if (strict_parse_float_token(item->value.s, &candidate_d) != 0) {
        LP_FREE(item->value.s);
        item->value.f = candidate_d;
        item->type = LP_FLOAT;
        LP_DEBUG_PRINT("Type is double: %f\n", candidate_d);
        return 1;
    }

    if (strict_parse_integer_token(item->value.s, &candidate_i) != 0) {
        LP_FREE(item->value.s);
        item->value.i = candidate_i;
        item->type = LP_INTEGER;
        LP_DEBUG_PRINT("Type is integer: %lld\n", candidate_i);
        return 1;
    }

    if (strict_parse_uinteger_token(item->value.s, &candidate_u) != 0) {
        LP_FREE(item->value.s);
        item->value.u = candidate_u;
        item->type = LP_UINTEGER;
        LP_DEBUG_PRINT("Type is uinteger: %llu\n", candidate_u);
        return 1;
    }

    length = strlen(item->value.s);
    // Try parse string
    if (*(item->value.s) == '"') {
        if (length < 2 || item->value.s[length - 1] != '"') {
            return 0;
        }
        LP_DEBUG_PRINT("Type is string: %s\n", item->value.s);
        /* Match InfluxDB v1.2: only `\\` and `\"` collapse inside quoted
           field strings, left to right. */
        decode_quoted_string_inplace(item->value.s);
        return 1;
    }

    if (strict_parse_boolean_token(item->value.s, &candidate_b) != 0) {
        LP_FREE(item->value.s);
        item->value.b = candidate_b;
        item->type = LP_BOOLEAN;
        LP_DEBUG_PRINT("Type is boolean: %d\n", item->value.b);
        return 1;
    }

    return 0; // Error
}

struct LP_Point*
LP_parse_line(const char *line, int *status)
{
    struct LP_Point *point = NULL;
    struct LP_Item *item = NULL;
    struct LP_Item *prev_item = NULL;
    long long parsed_time = 0;
    size_t index = 0;
    size_t key_start = 0;
    size_t start = 0;
    size_t end = strlen(line);
    size_t time_end = 0;

    /* Accept a single trailing LF, CRLF, or CR terminator so readline-style
       input behaves the same as direct strings. */
    if (end > 0 && line[end - 1] == '\n') {
        end--;
        if (end > 0 && line[end - 1] == '\r') {
            end--;
        }
    } else if (end > 0 && line[end - 1] == '\r') {
        end--;
    }

    start = skip_whitespace(line, 0, end);
    key_start = start;
    if (start >= end) {
        // Zero length line
        *status = LP_LINE_EMPTY;
        goto error;
    }
    if ((point = new_point()) == NULL) {
        // Failed to allocate memory for point
        *status = LP_MEMORY_ERROR;
        goto error;
    }
    if ((index = search_measurement_end(line, start, end)) == 0) {
        // Failed to find end of measurement
        *status = LP_MEASUREMENT_ERROR;
        goto error;
    }
    if (set_measurement(point, line, start, index) == 0) {
        *status = LP_MEMORY_ERROR;
        goto error;
    }
    /* The `index` is pointing to the space or comma character marking the
       end of the measurement string. The `start` is pointing to the first
       character of the tags OR the fields (if there are no tags).
    */
    start = index + 1;
    LP_DEBUG_PRINT("Measurement: %s\n", point->measurement);

    /* Extract all tags available */
    while (line[index] == ','){
        if ((item = new_item()) == NULL) {
            // Failed to create new tag item
            *status = LP_MEMORY_ERROR;
            goto error;
        }
        if (prev_item != NULL) {
            item->next_item = prev_item;
        }
        // TAG KEY
        if ((index = search_tag_key_equal(line, start, end)) == 0){
            // Failed to find end of tag key
            *status = LP_TAG_KEY_ERROR;
            goto error;
        }
        if (set_key(item, line, start, index, LP_TAG_KEY) == 0){
            // Failed to set tag key
            *status = LP_SET_KEY_ERROR;
            goto error;
        }
        LP_DEBUG_PRINT("New tag key: %s\n", item->key);
        if (tag_key_exists(prev_item, item->key) != 0) {
            *status = LP_TAG_KEY_ERROR;
            goto error;
        }
        start = index + 1;
        // TAG VALUE
        if ((index = search_tag_value_end(line, start, end)) == 0){
            // Failed to find end of tag value
            *status = LP_TAG_VALUE_ERROR;
            goto error;
        }
        if (set_value(item, line, start, index, LP_TAG_VALUE) == 0){
            // Failed to set key value
            *status = LP_SET_VALUE_ERROR;
            goto error;
        }
        LP_DEBUG_PRINT("New tag value: %s\n", item->value.s);
        start = index + 1;

        prev_item = item;
    }
    // Hook the chain of tags to the point
    if (item != NULL) {
        point->tags = item;
    }

    if (index - key_start > LP_MAX_KEY_LENGTH) {
        *status = LP_KEY_TOO_LONG_ERROR;
        goto error;
    }

    // The `index` should now point on the space-character dividing
    // measurements/tags from the fields.
    start = skip_whitespace(line, start, end);
    item = NULL;
    prev_item = NULL;
    do {
        if ((item = new_item()) == NULL) {
            // Failed to create new field item
            *status = LP_MEMORY_ERROR;
            goto error;
        }
        if (prev_item != NULL) {
            item->next_item = prev_item;
        }
        // FIELD KEY
        if ((index = search_field_key_equal(line, start, end)) == 0){
            // Failed to find end of field key
            *status = LP_FIELD_KEY_ERROR;
            goto error;
        }
        if (set_key(item, line, start, index, LP_FIELD_KEY) == 0){
            // Failed to set field key
            *status = LP_SET_KEY_ERROR;
            goto error;
        }
        LP_DEBUG_PRINT("New field key: %s\n", item->key);
        start = index + 1;
        // FIELD VALUE
        if ((index = search_field_value_end(line, start, end)) == 0) {
            // Failed to find end of field value
            *status = LP_FIELD_VALUE_ERROR;
            goto error;
        }
        if (set_value(item, line, start, index, LP_FIELD_VALUE) == 0){
            // Failed to set field value
            *status = LP_SET_VALUE_ERROR;
            goto error;
        }
        LP_DEBUG_PRINT("New field value: %s\n", item->value.s);
        start = index + 1;

        /* Convert the string value to correct line protocol type */
        if (parse_value(item) == 0) {
            *status = LP_FIELD_VALUE_TYPE_ERROR;
            goto error;
        }

        prev_item = item;
    } while (line[index] == ',');
    // Hook the chain of fields to the point
    if (item != NULL) {
        point->fields = item;
    }

    // Parse the nanosecond timestamp
    start = skip_whitespace(line, start, end);
    if(start >= end) {
        point->has_time = false;
        point->time = 0;
    } else {
        time_end = trim_trailing_spaces(line, start, end);
        if (strict_parse_timestamp(line + start, time_end - start, &parsed_time) == 0) {
            *status = LP_TIME_ERROR;
            goto error;
        }
        point->has_time = true;
        point->time = parsed_time;
        LP_DEBUG_PRINT("Time: %lld\n", (long long) point->time);
    }
    goto done;
error:
    if (item != NULL && point != NULL &&
        item != point->tags && item != point->fields) {
        /* Free the in-progress tag or field chain if it was never
           attached to the point before parsing failed. */
        free_item(item);
    }
    LP_free_point(point);
    point = NULL;
done:
    LP_DEBUG_PRINT("RETURN STATUS: %d\n", *status);
    return point;
}

#ifndef NDEBUG

static int
LP_main(void)
{
    /* Example usage */
    const char *line = "measurement,tag=value field=\"Hello, world!\" 1570283407262541159";
    struct LP_Point *point;
    int status = 0;
    point = LP_parse_line(line, &status);
    if (point == NULL) {
        LP_DEBUG_PRINT("ERROR STATUS: %d\n", status);
    }
    LP_free_point(point);
    return status;
}

#endif
