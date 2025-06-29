import pandas as pd
from jsonpath_ng import jsonpath, parse
import json

def json_table(json_input, root_path: str, columns_config: list) -> pd.DataFrame:
    """
    Transforms a JSON input into a Pandas DataFrame based on a root path and column configurations.

    Args:
        json_input: The JSON data, can be a JSON string or a Python list/dict.
        root_path: A JSONPath string (e.g., '$[*]') to select the array of objects
                   from which to extract rows.
        columns_config: A list of dictionaries, where each dictionary defines a column
                        in the output DataFrame. Each dictionary should have:
                        - 'name' (str): The name of the output column.
                        - 'path' (str): The JSONPath string to extract the value for this
                                        column from an element selected by root_path.
                        - 'datatype' (type, optional): The Python type to which the
                                                     extracted value should be cast
                                                     (e.g., int, str, float).
                        - 'on_empty' (any, optional): The value to use if the 'path'
                                                    does not find a match in a JSON element.
                                                    Defaults to None.
                        - 'on_error' (str or any, optional): Policy for handling errors during
                                                          datatype conversion.
                                                          - 'raise': Raises the exception (default).
                                                          - 'null': Returns None.
                                                          - any other value: Returns that specific
                                                                           default value.
    Returns:
        A Pandas DataFrame representing the structured data.
    """
    if isinstance(json_input, str):
        try:
            processed_json_input = json.loads(json_input)
        except json.JSONDecodeError as e:
            raise ValueError("Invalid JSON string provided.") from e
    else:
        processed_json_input = json_input

    root_path_expr = parse(root_path)
    json_elements = root_path_expr.find(processed_json_input)

    all_rows = []

    for element in json_elements:
        current_row = {}
        for col_config in columns_config:
            col_name = col_config['name']
            col_path = col_config['path']

            col_path_expr = parse(col_path)
            found_values = col_path_expr.find(element.value)

            if not found_values:
                value_to_set = col_config.get('on_empty', None)
            else:
                # Assuming path expressions for columns usually point to a single value.
                # Take the first one if multiple are found.
                raw_value = found_values[0].value
                value_to_set = _apply_type_and_error_handling(raw_value, col_config)

            current_row[col_name] = value_to_set
        all_rows.append(current_row)

    # Ensure all columns are present in the DataFrame, even if all values were None/empty
    # This is important if the first few rows lack certain optional fields
    # but those columns are expected based on columns_config.
    # Ensure unique column names in the order of first appearance.
    df_columns = list(dict.fromkeys(cc['name'] for cc in columns_config))

    if not all_rows: # Handle case where root_path yields no elements
        return pd.DataFrame(columns=df_columns)

    return pd.DataFrame(all_rows, columns=df_columns)

def _apply_type_and_error_handling(value, column_config: dict):
    """
    Applies datatype conversion and error handling for a single value.
    """
    col_datatype = column_config.get('datatype')
    on_error_policy = column_config.get('on_error', 'raise')

    if col_datatype:
        # Handle None values before attempting type conversion,
        # unless the target type is explicitly type(None) (which is unlikely but possible)
        if value is None and col_datatype is not type(None):
            return None # Typically, if the source value is None, the typed value is also None.

        try:
            return col_datatype(value)
        except (ValueError, TypeError) as e:
            if on_error_policy == 'raise':
                raise
            elif on_error_policy == 'null':
                return None
            else:
                # on_error_policy is a default value
                return on_error_policy
    else:
        # No datatype specified, return value as is
        return value
