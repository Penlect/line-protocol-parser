#ifndef LINEPROTOCOLPARSER_H
#define LINEPROTOCOLPARSER_H

/* Error return codes of `LP_parse_line` */
#define LP_MEMORY_ERROR 1
#define LP_LINE_EMPTY 2
#define LP_MEASUREMENT_ERROR 3
#define LP_SET_KEY_ERROR 4
#define LP_SET_VALUE_ERROR 5
#define LP_TAG_KEY_ERROR 6
#define LP_TAG_VALUE_ERROR 7
#define LP_FIELD_KEY_ERROR 8
#define LP_FIELD_VALUE_ERROR 9
#define LP_FIELD_VALUE_TYPE_ERROR 10
#define LP_TIME_ERROR 11

/* Indicates which type a field has */
enum LP_ValueType {
    LP_FLOAT, LP_INTEGER, LP_UINTEGER, LP_BOOLEAN, LP_STRING
};

/* Represents the value of a tag or field.
 * Can be float, integer, boolean or string.
 * The boolean is represented as an integer.
 * A tag-value will always be a string.
*/
union LP_Value {
    double f; // Float
    signed long long i; // Integer
    int b; // Boolean
    char *s;  // String
};

/* A linked list of key-value paris. Used both by tags and fields.
 * The enum `type` indicates the correct type of union `value`.
*/
struct LP_Item {
    char *key;
    enum LP_ValueType type;
    union LP_Value value;
    struct LP_Item *next_item;
};

/* Represents a linked list of points. A point is the final data
 * structure obtained from reading a line-protocol line.
 */
struct LP_Point {
    char *measurement;
    struct LP_Item *fields;
    struct LP_Item *tags;
    unsigned long long time;
    struct LP_Point *next_point;
};

struct LP_Point*
LP_parse_line(const char *line, int *status);
void
LP_free_point(struct LP_Point *point);

#endif