#ifndef JSON_TABLE_H
#define JSON_TABLE_H

#include <stddef.h> // For size_t

// Forward declaration for cJSON
struct cJSON;

// Error codes for json_table functions
typedef enum {
    JT_SUCCESS = 0,
    JT_ERROR_PARSE_JSON,
    JT_ERROR_INVALID_PATH,
    JT_ERROR_INVALID_TYPE,
    JT_ERROR_MEMORY_ALLOCATION,
    JT_ERROR_COLUMN_PROCESSING,
    JT_ERROR_UNKNOWN
} JsonTableError;

// Supported C data types for table columns
typedef enum {
    JT_INT,
    JT_DOUBLE,
    JT_STRING,
    // JT_BOOLEAN, // Add more types as needed
    // JT_NULL,
} JsonTableDataType;

// Definition for a column in the output table
typedef struct {
    const char* columnName;         // Name of the column (for reference/debugging)
    const char* jsonPath;           // JSONPath expression to extract the value for this column, relative to a row item
    JsonTableDataType type;         // Target C data type for this column
    size_t offset;                  // Offset within a JsonTableRow's data buffer where this column's value is stored
    size_t size;                    // Size of the data type (e.g., sizeof(int), sizeof(char*))
} JsonTableColumnDef;

// Represents a single row in the output table
// The 'data' buffer is a contiguous block of memory holding all column values for this row.
// Values are accessed by casting (base_address + offset) to the appropriate type pointer.
typedef struct {
    void* data; // Buffer holding all column data for this row
} JsonTableRow;

// Represents the entire output table
typedef struct {
    JsonTableRow* rows;             // Array of rows
    size_t rowCount;                // Number of rows in the table
    size_t rowCapacity;             // Current allocated capacity for rows array

    JsonTableColumnDef* columnDefs; // Copy of the column definitions used to create this table
    size_t columnCount;             // Number of columns
    size_t totalRowDataSize;        // Total size in bytes for the data buffer of a single row (sum of all column sizes)
} JsonTable;

// Function to create and process the JSON data into a table
JsonTableError json_table_process(
    const char* json_string,          // Input JSON data as a C string
    const char* row_path_expr,        // JSONPath expression to identify the array of objects/elements that represent rows
    JsonTableColumnDef* column_defs,  // Array of column definitions
    size_t num_columns,               // Number of columns in column_defs
    JsonTable** result_table,         // Pointer to store the resulting table
    char** error_message              // Pointer to store an error message string if an error occurs
);

// Function to free all memory allocated for a JsonTable
void json_table_free(JsonTable* table);

// Helper function to get a string representation of an error code
const char* json_table_error_string(JsonTableError error_code);

#endif // JSON_TABLE_H
