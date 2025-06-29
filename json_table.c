#include "json_table.h"
#include "cJSON.h"
#include "json_path.h" // For JsonPathResult and json_path_evaluate

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Forward declarations for internal helper functions ---
static size_t calculate_total_row_data_size(JsonTableColumnDef* column_defs, size_t num_columns);
static void populate_column_offsets_and_sizes(JsonTableColumnDef* column_defs, size_t num_columns);
static JsonTableError process_single_row_item(
    cJSON* row_json_item,
    JsonTable* table_to_populate,
    char** error_message
);
static char* jt_strdup(const char* s); // Simple strdup, as it's not C90 standard

// --- Type Conversion Utilities (Placeholders for now) ---
static JsonTableError cjson_to_int(cJSON* item, int* out_val, char** error_message) {
    if (item == NULL || cJSON_IsNull(item)) { // Handle explicit JSON null
        *out_val = 0; // Default to 0 for null numbers
        return JT_SUCCESS;
    }
    if (!cJSON_IsNumber(item)) {
        if (error_message) *error_message = jt_strdup("Type error: Expected a number for INT conversion.");
        return JT_ERROR_INVALID_TYPE;
    }
    *out_val = item->valueint;
    return JT_SUCCESS;
}

static JsonTableError cjson_to_double(cJSON* item, double* out_val, char** error_message) {
    if (item == NULL || cJSON_IsNull(item)) { // Handle explicit JSON null
        *out_val = 0.0; // Default to 0.0 for null numbers
        return JT_SUCCESS;
    }
    if (!cJSON_IsNumber(item)) {
        if (error_message) *error_message = jt_strdup("Type error: Expected a number for DOUBLE conversion.");
        return JT_ERROR_INVALID_TYPE;
    }
    *out_val = item->valuedouble;
    return JT_SUCCESS;
}

static JsonTableError cjson_to_string(cJSON* item, char** out_val, char** error_message) {
    if (!cJSON_IsString(item)) {
        // Allow NULL for strings if item is NULL or not a string
        if (item == NULL || cJSON_IsNull(item)) {
            *out_val = NULL; // Represent JSON null as C NULL for strings
            return JT_SUCCESS;
        }
        *error_message = jt_strdup("Type error: Expected a string for STRING conversion.");
        return JT_ERROR_INVALID_TYPE;
    }
    *out_val = jt_strdup(item->valuestring);
    if (!*out_val && item->valuestring) { // Check if strdup failed but original string existed
        *error_message = jt_strdup("Memory allocation failed for string copy.");
        return JT_ERROR_MEMORY_ALLOCATION;
    }
    return JT_SUCCESS;
}

// --- Helper Functions ---
static char* jt_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* new_s = (char*)malloc(len);
    if (!new_s) return NULL;
    memcpy(new_s, s, len);
    return new_s;
}


const char* json_table_error_string(JsonTableError error_code) {
    switch (error_code) {
        case JT_SUCCESS: return "Success";
        case JT_ERROR_PARSE_JSON: return "JSON Parsing Error";
        case JT_ERROR_INVALID_PATH: return "Invalid JSONPath Expression";
        case JT_ERROR_INVALID_TYPE: return "Data Type Mismatch/Conversion Error";
        case JT_ERROR_MEMORY_ALLOCATION: return "Memory Allocation Error";
        case JT_ERROR_COLUMN_PROCESSING: return "Error Processing Column Data";
        case JT_ERROR_UNKNOWN: return "Unknown Error";
        default: return "Undefined Error Code";
    }
}

static size_t get_data_type_size(JsonTableDataType type) {
    switch (type) {
        case JT_INT: return sizeof(int);
        case JT_DOUBLE: return sizeof(double);
        case JT_STRING: return sizeof(char*);
        default: return 0; // Should not happen if types are validated
    }
}

static void populate_column_offsets_and_sizes(JsonTableColumnDef* column_defs, size_t num_columns) {
    size_t current_offset = 0;
    for (size_t i = 0; i < num_columns; ++i) {
        column_defs[i].size = get_data_type_size(column_defs[i].type);
        column_defs[i].offset = current_offset;
        current_offset += column_defs[i].size;
         // Add padding here if alignment becomes an issue for certain architectures/types
    }
}

static size_t calculate_total_row_data_size(JsonTableColumnDef* column_defs, size_t num_columns) {
    size_t total_size = 0;
    for (size_t i = 0; i < num_columns; ++i) {
        total_size += get_data_type_size(column_defs[i].type);
        // Add padding here if necessary
    }
    return total_size;
}


// --- Core `json_table_process` Logic ---
JsonTableError json_table_process(
    const char* json_string,
    const char* row_path_expr,
    JsonTableColumnDef* column_defs_input, // User provided column definitions
    size_t num_columns,
    JsonTable** result_table_ptr,
    char** error_message) {

    if (!json_string || !row_path_expr || !column_defs_input || num_columns == 0 || !result_table_ptr) {
        if (error_message) *error_message = jt_strdup("Invalid arguments to json_table_process.");
        return JT_ERROR_UNKNOWN; // Or a more specific error for invalid args
    }

    if (error_message) *error_message = NULL; // Initialize error message

    JsonTableError err_code = JT_SUCCESS;
    cJSON* root_json = NULL;
    JsonTable* table = NULL;
    JsonPathResult row_items_result = {NULL, 0, 0}; // Use 0 for cJSON_bool false

    // 1. Parse the input JSON string
    root_json = cJSON_Parse(json_string);
    if (!root_json) {
        if (error_message) {
            const char* parse_end = cJSON_GetErrorPtr();
            if (parse_end) {
                // Basic error message, could be more sophisticated
                char temp_err_msg[256];
                snprintf(temp_err_msg, sizeof(temp_err_msg), "JSON parse error near: %.*s", 30, parse_end);
                *error_message = jt_strdup(temp_err_msg);
            } else {
                *error_message = jt_strdup("JSON parse error (unknown location).");
            }
        }
        err_code = JT_ERROR_PARSE_JSON;
        goto cleanup;
    }

    // 2. Allocate and initialize the result table structure
    table = (JsonTable*)calloc(1, sizeof(JsonTable));
    if (!table) {
        if (error_message) *error_message = jt_strdup("Memory allocation failed for JsonTable.");
        err_code = JT_ERROR_MEMORY_ALLOCATION;
        goto cleanup;
    }
    *result_table_ptr = table;

    // Copy column definitions and calculate offsets/sizes
    table->columnDefs = (JsonTableColumnDef*)malloc(num_columns * sizeof(JsonTableColumnDef));
    if (!table->columnDefs) {
        if (error_message) *error_message = jt_strdup("Memory allocation failed for column definitions copy.");
        err_code = JT_ERROR_MEMORY_ALLOCATION;
        goto cleanup;
    }
    memcpy(table->columnDefs, column_defs_input, num_columns * sizeof(JsonTableColumnDef));
    table->columnCount = num_columns;
    populate_column_offsets_and_sizes(table->columnDefs, table->columnCount);
    table->totalRowDataSize = calculate_total_row_data_size(table->columnDefs, table->columnCount);


    // 3. Identify Row Items using the row_path_expr
    row_items_result = json_path_evaluate(root_json, row_path_expr);
    if (row_items_result.count == 0 && strcmp(row_path_expr, "$") != 0) { // If path is "$" and it's not an array, it might be a single object row
        // If path is just "$", and root is an object, treat it as a single row.
        if (strcmp(row_path_expr, "$") == 0 && cJSON_IsObject(root_json)) {
             // Special case: root object is the single row item.
             // json_path_evaluate for "$" currently returns the root itself.
             // We need to re-package it as if it was found by a path.
             json_path_result_free(&row_items_result); // Free the previous result
             row_items_result.items = (cJSON**)malloc(sizeof(cJSON*));
             if (!row_items_result.items) {
                 if (error_message) *error_message = jt_strdup("Memory allocation failed for single row item.");
                 err_code = JT_ERROR_MEMORY_ALLOCATION;
                 goto cleanup;
             }
             row_items_result.items[0] = root_json;
             row_items_result.count = 1;
             row_items_result.is_array_wildcard_result = 0; // Use 0 for cJSON_bool false
        } else {
            // No items found for rows, could be an empty result or invalid path
            // This is not necessarily an error, could just be an empty table.
            // If error_message is not NULL, we might want to indicate path evaluation failure.
            // For now, assume it's an empty table result.
            goto cleanup; // Successfully created an empty table
        }
    }


    // 4. Process Each Row Item
    table->rowCapacity = row_items_result.count > 0 ? row_items_result.count : 10; // Initial capacity
    table->rows = (JsonTableRow*)malloc(table->rowCapacity * sizeof(JsonTableRow));
    if (!table->rows) {
        if (error_message) *error_message = jt_strdup("Memory allocation failed for table rows.");
        err_code = JT_ERROR_MEMORY_ALLOCATION;
        goto cleanup;
    }

    for (size_t i = 0; i < row_items_result.count; ++i) {
        cJSON* current_row_json_item = row_items_result.items[i];
        if (!current_row_json_item) continue; // Should not happen if json_path_evaluate is correct

        if (table->rowCount >= table->rowCapacity) {
            // Resize rows array
            size_t new_capacity = table->rowCapacity * 2;
            JsonTableRow* new_rows = (JsonTableRow*)realloc(table->rows, new_capacity * sizeof(JsonTableRow));
            if (!new_rows) {
                if (error_message) *error_message = jt_strdup("Memory reallocation failed for table rows.");
                err_code = JT_ERROR_MEMORY_ALLOCATION;
                goto cleanup;
            }
            table->rows = new_rows;
            table->rowCapacity = new_capacity;
        }

        // Initialize data for the current row to NULL or zero
        table->rows[table->rowCount].data = calloc(1, table->totalRowDataSize);
        if (!table->rows[table->rowCount].data) {
            if (error_message) *error_message = jt_strdup("Memory allocation failed for row data buffer.");
            err_code = JT_ERROR_MEMORY_ALLOCATION;
            goto cleanup;
        }

        err_code = process_single_row_item(current_row_json_item, table, error_message);
        if (err_code != JT_SUCCESS) {
            // Error message should already be set by process_single_row_item
            // We might need to free the partially allocated row data if an error occurs here
            free(table->rows[table->rowCount].data);
            table->rows[table->rowCount].data = NULL;
            goto cleanup;
        }
        // Only increment rowCount if process_single_row_item was successful
        // table->rowCount is incremented inside process_single_row_item upon success
    }

cleanup:
    if (root_json) {
        cJSON_Delete(root_json);
        root_json = NULL;
    }
    json_path_result_free(&row_items_result);

    if (err_code != JT_SUCCESS && table) {
        json_table_free(table); // Free partially constructed table on error
        *result_table_ptr = NULL; // Ensure caller doesn't use partially freed table
    }

    return err_code;
}

static JsonTableError process_single_row_item(
    cJSON* row_json_item,
    JsonTable* table, // table_to_populate
    char** error_message) {

    JsonTableError col_err = JT_SUCCESS;
    void* current_row_data_buffer = table->rows[table->rowCount].data;

    for (size_t j = 0; j < table->columnCount; ++j) {
        JsonTableColumnDef* col_def = &(table->columnDefs[j]);
        JsonPathResult col_val_result = json_path_evaluate(row_json_item, col_def->jsonPath);
        cJSON* val_item = NULL;

        if (col_val_result.count > 0) {
            val_item = col_val_result.items[0]; // Take the first item if path resolves to multiple (should ideally be specific)
        }
        // If val_item is NULL here, it means the path didn't resolve or resolved to null.
        // Type conversion functions should handle NULL val_item gracefully (e.g., for strings or nullable types).

        char* conv_error_msg = NULL;
        switch (col_def->type) {
            case JT_INT:
                col_err = cjson_to_int(val_item, (int*)((char*)current_row_data_buffer + col_def->offset), &conv_error_msg);
                break;
            case JT_DOUBLE:
                col_err = cjson_to_double(val_item, (double*)((char*)current_row_data_buffer + col_def->offset), &conv_error_msg);
                break;
            case JT_STRING:
                col_err = cjson_to_string(val_item, (char**)((char*)current_row_data_buffer + col_def->offset), &conv_error_msg);
                break;
            default:
                if (error_message && !*error_message) *error_message = jt_strdup("Unsupported column data type.");
                col_err = JT_ERROR_INVALID_TYPE;
        }

        json_path_result_free(&col_val_result); // Free result from column path evaluation

        if (col_err != JT_SUCCESS) {
            if (error_message && !*error_message && conv_error_msg) {
                 char temp_err_msg[512];
                 snprintf(temp_err_msg, sizeof(temp_err_msg), "Error processing column '%s': %s", col_def->columnName ? col_def->columnName : "[Unnamed]", conv_error_msg);
                *error_message = jt_strdup(temp_err_msg);
                free(conv_error_msg); // Free the message from the conversion function
            } else if (error_message && !*error_message) {
                 char temp_err_msg[256];
                 snprintf(temp_err_msg, sizeof(temp_err_msg), "Error processing column '%s'.", col_def->columnName ? col_def->columnName : "[Unnamed]");
                *error_message = jt_strdup(temp_err_msg);
            } else if (conv_error_msg) { // error_message might already be set, or user didn't want one
                free(conv_error_msg);
            }
            return JT_ERROR_COLUMN_PROCESSING; // Return immediately on first column error
        }
    }
    table->rowCount++; // Increment only after all columns for the current row are processed successfully
    return JT_SUCCESS;
}


// --- Cleanup Function ---
void json_table_free(JsonTable* table) {
    if (!table) {
        return;
    }

    // Free data within each row
    if (table->rows) {
        for (size_t i = 0; i < table->rowCount; ++i) {
            if (table->rows[i].data) {
                // Free individual string members if any
                for (size_t j = 0; j < table->columnCount; ++j) {
                    if (table->columnDefs[j].type == JT_STRING) {
                        char** str_ptr = (char**)((char*)table->rows[i].data + table->columnDefs[j].offset);
                        if (*str_ptr) {
                            free(*str_ptr);
                            *str_ptr = NULL;
                        }
                    }
                }
                free(table->rows[i].data);
                table->rows[i].data = NULL;
            }
        }
        free(table->rows);
        table->rows = NULL;
    }

    // Free column definitions copy
    if (table->columnDefs) {
        // If column names or paths were dynamically allocated FOR the table struct, free them here.
        // Assuming they are const char* pointing to user's memory or literals for now.
        free(table->columnDefs);
        table->columnDefs = NULL;
    }

    // Free the table structure itself
    free(table);
}
