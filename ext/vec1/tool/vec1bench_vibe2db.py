
import h5py
import sqlite3
import numpy as np
import os
import sys

def sqlite_type_for(dtype):
    # Everything goes in as BLOB; dtype is recorded separately
    return "BLOB"

def sanitize(name):
    # HDF5 paths like "/train" → "train"
    return name.strip("/").replace("/", "_")

def main(h5_path, sqlite_path):
    if os.path.exists(sqlite_path):
        os.remove(sqlite_path)

    conn = sqlite3.connect(sqlite_path)
    cur = conn.cursor()

    with h5py.File(h5_path, "r") as f:

        dm = "unknown"
        cur.execute("CREATE TABLE meta(field TEXT, value ANY)")
        if "distance" in f.attrs:
            dm = f.attrs["distance"]
            if isinstance(dm, bytes):
                dm = dm.decode("utf-8")
            cur.execute("INSERT INTO meta VALUES('distance', ?)", (dm,))

        print("VIBE distance metric:", dm)

        def visit(name, obj):
            if not isinstance(obj, h5py.Dataset):
                return



            data = obj[()]
            table = sanitize(name)

            print(f"Importing {name} → table '{table}'  shape={data.shape} dtype={data.dtype}")

            # Create table
            cur.execute(f"""
                CREATE TABLE {table} (
                    id  INTEGER PRIMARY KEY,
                    vec BLOB NOT NULL
                )
            """)

            # Metadata table (created once)
            cur.execute("""
                CREATE TABLE IF NOT EXISTS __meta__ (
                    table_name TEXT PRIMARY KEY,
                    dtype TEXT,
                    shape TEXT
                )
            """)

            cur.execute(
                "INSERT INTO __meta__ VALUES (?, ?, ?)",
                (table, str(data.dtype), str(data.shape))
            )

            # Insert rows
            if data.ndim == 1:
                rows = [(i, data[i].tobytes()) for i in range(data.shape[0])]
            else:
                rows = [(i, data[i].tobytes()) for i in range(data.shape[0])]

            cur.executemany(
                f"INSERT INTO {table} (id, vec) VALUES (?, ?)",
                rows
            )

            conn.commit()

        f.visititems(visit)

    conn.close()
    print("Done.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: hdf5_to_sqlite.py input.hdf5 output.sqlite")
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])
