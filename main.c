#include <stdio.h>
#include <stdlib.h>
#include "json_table.h"

// Helper function to print the extracted table
void print_json_table(JsonTable* table) {
    if (!table) {
        printf("Table is NULL.\n");
        return;
    }

    printf("Table has %zu rows and %zu columns.\n", table->rowCount, table->columnCount);

    // Print header
    for (size_t j = 0; j < table->columnCount; ++j) {
        printf("%-20s | ", table->columnDefs[j].columnName ? table->columnDefs[j].columnName : "N/A");
    }
    printf("\n");
    for (size_t j = 0; j < table->columnCount; ++j) {
        for(int k=0; k<20; ++k) printf("-");
        printf("---");
    }
    printf("\n");

    // Print rows
    for (size_t i = 0; i < table->rowCount; ++i) {
        for (size_t j = 0; j < table->columnCount; ++j) {
            void* cell_data_ptr = (char*)table->rows[i].data + table->columnDefs[j].offset;
            switch (table->columnDefs[j].type) {
                case JT_INT:
                    printf("%-20d | ", *(int*)cell_data_ptr);
                    break;
                case JT_DOUBLE:
                    printf("%-20.2f | ", *(double*)cell_data_ptr);
                    break;
                case JT_STRING:
                    {
                        char* str_val = *(char**)cell_data_ptr;
                        printf("%-20s | ", str_val ? str_val : "NULL");
                    }
                    break;
                default:
                    printf("%-20s | ", "UNKN_TYPE");
            }
        }
        printf("\n");
    }
    printf("\n");
}

int main() {
    // Example 1: Simple array of objects
    const char* json_string_1 = "{\n"
                              "  \"storeName\": \"My Tech Store\",\n"
                              "  \"products\": [\n"
                              "    { \"id\": 1, \"name\": \"Laptop\", \"price\": 1200.50, \"specs\": { \"cpu\": \"i7\", \"ram\": 16 } },\n"
                              "    { \"id\": 2, \"name\": \"Mouse\", \"price\": 25.99, \"specs\": { \"dpi\": 1200 } },\n"
                              "    { \"id\": 3, \"name\": \"Keyboard\", \"price\": 75.00, \"tags\": [\"mechanical\", \"rgb\"] },\n"
                              "    { \"id\": 4, \"name\": \"Monitor\", \"price\": 300.75, \"specs\": null }\n"
                              "  ]\n"
                              "}";

    printf("--- Example 1: Extracting products ---\n");
    JsonTableColumnDef cols1[] = {
        {"ID", "$.id", JT_INT},
        {"Product Name", "$.name", JT_STRING},
        {"Price", "$.price", JT_DOUBLE},
        {"CPU", "$.specs.cpu", JT_STRING} // This will be NULL for items without it
    };
    size_t num_cols1 = sizeof(cols1) / sizeof(cols1[0]);

    JsonTable* table1 = NULL;
    char* error_msg1 = NULL;

    JsonTableError err1 = json_table_process(json_string_1, "$.products[*]", cols1, num_cols1, &table1, &error_msg1);

    if (err1 == JT_SUCCESS) {
        printf("Successfully processed JSON for Example 1.\n");
        print_json_table(table1);
    } else {
        printf("Error in Example 1 (code %d): %s\n", err1, json_table_error_string(err1));
        if (error_msg1) {
            printf("Details: %s\n", error_msg1);
        }
    }
    if (error_msg1) free(error_msg1);
    json_table_free(table1);
    printf("-------------------------------------\n\n");


    // Example 2: JSON is a single object, to be treated as one row
    const char* json_string_2 = "{ \"name\": \"Alice\", \"age\": 30, \"city\": \"New York\" }";

    printf("--- Example 2: Single object as a row ---\n");
    JsonTableColumnDef cols2[] = {
        {"Person Name", "$.name", JT_STRING},
        {"Age", "$.age", JT_INT},
        {"City", "$.city", JT_STRING},
        {"Country", "$.country", JT_STRING} // Non-existent field
    };
    size_t num_cols2 = sizeof(cols2) / sizeof(cols2[0]);
    JsonTable* table2 = NULL;
    char* error_msg2 = NULL;

    // Using "$" as row path to indicate the root object is the row.
    JsonTableError err2 = json_table_process(json_string_2, "$", cols2, num_cols2, &table2, &error_msg2);

    if (err2 == JT_SUCCESS) {
        printf("Successfully processed JSON for Example 2.\n");
        print_json_table(table2);
    } else {
        printf("Error in Example 2 (code %d): %s\n", err2, json_table_error_string(err2));
        if (error_msg2) {
            printf("Details: %s\n", error_msg2);
        }
    }
    if (error_msg2) free(error_msg2);
    json_table_free(table2);
    printf("-------------------------------------\n\n");


    // Example 3: Malformed JSON
    printf("--- Example 3: Malformed JSON ---\n");
    const char* json_string_3 = "{ \"name\": \"Bob\", \"age\": 40, city: \"London\" }"; // "city" key not quoted
    JsonTable* table3 = NULL;
    char* error_msg3 = NULL;

    JsonTableError err3 = json_table_process(json_string_3, "$", cols2, num_cols2, &table3, &error_msg3);
    if (err3 == JT_SUCCESS) {
        print_json_table(table3);
    } else {
        printf("Error in Example 3 (code %d): %s\n", err3, json_table_error_string(err3));
        if (error_msg3) {
            printf("Details: %s\n", error_msg3);
        }
    }
    if (error_msg3) free(error_msg3);
    json_table_free(table3); // Safe to call even if table3 is NULL
    printf("-------------------------------------\n\n");

    // Example 4: Path to non-array for wildcard
    printf("--- Example 4: Wildcard on non-array path ---\n");
    const char* json_string_4 = "{ \"data\": { \"value\": 123 } }";
    JsonTableColumnDef cols4[] = { {"Value", "$.value", JT_INT} };
    JsonTable* table4 = NULL;
    char* error_msg4 = NULL;
    JsonTableError err4 = json_table_process(json_string_4, "$.data[*]", cols4, 1, &table4, &error_msg4);
     if (err4 == JT_SUCCESS) {
        printf("Successfully processed JSON for Example 4 (expecting empty table).\n");
        print_json_table(table4); // Should be an empty table
    } else {
        printf("Error in Example 4 (code %d): %s\n", err4, json_table_error_string(err4));
        if (error_msg4) {
            printf("Details: %s\n", error_msg4);
        }
    }
    if (error_msg4) free(error_msg4);
    json_table_free(table4);
    printf("-------------------------------------\n\n");


    // Example 5: Array of simple types (not objects)
    const char* json_string_5 = "[10, 20, 30, null, 40]";
    printf("--- Example 5: Array of simple types ---\n");
    JsonTableColumnDef cols5[] = {
        {"Number", "$", JT_INT} // Path "$" here refers to the array element itself
    };
    JsonTable* table5 = NULL;
    char* error_msg5 = NULL;
    // For an array of simple types, each element is a row.
    // The column path `$` means "this element".
    JsonTableError err5 = json_table_process(json_string_5, "$[*]", cols5, 1, &table5, &error_msg5);
     if (err5 == JT_SUCCESS) {
        printf("Successfully processed JSON for Example 5.\n");
        print_json_table(table5);
    } else {
        printf("Error in Example 5 (code %d): %s\n", err5, json_table_error_string(err5));
        if (error_msg5) {
            printf("Details: %s\n", error_msg5); // Expect type error for 'null' if JT_INT is strict
        }
    }
    if (error_msg5) free(error_msg5);
    json_table_free(table5);
    printf("-------------------------------------\n\n");


    return 0;
}
