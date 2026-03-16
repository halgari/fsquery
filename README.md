# fsquery

A [DuckDB](https://duckdb.org/) extension that lets you query filesystem metadata using SQL. `fsquery_scan()` recursively traverses directories and returns file metadata as a table, with filter and projection pushdown for efficient scanning.

## Installation

```sql
INSTALL fsquery FROM community;
LOAD fsquery;
```

## Usage

### Scan a directory and find large files

```sql
SELECT path, size, mtime
FROM fsquery_scan('/home')
WHERE is_regular_file AND size > 1000000
ORDER BY size DESC
LIMIT 10;
```

### Filter pushdown — only `/etc` and `/var/log` are traversed, not the entire filesystem

```sql
SELECT path, size FROM fsquery_scan()
WHERE path >= '/etc' OR path >= '/var/log';
```

### Find recently modified files

```sql
SELECT path, mtime FROM fsquery_scan('/home')
WHERE is_regular_file AND mtime > TIMESTAMP '2025-01-01'
ORDER BY mtime DESC;
```

### Find all symlinks

```sql
SELECT path FROM fsquery_scan('/usr') WHERE is_symlink;
```

## Columns

`fsquery_scan()` returns the following columns:

| Column | Type | Description |
|--------|------|-------------|
| `path` | `VARCHAR` | Full file path |
| `mode` | `INTEGER` | File mode/permissions |
| `size` | `BIGINT` | File size in bytes |
| `uid` | `INTEGER` | Owner user ID |
| `gid` | `INTEGER` | Owner group ID |
| `atime` | `TIMESTAMP` | Last access time |
| `mtime` | `TIMESTAMP` | Last modification time |
| `ctime` | `TIMESTAMP` | Last status change time |
| `nlink` | `INTEGER` | Number of hard links |
| `ino` | `BIGINT` | Inode number |
| `dev` | `BIGINT` | Device ID |
| `is_regular_file` | `BOOLEAN` | True if regular file |
| `is_directory` | `BOOLEAN` | True if directory |
| `is_symlink` | `BOOLEAN` | True if symbolic link |

## Features

- **Filter pushdown**: Path prefix filters are pushed down to avoid scanning irrelevant directories. When using `fsquery_scan()` (no explicit path), filters like `path >= '/home/user/docs'` cause only those subtrees to be traversed.
- **Projection pushdown**: Only requested columns are computed, skipping unnecessary `stat` field extraction.
- **Cross-platform**: Linux, macOS, and Windows.

## Building from source

```sh
make
```

## Running tests

```sh
make test
```
