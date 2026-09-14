#include "json_path.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h> // For isdigit
#include <stdbool.h> // For true/false

// Helper to free JsonPathResult
void json_path_result_free(JsonPathResult* result) {
    if (result) {
        free(result->items); // Frees the array of pointers
        result->items = NULL;
        result->count = 0;
    }
}

// Internal helper to duplicate a string, as strdup is not standard C90
// and we want to manage memory explicitly.
static char* jp_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* new_s = (char*)malloc(len);
    if (!new_s) return NULL;
    memcpy(new_s, s, len);
    return new_s;
}

// Internal helper to add a cJSON item to a JsonPathResult
static JsonPathResult append_item_to_result(JsonPathResult result, cJSON* item) {
    if (!item) return result; // Don't add NULL items if that's the resolution

    cJSON** new_items = (cJSON**)realloc(result.items, (result.count + 1) * sizeof(cJSON*));
    if (!new_items) {
        // Realloc failed, free existing and return empty
        free(result.items);
        return (JsonPathResult){NULL, 0, false};
    }
    result.items = new_items;
    result.items[result.count] = item;
    result.count++;
    return result;
}


JsonPathResult json_path_evaluate(cJSON* current_node, const char* path_expr) {
    JsonPathResult result = {NULL, 0, false};
    if (!current_node || !path_expr) {
        return result;
    }

    char* path_copy = jp_strdup(path_expr);
    if (!path_copy) {
        return result; // Memory allocation failure
    }

    char* current_segment = path_copy;
    cJSON* current_json_item = current_node;
    result = append_item_to_result(result, current_json_item); // Start with the current node

    if (strcmp(current_segment, "$") == 0) {
        // Path is just "$", refers to the root_node itself
        free(path_copy);
        // Result already contains current_node, so just return
        return result;
    }

    // If path starts with "$.", move past it.
    if (strncmp(current_segment, "$.", 2) == 0) {
        current_segment += 2;
    } else if (current_segment[0] == '$') { // Path like "$[0]" or "$['key']" (though we don't support quoted keys yet)
         current_segment += 1;
    }
    // If path does not start with '$', it's assumed to be relative from current_node
    // (this behavior is for when json_path_evaluate is called recursively for column paths)

    char* next_delimiter;
    while (current_segment && *current_segment != '\0' && result.count > 0) {
        JsonPathResult current_iteration_result = {NULL, 0, false};
        result.is_array_wildcard_result = false; // Reset for this segment

        for (size_t i = 0; i < result.count; ++i) {
            current_json_item = result.items[i];
            if (!current_json_item) continue;

            if (*current_segment == '[') { // Array access: [index] or [*]
                current_segment++; // Skip '['
                next_delimiter = strchr(current_segment, ']');
                if (!next_delimiter) {
                    json_path_result_free(&result);
                    json_path_result_free(&current_iteration_result);
                    free(path_copy);
                    return (JsonPathResult){NULL, 0, false}; // Invalid array segment
                }
                *next_delimiter = '\0'; // Terminate segment

                if (strcmp(current_segment, "*") == 0) { // Wildcard [*]
                    if (cJSON_IsArray(current_json_item)) {
                        result.is_array_wildcard_result = true;
                        cJSON* array_child = current_json_item->child;
                        while (array_child) {
                            current_iteration_result = append_item_to_result(current_iteration_result, array_child);
                            array_child = array_child->next;
                        }
                    } else {
                        // Wildcard on non-array, effectively finds nothing for this path item
                        // Continue to next item in 'result.items' if any
                    }
                } else { // Specific index [index]
                    char* endptr;
                    long index = strtol(current_segment, &endptr, 10);
                    if (*endptr != '\0' || !cJSON_IsArray(current_json_item)) {
                         // Invalid index or not an array
                        // Continue to next item in 'result.items' if any
                        continue;
                    }
                    cJSON* indexed_item = cJSON_GetArrayItem(current_json_item, (int)index);
                    if (indexed_item) {
                        current_iteration_result = append_item_to_result(current_iteration_result, indexed_item);
                    }
                }
                current_segment = next_delimiter + 1;
            } else { // Object access: .key
                if (*current_segment == '.') current_segment++; // Skip leading '.' if present

                next_delimiter = strchr(current_segment, '.');
                char* next_bracket = strchr(current_segment, '[');
                if (next_bracket && (!next_delimiter || next_bracket < next_delimiter)) {
                    next_delimiter = next_bracket;
                }

                char temp_char = 0;
                if (next_delimiter) {
                    temp_char = *next_delimiter;
                    *next_delimiter = '\0';
                }

                if (cJSON_IsObject(current_json_item)) {
                    cJSON* object_child = cJSON_GetObjectItemCaseSensitive(current_json_item, current_segment);
                    if (object_child) {
                         current_iteration_result = append_item_to_result(current_iteration_result, object_child);
                    }
                } else {
                     // Trying to access key on non-object
                     // Continue to next item in 'result.items' if any
                }

                if (next_delimiter) {
                    *next_delimiter = temp_char; // Restore char
                    current_segment = next_delimiter;
                } else {
                    current_segment = NULL; // End of path
                }
            }
        } // End for loop over result.items

        json_path_result_free(&result); // Free previous result's item array
        result = current_iteration_result; // Assign new result

        if (result.count == 0 && current_segment && *current_segment != '\0') {
            // If at any point we have no items but still have path segments, it's a miss.
            break;
        }

    } // End while loop over path segments

    free(path_copy);
    return result;
}
