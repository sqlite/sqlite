
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




