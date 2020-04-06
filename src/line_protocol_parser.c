#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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

/* Used to indicate different components of a line */
enum _LP_Part {
    LP_MEASUREMENT,
    LP_TAG_KEY,
    LP_TAG_VALUE,
    LP_FIELD_KEY,
    LP_FIELD_VALUE
};

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
        /* Drop backslash preceding specified characters */
        if (line[j] == '\\' && j + 1  < end) {
            switch (line[j + 1]) {
                case ',': continue;
                case ' ': continue;
                case '=': continue;
                case '"': continue;
            }
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
        /* Drop backslash preceding specified characters */
        if (line[j] == '\\' && j + 1  < end) {
            switch (line[j + 1]) {
                case ',': continue;
                case ' ': continue;
                case '=': continue;
                case '"':
                    if (part == LP_FIELD_KEY){
                        continue;
                    }
                    break;
            }
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
        /* Drop backslash preceding specified characters */
        if (line[j] == '\\' && j + 1  < end && part != LP_FIELD_VALUE) {
            switch (line[j + 1]) {
                case ',': continue;
                case ' ': continue;
                case '=': continue;
                case '"':
                    if (part == LP_FIELD_KEY){
                        continue;
                    }
                    break;
            }
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

/* Find the next "," or " "-character that is not escaped */
static size_t
search_comma_space(const char *line, size_t start, size_t end, enum _LP_Part part)
{
    size_t i = start;
    int respect_quotes = (part == LP_FIELD_VALUE);
    int inside_quotes = 0;
    for (; i < end; i++) {
        LP_DEBUG_PRINT("search comma/space: %c\n", line[i]);
        if (respect_quotes && line[i] == '"') {
            if (i > 0 && line[i - 1] != '\\') {
                inside_quotes = !inside_quotes;
            } else if (i > 1 && line[i - 2] == '\\') {
                inside_quotes = !inside_quotes;
            }
        }
        if (respect_quotes && inside_quotes == 1) {
            /* Eat everything until we find next quote */
            continue;
        }
        if (line[i] == ' ' || line[i] == ',') {
            if (i > 0 && line[i - 1] != '\\') {
                return i;
            } else if (i > 1 && line[i - 2] == '\\') {
                return i;
            }
        }
    }
    return 0; // Error
}

/* Find the next "="-character that is not escaped */
static size_t
search_equal(const char *line, size_t start, size_t end)
{
    size_t i = start;
    for (; i < end; i++) {
        LP_DEBUG_PRINT("search equal: %c\n", line[i]);
        if (line[i] == '=') {
            if (i > 0 && line[i - 1] != '\\') {
                /* The "=" is not backslash-escaped */
                return i;
            } else if (i > 1 && line[i - 2] == '\\') {
                /* The "=" IS backslash-escaped, but that backslash
                   is itself escaped. */
                return i;
            }
        }
    }
    return 0; // Error
}

/* Convert the field value string to correct type */
static int
parse_value(struct LP_Item* item)
{
    size_t i = 0;
    size_t length = 0;
    double candidate_d = 0;
    signed long long candidate_i = 0;
    char boolstr[6];
    char *endptr = NULL;

    if (item->type != LP_STRING){
        return 0;
    }
    // Try parse float
    endptr = NULL;
    candidate_d = strtod(item->value.s, &endptr);
    if (*endptr == '\0') {
        LP_FREE(item->value.s);
        item->value.f = candidate_d;
        item->type = LP_FLOAT;
        LP_DEBUG_PRINT("Type is double: %f\n", candidate_d);
        return 1;
    }

    // Try parse integer
    endptr = NULL;
    candidate_i = strtoll(item->value.s, &endptr, 10);
    if (*endptr == 'i' && *(endptr + 1) == '\0') {
        LP_FREE(item->value.s);
        item->value.i = candidate_i;
        item->type = LP_INTEGER;
        LP_DEBUG_PRINT("Type is integer: %lld\n", candidate_i);
        return 1;
    }

    length = strlen(item->value.s);
    // Try parse string
    if (*(item->value.s) == '"') {
        LP_DEBUG_PRINT("Type is string: %s\n", item->value.s);

        // Strip the surronding quotes
        for (i = 1; i < length - 1; i++) {
            item->value.s[i - 1] = item->value.s[i];
        }
        item->value.s[length - 2] = '\0';
        return 1;
    }

    // Try parse boolan
    if (length == 1 && tolower(*(item->value.s)) == 't' ) {
        LP_FREE(item->value.s);
        item->value.b = 1;
        item->type = LP_BOOLEAN;
        LP_DEBUG_PRINT("Type is boolean: %d\n", item->value.b);
        return 1;
    } else if (length == 1 && tolower(*(item->value.s)) == 'f' ) {
        LP_FREE(item->value.s);
        item->value.b = 0;
        item->type = LP_BOOLEAN;
        LP_DEBUG_PRINT("Type is boolean: %d\n", item->value.b);
        return 1;
    } else if (length == 4 || length == 5) {
        /* Convert to lower case to reduce nr of comparisons*/
        for (i = 0; i <= length; i++) {
            boolstr[i] = tolower(item->value.s[i]);
        }
        if ((strcmp(boolstr, "true") == 0)) {
            LP_FREE(item->value.s);
            item->value.b = 1;
            item->type = LP_BOOLEAN;
            LP_DEBUG_PRINT("Type is boolean: %d\n", item->value.b);
            return 1;
        } else if ((strcmp(boolstr, "false") == 0)) {
            LP_FREE(item->value.s);
            item->value.b = 0;
            item->type = LP_BOOLEAN;
            LP_DEBUG_PRINT("Type is boolean: %d\n", item->value.b);
            return 1;
        }
    }

    return 0; // Error
}

struct LP_Point*
LP_parse_line(const char *line, int *status)
{
    struct LP_Point *point = NULL;
    struct LP_Item *item = NULL;
    struct LP_Item *prev_item = NULL;
    char *endptr_time = NULL;
    size_t index = 0;
    size_t start = 0;
    size_t end = strlen(line);
    if (end == 0) {
        // Zero length line
        *status = LP_LINE_EMPTY;
        goto error;
    }
    if ((point = new_point()) == NULL) {
        // Failed to allocate memory for point
        *status = LP_MEMORY_ERROR;
        goto error;
    }
    if ((index = search_comma_space(line, start, end, LP_MEASUREMENT)) == 0) {
        // Failed to find end of measurement
        *status = LP_MEASUREMENT_ERROR;
        goto error;
    }
    if (set_measurement(point, line, 0, index) == 0) {
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
        if ((index = search_equal(line, start, end)) == 0){
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
        start = index + 1;
        // TAG VALUE
        if ((index = search_comma_space(line, start, end, LP_TAG_VALUE)) == 0){
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

    // The `index` should now point on the space-character dividing
    // measurements/tags from the fields.
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
        if ((index = search_equal(line, start, end)) == 0){
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
        if ((index = search_comma_space(line, start, end, LP_FIELD_VALUE)) == 0){
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
    point->time = strtoull(line + start, &endptr_time, 10);
    LP_DEBUG_PRINT("Time: %llu\n", point->time);
    if (*endptr_time != '\0' && *endptr_time != '\n' && *endptr_time != '\r') {
        // Failed to parse whole nanosecond timestamp
        *status = LP_TIME_ERROR;
        goto error;
    }
    goto done;
error:
    LP_free_point(point);
    point = NULL;
done:
    LP_DEBUG_PRINT("RETURN STATUS: %d\n", *status);
    return point;
}

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
