import unittest
import pandas as pd
from pandas.testing import assert_frame_equal
import json
from .json_table import json_table # Use relative import

class TestJsonTable(unittest.TestCase):

    def test_basic_functionality_user_example(self):
        json_input = """
        [
          {
            "id_produto": 101,
            "nome_produto": "Laptop",
            "quantidade": 1,
            "preco_unitario": 1200.00
          },
          {
            "id_produto": 205,
            "nome_produto": "Mouse",
            "quantidade": 2,
            "preco_unitario": 25.00
          }
        ]
        """
        columns_config = [
            {'name': 'id_produto', 'path': '$.id_produto', 'datatype': int},
            {'name': 'nome_produto', 'path': '$.nome_produto', 'datatype': str},
            {'name': 'quantidade', 'path': '$.quantidade', 'datatype': int},
            {'name': 'preco_unitario', 'path': '$.preco_unitario', 'datatype': float}
        ]

        expected_data = {
            'id_produto': [101, 205],
            'nome_produto': ["Laptop", "Mouse"],
            'quantidade': [1, 2],
            'preco_unitario': [1200.00, 25.00]
        }
        expected_df = pd.DataFrame(expected_data)

        result_df = json_table(json_input, root_path='$[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df)

    def test_python_object_input(self):
        json_input_obj = [
          {
            "id_produto": 101,
            "nome_produto": "Laptop",
            "quantidade": 1,
            "preco_unitario": 1200.00
          }
        ]
        columns_config = [
            {'name': 'id_produto', 'path': '$.id_produto', 'datatype': int},
            {'name': 'nome_produto', 'path': '$.nome_produto', 'datatype': str},
        ]
        expected_df = pd.DataFrame({'id_produto': [101], 'nome_produto': ["Laptop"]})
        result_df = json_table(json_input_obj, root_path='$[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df)

    def test_empty_json_input_array(self):
        json_input = "[]"
        columns_config = [
            {'name': 'id', 'path': '$.id', 'datatype': int}
        ]
        expected_df = pd.DataFrame(columns=['id'])
        # Pandas by default creates float64 for empty int columns if not specified,
        # or int32/64 depending on platform.
        # For consistency in tests, we can cast if needed or accept object.
        # The function itself will create columns based on names.
        # Let's ensure the structure from json_table has the correct dtype if possible.
        # Current implementation will result in object type for empty columns from empty list.
        # This is acceptable. If specific dtypes are needed for empty DFs, json_table would need adjustment.
        result_df = json_table(json_input, root_path='$[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df, check_dtype=False) # Check_dtype False for empty or all-NaN cols

    def test_root_path_no_match(self):
        json_input = '[{"id": 1}]'
        columns_config = [{'name': 'id', 'path': '$.id', 'datatype': int}]
        expected_df = pd.DataFrame(columns=['id'])
        result_df = json_table(json_input, root_path='$.nonexistent[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df, check_dtype=False)

    def test_column_path_no_match_on_empty_none(self):
        json_input = '[{"name": "A"}]'
        columns_config = [
            {'name': 'name', 'path': '$.name', 'datatype': str},
            {'name': 'value', 'path': '$.value', 'datatype': int, 'on_empty': None}
        ]
        expected_data = {'name': ["A"], 'value': [None]}
        expected_df = pd.DataFrame(expected_data)
        # Pandas infers 'object' for [None] if int is target, which is fine.
        result_df = json_table(json_input, root_path='$[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df)

    def test_column_path_no_match_on_empty_default(self):
        json_input = '[{"name": "B"}]'
        columns_config = [
            {'name': 'name', 'path': '$.name'}, # No datatype
            {'name': 'value', 'path': '$.value', 'datatype': int, 'on_empty': -1}
        ]
        expected_data = {'name': ["B"], 'value': [-1]}
        expected_df = pd.DataFrame(expected_data)
        result_df = json_table(json_input, root_path='$[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df)

    def test_datatype_conversion_error_raise(self):
        json_input = '[{"value": "not_an_int"}]'
        columns_config = [
            {'name': 'val', 'path': '$.value', 'datatype': int, 'on_error': 'raise'}
        ]
        with self.assertRaises(ValueError):
            json_table(json_input, root_path='$[*]', columns_config=columns_config)

    def test_datatype_conversion_error_null(self):
        json_input = '[{"value": "not_an_int"}, {"value": "123"}]'
        columns_config = [
            {'name': 'val', 'path': '$.value', 'datatype': int, 'on_error': 'null'}
        ]
        expected_data = {'val': [None, 123]} # Pandas will make this float if None is present
        expected_df = pd.DataFrame(expected_data, dtype=object) # Explicitly object to match None behavior
        result_df = json_table(json_input, root_path='$[*]', columns_config=columns_config)
        # Convert result column to object for comparison if it became float due to NaN
        if pd.api.types.is_numeric_dtype(result_df['val']) and result_df['val'].isnull().any():
             result_df['val'] = result_df['val'].astype(object).where(pd.notna(result_df['val']), None)

        assert_frame_equal(result_df, expected_df)


    def test_datatype_conversion_error_default_value(self):
        json_input = '[{"value": "not_an_int"}, {"value": "456"}]'
        columns_config = [
            {'name': 'val', 'path': '$.value', 'datatype': int, 'on_error': -99}
        ]
        expected_data = {'val': [-99, 456]}
        expected_df = pd.DataFrame(expected_data)
        result_df = json_table(json_input, root_path='$[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df)

    def test_nested_root_path(self):
        json_input = """
        {
          "data": {
            "items": [
              {"id": 1, "name": "Item1"},
              {"id": 2, "name": "Item2"}
            ]
          }
        }
        """
        columns_config = [
            {'name': 'item_id', 'path': '$.id', 'datatype': int},
            {'name': 'item_name', 'path': '$.name', 'datatype': str}
        ]
        expected_data = {
            'item_id': [1, 2],
            'item_name': ["Item1", "Item2"]
        }
        expected_df = pd.DataFrame(expected_data)
        result_df = json_table(json_input, root_path='$.data.items[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df)

    def test_no_datatype_specified(self):
        json_input = '[{"value": 123, "text": "hello"}]'
        columns_config = [
            {'name': 'num_val', 'path': '$.value'},
            {'name': 'str_val', 'path': '$.text'}
        ]
        expected_data = {'num_val': [123], 'str_val': ["hello"]}
        expected_df = pd.DataFrame(expected_data)
        result_df = json_table(json_input, root_path='$[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df)

    def test_invalid_json_string(self):
        json_input = '{"invalid_json": "test"' # Missing closing brace
        columns_config = [{'name': 'col', 'path': '$.foo'}]
        with self.assertRaisesRegex(ValueError, "Invalid JSON string provided."):
            json_table(json_input, root_path='$[*]', columns_config=columns_config)

    def test_column_name_already_exists_behavior(self):
        # If two column configs have the same 'name', the latter should overwrite the former
        # This is standard dict behavior when building current_row
        json_input = '[{"a": 1, "b": 2}]'
        columns_config = [
            {'name': 'output_col', 'path': '$.a'},
            {'name': 'output_col', 'path': '$.b'}
        ]
        expected_data = {'output_col': [2]} # Value from '$.b' should win
        expected_df = pd.DataFrame(expected_data)
        result_df = json_table(json_input, root_path='$[*]', columns_config=columns_config)
        assert_frame_equal(result_df, expected_df)

    def test_value_is_none_with_datatype(self):
        json_input = '[{"val": null}, {"val": 10}]'
        columns_config = [
            {'name': 'my_val', 'path': '$.val', 'datatype': int, 'on_error': 'null'}
        ]
        # Pandas will make a column float if it has int and None.
        # To compare apples-to-apples, we can specify object dtype for the expected,
        # or ensure the result is handled correctly.
        expected_data = {'my_val': [None, 10]}
        expected_df = pd.DataFrame(expected_data, dtype=object) # Use object for None + int

        result_df = json_table(json_input, root_path='$[*]', columns_config=columns_config)

        # Coerce result column to object if it became float due to NaN/None
        if pd.api.types.is_numeric_dtype(result_df['my_val']) and result_df['my_val'].isnull().any():
             result_df['my_val'] = result_df['my_val'].astype(object).where(pd.notna(result_df['my_val']), None)

        assert_frame_equal(result_df, expected_df)

if __name__ == '__main__':
    unittest.main()
