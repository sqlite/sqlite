from json_utils.json_table import json_table
import json

# Example JSON data as a string (similar to the user's problem description)
json_data_string = """
[
  {
    "id_produto": 101,
    "nome_produto": "Laptop",
    "quantidade": 1,
    "preco_unitario": 1200.00,
    "categoria": "Eletrônicos"
  },
  {
    "id_produto": 205,
    "nome_produto": "Mouse",
    "quantidade": 2,
    "preco_unitario": 25.00
  },
  {
    "id_produto": 310,
    "nome_produto": "Teclado",
    "quantidade": 1,
    "preco_unitario": 75.00,
    "categoria": "Periféricos",
    "detalhes_extra": {"cor": "preto"}
  }
]
"""

# Column configuration
# This defines how to map JSON fields to DataFrame columns
columns_config = [
    {'name': 'ID do Produto', 'path': '$.id_produto', 'datatype': int},
    {'name': 'Nome', 'path': '$.nome_produto', 'datatype': str},
    {'name': 'Qtd', 'path': '$.quantidade', 'datatype': int},
    {'name': 'Preço Unitário', 'path': '$.preco_unitario', 'datatype': float},
    {'name': 'Categoria', 'path': '$.categoria', 'datatype': str, 'on_empty': 'N/A'}, # Handle missing Categoria
    {'name': 'Cor', 'path': '$.detalhes_extra.cor', 'datatype': str, 'on_empty': 'Não especificada'} # Nested path
]

def run_example():
    print("Demonstrating json_table functionality:\n")

    # 1. Using JSON string input
    print("--- Example 1: Processing JSON string ---")
    try:
        df_from_string = json_table(
            json_input=json_data_string,
            root_path='$[*]',  # Iterate over each element in the root array
            columns_config=columns_config
        )
        print("DataFrame created from JSON string:")
        print(df_from_string)
        print("\n")
    except Exception as e:
        print(f"Error processing JSON string: {e}\n")

    # 2. Using Python object input (parsed JSON)
    print("--- Example 2: Processing Python dictionary/list ---")
    try:
        python_object_input = json.loads(json_data_string) # Simulate already parsed JSON
        df_from_object = json_table(
            json_input=python_object_input,
            root_path='$[*]',
            columns_config=columns_config
        )
        print("DataFrame created from Python object:")
        print(df_from_object)
        print("\n")
    except Exception as e:
        print(f"Error processing Python object: {e}\n")

    # 3. Example with a different root path (e.g., if items were nested)
    print("--- Example 3: Processing with a nested root_path ---")
    nested_json_data = """
    {
      "pedido_id": "XYZ123",
      "itens_pedido": [
        { "sku": "A001", "descricao": "Produto Alpha", "valor": 10.50 },
        { "sku": "B002", "descricao": "Produto Beta", "valor": 20.75 }
      ]
    }
    """
    nested_columns_config = [
        {'name': 'SKU', 'path': '$.sku', 'datatype': str},
        {'name': 'Descrição', 'path': '$.descricao', 'datatype': str},
        {'name': 'Valor', 'path': '$.valor', 'datatype': float}
    ]
    try:
        df_nested = json_table(
            json_input=nested_json_data,
            root_path='$.itens_pedido[*]', # Target the nested array
            columns_config=nested_columns_config
        )
        print("DataFrame from nested items:")
        print(df_nested)
        print("\n")
    except Exception as e:
        print(f"Error processing nested JSON: {e}\n")

if __name__ == '__main__':
    run_example()
