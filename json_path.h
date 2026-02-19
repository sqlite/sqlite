#ifndef JSON_PATH_H
#define JSON_PATH_H

#include "cJSON.h" // Needs cJSON structures
#include <stddef.h> // For size_t

// Structure to hold the result of a JSONPath evaluation.
// It might be a single cJSON item or an array of cJSON items (e.g., from [*]).
typedef struct {
    cJSON** items;     // Array of pointers to cJSON items.
                       // If the path resolves to a single item, this will have one element.
                       // If the path resolves to multiple items (e.g. wildcard), this will have multiple.
                       // The items are BORROWED from the main parsed cJSON structure, not copies.
    size_t count;      // Number of items in the 'items' array.
    cJSON_bool is_array_wildcard_result; // Flag to indicate if the result came from a [*] operation on an array
} JsonPathResult;

// Evaluates a JSONPath expression against a cJSON object.
//
// Parameters:
//   root_node: The cJSON object (or array) to evaluate the path against.
//   path_expr: The JSONPath expression string.
//              Supported:
//                - `$` (root)
//                - `.child_name` (object member access)
//                - `[index]` (array element access by specific index)
//                - `[*]` (array wildcard - returns all elements of an array)
//
// Returns:
//   A JsonPathResult structure. The caller is responsible for freeing
//   the 'items' array within this structure using `json_path_result_free()`.
//   The cJSON items themselves are part of the main document and should not be freed here.
//   If an error occurs (e.g., invalid path, item not found for a specific part of the path),
//   `items` will be NULL and `count` will be 0.
//
// Notes:
//   - This is a simplified implementation. It does not support deep scanning (..),
//     script expressions, filters, or more complex path features.
//   - Object member names and array indices are processed sequentially.
//   - If a segment of the path does not find an item (e.g., non-existent key or out-of-bounds index),
//     the evaluation stops and returns an empty result, unless it's a wildcard `[*]`.
JsonPathResult json_path_evaluate(cJSON* current_node, const char* path_expr);

// Frees the memory allocated for the JsonPathResult structure, specifically the 'items' array.
// Does NOT free the cJSON items themselves.
void json_path_result_free(JsonPathResult* result);

#endif // JSON_PATH_H
