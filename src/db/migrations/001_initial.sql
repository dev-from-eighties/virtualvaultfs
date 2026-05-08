CREATE TABLE nodes (
  id INTEGER PRIMARY KEY,
  parent_id INTEGER NOT NULL,
  name TEXT NOT NULL,
  type TEXT NOT NULL, -- file, dir, symlink
  object_id INTEGER,
  mode INTEGER,
  uid INTEGER,
  gid INTEGER,
  size INTEGER,
  mtime_ns INTEGER,
  ctime_ns INTEGER,
  UNIQUE(parent_id, name)
);

CREATE TABLE objects (
  id INTEGER PRIMARY KEY,
  relpath TEXT NOT NULL UNIQUE,
  size INTEGER,
  hash TEXT
);
