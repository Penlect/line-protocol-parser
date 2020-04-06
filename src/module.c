/* Python.h must be included first */
#ifndef PY_SSIZE_T_CLEAN
#define PY_SSIZE_T_CLEAN
#endif
#include <Python.h>
#include "line_protocol_parser.h"

PyDoc_STRVAR(module_doc,
"Parse InfluxDB line protocol strings into Python dictionaries.\n\
\n\
Functions:\n\
parse_line(line) -> dict.\n\
\n\
Exceptions:\n\
LineFormatError (raised when a line protocol string is wrong).\n\
");

// Custom exception
PyDoc_STRVAR(LineFormatError__doc__,
"An error ocurred when parsing the components of the line.\n"
);
static PyObject *LineFormatError = NULL;


PyDoc_STRVAR(parse_line__doc__,
"Parse a line protocol string into a dictionary.\n\
\n\
Returns a dictionary with keys 'measurement', 'fields', 'tags' and\n\
'time'. Rases `LineFormatError` when input can't be parsed.\n\
");

static PyObject*
parse_line(PyObject* self, PyObject* args)
{
    PyObject *measurement = NULL;
    PyObject *tag_value = NULL;
    PyObject *field_value = NULL;
    PyObject *tags = NULL, *fields = NULL;
    PyObject *time = NULL;
    PyObject *input = NULL, *output = NULL;
    struct LP_Item *tmp = NULL;
    struct LP_Point *point = NULL;
    char *line = NULL;
    int status = 0;
    goto try;
try:
    assert(!PyErr_Occurred());
    assert(args);
    if (PyBytes_Check(args)) {
        input = args;
        Py_INCREF(input);
    } else if ((input = PyUnicode_AsEncodedString(args, NULL, NULL)) == NULL) {
        return NULL;
    }
    if ((line = PyBytes_AsString(input)) == NULL) {
        return NULL;
    }
    point = LP_parse_line(line, &status);
    // Check status and raise exception based on status
    if (point == NULL) {
        switch(status) {
            case LP_MEMORY_ERROR:
                PyErr_SetString(PyExc_MemoryError, "Failed to allocate memory.");
                goto except;
            case LP_LINE_EMPTY:
                PyErr_SetString(LineFormatError, "Line is empty string.");
                goto except;
            case LP_MEASUREMENT_ERROR:
                PyErr_SetString(LineFormatError, "Failed to parse measurement.");
                goto except;
            case LP_SET_KEY_ERROR:
                PyErr_SetString(PyExc_MemoryError, "Failed to allocate memory for key string.");
                goto except;
            case LP_SET_VALUE_ERROR:
                PyErr_SetString(PyExc_MemoryError, "Failed to allocate memory for value string.");
                goto except;
            case LP_TAG_KEY_ERROR:
                PyErr_SetString(LineFormatError, "Failed to parse key of tag.");
                goto except;
            case LP_TAG_VALUE_ERROR:
                PyErr_SetString(LineFormatError, "Failed to parse value of tag.");
                goto except;
            case LP_FIELD_KEY_ERROR:
                PyErr_SetString(LineFormatError, "Failed to parse key of field.");
                goto except;
            case LP_FIELD_VALUE_ERROR:
                PyErr_SetString(LineFormatError, "Failed to parse value of field.");
                goto except;
            case LP_FIELD_VALUE_TYPE_ERROR:
                PyErr_SetString(LineFormatError, "Failed to parse type of field value.");
                goto except;
            case LP_TIME_ERROR:
                PyErr_SetString(LineFormatError, "Failed to parse nanoseconds integer timestamp.");
                goto except;
        }
    }
    if ((measurement = PyUnicode_FromString(point->measurement)) == NULL) {
        goto except;
    }
    if ((tags = PyDict_New()) == NULL) {
        goto except;
    }
    tmp = point->tags;
    while (tmp != NULL) {
        if ((tag_value = PyUnicode_FromString(tmp->value.s)) == NULL) {
            goto except;
        }
        if ((PyDict_SetItemString(tags, tmp->key, tag_value)) == -1) {
            goto except;
        }
        Py_DECREF(tag_value);
        tmp = tmp->next_item;
    }
    if ((fields = PyDict_New()) == NULL) {
        goto except;
    }
    tmp = point->fields;
    if (tmp != NULL) {
        while (tmp != NULL) {
            switch (tmp->type) {
                case LP_FLOAT:
                    field_value = PyFloat_FromDouble(tmp->value.f);
                    break;
                case LP_INTEGER:
                    field_value = PyLong_FromLongLong(tmp->value.i);
                    break;
                case LP_BOOLEAN:
                    field_value = PyBool_FromLong(tmp->value.b);
                    break;
                case LP_STRING:
                    field_value = PyUnicode_FromString(tmp->value.s);
                    break;
                default:
                    PyErr_SetString(LineFormatError, "Unexpected value type.");
                    goto except;
            }
            if ((PyDict_SetItemString(fields, tmp->key, field_value)) == -1) {
                goto except;
            }
            Py_DECREF(field_value);
            tmp = tmp->next_item;
        }
    }
    if ((time = PyLong_FromUnsignedLongLong(point->time)) == NULL){
        goto except;
    }
    output = Py_BuildValue("{sOsOsOsO}", "measurement", measurement, "tags", tags,
                           "fields", fields, "time", time);
    if (output == NULL){
        goto except;
    }

    assert(!PyErr_Occurred());
    goto finally;
except:
    Py_XDECREF(tag_value);
    Py_XDECREF(field_value);
    output = NULL;
finally:
    Py_XDECREF(measurement);
    Py_XDECREF(tags);
    Py_XDECREF(fields);
    Py_XDECREF(time);
    Py_XDECREF(input);
    if (point != NULL){
        LP_free_point(point);
    }
    return output;
}

static PyMethodDef _line_protocol_functions[] = {
    {"parse_line", (PyCFunction)parse_line, METH_O, parse_line__doc__},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef _line_protocol_parser_module = {
	PyModuleDef_HEAD_INIT,
	"_line_protocol_parser",
	module_doc,
	-1,
	_line_protocol_functions
};

PyMODINIT_FUNC
PyInit__line_protocol_parser(void){
    PyObject* module = PyModule_Create(&_line_protocol_parser_module);
    if (module == NULL) {
        return NULL;
    }
    LineFormatError = PyErr_NewExceptionWithDoc(
        "_line_protocol_parser.LineFormatError", LineFormatError__doc__, NULL, NULL);
    Py_XINCREF(LineFormatError);
    if (PyModule_AddObject(module, "LineFormatError", LineFormatError) < 0){
        Py_XDECREF(LineFormatError);
        Py_DECREF(module);
        return NULL;
    }
    return module;
}
