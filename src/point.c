/*
 * Point-related functionality for CS541 project.
 */

#include "sqliteInt.h"

#include <math.h>
#include <string.h>

struct Point;

static void distFunc(
    sqlite3_context *context,
    int argc,
    sqlite3_value **argv
) {
    float coords[2][2];
    double result;
    const char *text;

    if (argc != 2) {
        sqlite3_result_error(context, "Invalid # of arguments for dist(), expected 2", -1);
        return;
    }

    for (int i = 0; i < 2; i++) {
        if (sqlite3_value_type(argv[i]) == SQLITE_POINT) {
            coords[i][0] = sqlite3_value_point_x(argv[i]);
            coords[i][1] = sqlite3_value_point_y(argv[i]);
        }
        else {
            if (sqlite3_value_type(argv[i]) != SQLITE3_TEXT) goto dist_fail;

            text = sqlite3_value_text(argv[i]);

            if ((!text) || (sqlite3AtoPoint(
                sqlite3_value_text(argv[i]),
                (Point*) coords[i],
                strlen(text),
                SQLITE_UTF8
            ) <= 0)) goto dist_fail;
        }
    }

    result = pow(
        (double)(powf(coords[0][0] - coords[1][0], 2.0f) + powf(coords[0][1] - coords[1][1], 2.0f)),
        0.5
    );

    sqlite3_result_double(context, result);
    return;

dist_fail:
    sqlite3_result_error(context, "Provide points or strings convertible to points to dist()", -1);
}

void sqlite3RegisterPointFunctions(void){
  static FuncDef aPointFuncs[] = {
    DFUNCTION(dist, 2, 0, 0, distFunc),
  };
  sqlite3InsertBuiltinFuncs(aPointFuncs, sizeof(aPointFuncs) / sizeof(aPointFuncs[0]));
}