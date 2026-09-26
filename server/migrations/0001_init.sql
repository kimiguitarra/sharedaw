-- 同期サーバーの D1 スキーマ（仕様書 §6.3）。日時はすべて UTC の ISO 8601。
CREATE TABLE users (
  id TEXT PRIMARY KEY,
  display_name TEXT NOT NULL,
  token_hash TEXT NOT NULL UNIQUE,
  created_at TEXT NOT NULL
);

CREATE TABLE projects (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  head_revision INTEGER NOT NULL DEFAULT 0,
  created_by TEXT NOT NULL,
  created_at TEXT NOT NULL
);

CREATE TABLE project_members (
  project_id TEXT NOT NULL,
  user_id TEXT NOT NULL,
  PRIMARY KEY (project_id, user_id)
);

CREATE TABLE revisions (
  project_id TEXT NOT NULL,
  number INTEGER NOT NULL,
  parent_number INTEGER,
  author_id TEXT,
  message TEXT,
  project_json_hash TEXT NOT NULL,
  created_at TEXT NOT NULL,
  PRIMARY KEY (project_id, number)
);

CREATE TABLE locks (
  project_id TEXT NOT NULL,
  track_id TEXT NOT NULL,
  user_id TEXT NOT NULL,
  acquired_at TEXT,
  PRIMARY KEY (project_id, track_id)
);

CREATE TABLE lock_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id TEXT,
  track_id TEXT,
  user_id TEXT,
  action TEXT, -- acquire | release | force_release
  created_at TEXT
);

CREATE TABLE blobs (
  hash TEXT PRIMARY KEY,
  size INTEGER,
  created_at TEXT
);

CREATE INDEX idx_members_user ON project_members (user_id);
CREATE INDEX idx_lock_events_project ON lock_events (project_id, id);
