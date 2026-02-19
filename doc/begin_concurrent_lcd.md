
Logical Conflict Detection
==========================

## Logical Conflict Detection for BEGIN CONCURRENT

This branch extends the <a href=begin_concurrent.md>BEGIN CONCURRENT</a> 
feature with logical (range-based) conflict detection. The system falls back to
page-level locking unless all write transactions:

  *  are executed from within the same process,
  *  are started with "BEGIN CONCURRENT",
  *  use no more than 8 nested savepoints, and
  *  do not write to the database using incremental blob handles.

Configuration is done using:

        sqlite3_int64 szMax = (1*1024*1024*1024);
        int rc = sqlite3_config(SQLITE_CONFIG_SHAREDLOG_MAXSIZE, szMax);
        if( rc!=SQLITE_OK ) panic();

The above configures SQLite to use up to 1GiB of heap memory for each distinct
database opened to store "shared-log" entries - the data structure used to test
for conflicts when a BEGIN CONCURRENT transaction is committed.  Shared-logs
can be disabled by setting the max-size to 0. There is currently no way to
configure the limit separately for each database. The default value is 1GiB.

Like other sqlite3\_config() calls, this one must be made before any other
sqlite3\_xxx APIs are invoked by the process.

## Implementation Details

If logical conflict detection is enabled (if SQLITE\_CONFIG\_SHAREDLOG\_MAXSIZE
is configured to a value greater than zero), then as each BEGIN CONCURRENT
transaction is run the system collects lists of:

  1)  Ranges of keys read from index and table b-trees, and
  2)  The set of inserts and deletes made to index and table b-trees.

as well as the set of all database pages read during the transaction.

When a BEGIN CONCURRENT transaction is successfully committed, it creates
a new entry in the database's shared-log containing the set of table and
index keys inserted and deleted by the transaction (as collected in (2)
above).

When committing a BEGIN CONCURRENT transaction, the system first attempts
to validate the transaction by checking if any of the pages read by the
transaction have been modified since the transaction started, just as it
does in regular <a href=begin_concurrent.md>BEGIN CONCURRENT</a> mode. If
this reveals no conflicts, the transaction is committed to the database
as normal by flushing all dirty cache pages to disk.

Or, if there are page-level conflicts, the system checks if the in-process
shared-log for the database contains entries for all transactions committed
since the transaction being committed started. It may not, as:

  *  transactions may have been committed by another process, or
  *  transactions not started with BEGIN CONCURRENT may have been committed, or
  *  shared-log entries may have been discarded as part of enforcing the SQLITE\_CONFIG\_SHAREDLOG\_MAXSIZE limit, or
  *  transactions may have used incremental blob handles or too many nested
     savepoints.

If the shared-log does contain all required entries, the system loops through
the shared-log entries for all these transactions, comparing the set of keys
they wrote with the set of key ranges (as collected in (1)) read by this
transaction. If this procedure shows that there are no logical conflicts, then
the transaction is committed to disk by applying the transaction's inserts and
deletes to the latest database snapshot.

In other words, the system first attempts to commit the transaction using 
the regular BEGIN CONCURRENT page-level locking system, then falls back 
to logical conflict detection and transaction application if that fails.

There is an eponymous virtual table named "sqlite\_concurrent" that may be
queried from within an ongoing BEGIN CONCURRENT transaction. It returns a
list of the reads and writes currently accumulated for the transaction. For
example, given the following schema and transaction:

        CREATE TABLE t1(a INTEGER PRIMARY KEY, b TEXT UNIQUE);
        INSERT INTO t1 VALUES(1, 'one'), (2, 'two'), (3, 'three'),
                             (4, 'four'), (5, 'five'), (6, 'six');
        
        BEGIN CONCURRENT;
          SELECT * FROM t1 WHERE b='two';
          SELECT * FROM t1 WHERE a BETWEEN 2 AND 3;
          INSERT INTO t1 VALUES(7, 'seven');

Then sqlite\_concurrent contains the following:

        sqlite> SELECT * FROM sqlite_concurrent;
        ╭──────┬────────┬─────────────┬────────────────╮
        │ root │   op   │     k1      │       k2       │
        ╞══════╪════════╪═════════════╪════════════════╡
        │    2 │ read   │ 2           │ 4              │
        │    2 │ read   │ 7           │ 7              │
        │    3 │ read   │ ('two')-    │ ('two')+       │
        │    3 │ read   │ ('seven')   │ ('seven')      │
        │    2 │ insert │ 7           │ (NULL,'seven') │
        │    3 │ insert │ ('seven',7) │ NULL           │
        ╰──────┴────────┴─────────────┴────────────────╯
        
The first row indicates that the transaction scanned rowids between 2 and 4,
inclusive. This is confusing, as the WHERE clause on the relevent SELECT 
query was "BETWEEN 2 AND 3". This is a consequence of the way this extension
collects scans at the b-tree level - sometimes a range scan is recorded as 
having one extra row at the end of it that does not actually match the
predicate.

The second row says it also read rowid 7. This was to detect a conflict on 
the rowid when inserting the new row. Index keys between ('seven') and
('seven') were scanned for a similar purpose - to check if the new row
causes a conflict in the UNIQUE index.

And so on.


