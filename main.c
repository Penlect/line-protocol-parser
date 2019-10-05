
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct Item {
    char *key;
    char *value;
    struct Item *next_item;
};

struct Item*
new_item()
{
    struct Item *output;
    output = malloc(sizeof(*output));
    if (output == NULL) {
        return NULL;
    }
    output->key = NULL;
    output->value = NULL;
    output->next_item = NULL;
    return output;
}

void
free_item(struct Item *item)
{
    struct Item *tmp;
    struct Item *_item = item;
    while (_item != NULL) {
        tmp = _item->next_item;
        free(_item->key);
        free(_item->value);
        free(_item);
        _item = tmp;
    }
}

static int
set_key(struct Item *item, const char *line, size_t start, size_t end)
{
    size_t i, j;
    item->key = malloc(end - start + 1);
    if (item->key == NULL) {
        return 0;
    }
    for (i = 0, j = start; j < end; i++, j++) {
        item->key[i] = line[j];
    }
    item->key[i] = '\0';
    return 1;
}

static int
set_value(struct Item *item, const char *line, size_t start, size_t end)
{
    size_t i, j;
    item->value = malloc(end - start + 1);
    if (item->value == NULL) {
        return 0;
    }
    for (i = 0, j = start; j < end; i++, j++) {
        item->value[i] = line[j];
    }
    item->value[i] = '\0';
    return 1;
}

struct Point {
    char *measurement;
    struct Item *fields;
    struct Item *tags;
    unsigned long long time;
    struct Point *next_point;
};

struct Point*
new_point() {
    struct Point *output;
    output = malloc(sizeof(*output));
    if (output == NULL) {
        return NULL;
    }
    output->fields = NULL;
    output->tags = NULL;
    output->time = 0;
    output->next_point = NULL;
    return output;
}

void
free_point(struct Point *point) {
    struct Point *tmp;
    struct Point *_point = point;
    while (_point != NULL) {
        tmp = _point->next_point;
        free(_point->measurement);
        free_item(point->fields);
        free_item(point->tags);
        free(point);
        _point = tmp;
    }
}

size_t
search_comma_space(const char *line, size_t start, size_t end, int quotes)
{
    size_t i = start;
    int inside_quotes = 0;
    for (; i < end; i++) {
        printf("search comma/space: %c\n", line[i]);
        if (line[i] == '"') {
            if (i > 0 && line[i - 1] != '\\') {
                inside_quotes = !inside_quotes;
            }
            else if (i > 1 && line[i - 2] == '\\') {
                inside_quotes = !inside_quotes;
            }
        }
        if (inside_quotes == 1) {
            continue;
        }
        if (line[i] == ' ' || line[i] == ',') {
            if (i > 0 && line[i - 1] != '\\') {
                return i;
            }
            else if (i > 1 && line[i - 2] == '\\') {
                return i;
            }
        }
    }
    return 0;
}

size_t
search_equal(const char *line, size_t start, size_t end)
{
    size_t i = start;
    for (; i < end; i++) {
        printf("search equal: %c\n", line[i]);
        if (line[i] == '=') {
            if (i > 0 && line[i - 1] != '\\') {
                return i;
            }
            else if (i > 1 && line[i - 2] == '\\') {
                return i;
            }
        }
    }
    return 0;
}

struct Point *parse_line(const char *line)
{
    struct Point *point = NULL;
    struct Item *item = NULL;
    struct Item *prev_item = NULL;
    char *endptr_time = NULL;
    size_t index = 0;
    size_t start = 0;
    size_t end = strlen(line);
    if (end == 0) {
        // zero length line
        return NULL;
    }
    if ((point = malloc(sizeof(*point))) == NULL) {
        // Failed to allocate memory for point
        return NULL;
    }
    if ((index = search_comma_space(line, start, end, 0)) == 0) {
        // Failed to find end of measurement
        goto error;
    }
    if ((point->measurement = malloc(index + 1)) == NULL) {
        // Failed to allocate memory for measurement string
        goto error;
    }
    strncpy(point->measurement, line, index);
    point->measurement[index] = 0;
    /* The `index` is pointing to the space or comma character marking the
       end of the measurement string. The `start` is pointing to the first
       character of the tags OR the fields (if there are no tags).
    */
    start = index + 1;
    printf("Measurement: %s\n", point->measurement);

    while (line[index] == ','){
        if ((item = new_item()) == NULL) {
            // Failed to create new tag item
            goto error;
        }
        if (prev_item != NULL) {
            item->next_item = prev_item;
        }
        //KEY
        if ((index = search_equal(line, start, end)) == 0){
            // Failed to find end of tag key
            goto error;
        }
        if (set_key(item, line, start, index) == 0){
            // Failed to set tag key
            goto error;
        }
        printf("New tag key: %s\n", item->key);
        start = index + 1;
        //VALUE
        if ((index = search_comma_space(line, start, end, 0)) == 0){
            // Failed to find end of tag value
            goto error;
        }
        if (set_value(item, line, start, index) == 0){
            // Failed to set key value
            goto error;
        }
        printf("New tag value: %s\n", item->value);
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
            goto error;
        }
        if (prev_item != NULL) {
            item->next_item = prev_item;
        }
        //KEY
        if ((index = search_equal(line, start, end)) == 0){
            // Failed to find end of field key
            goto error;
        }
        if (set_key(item, line, start, index) == 0){
            // Failed to set field key
            goto error;
        }
        printf("New field key: %s\n", item->key);
        start = index + 1;
        //VALUE
        if ((index = search_comma_space(line, start, end, 1)) == 0){
            // Failed to find end of field value
            goto error;
        }
        if (set_value(item, line, start, index) == 0){
            // Failed to set field value
            goto error;
        }
        printf("New field value: %s\n", item->value);
        start = index + 1;

        prev_item = item;
    } while (line[index] == ',');
    // Hook the chain of fields to the point
    if (item != NULL) {
        point->fields = item;
    }
    // Parse the nanosecond timestamp
    point->time = strtoull(line + start, &endptr_time, 10);
    printf("Time: %llu\n", point->time);
    if (*endptr_time != '\0') {
        // Failed to parse whole nanosecond timestamp
        goto error;
    }
    goto done;
error:
    free_point(point);
    point = NULL;
done:
    return point;
}

int main() {
    const char *line = "measu\\,re\\ m=ent,tag1=A,tag2=B f0=True,f1=\"1. 2,\\\"3\" 1570283407262541159";
    struct Point *point;
    point = parse_line(line);
    if (point == NULL) {
        printf("ERROR\n");
    }
    free_point(point);
    return 0;
}