CREATE TABLE IF NOT EXISTS rtc_rooms (code TEXT PRIMARY KEY, used INTEGER NOT NULL, version INTEGER NOT NULL, ip TEXT NOT NULL, host_key TEXT NOT NULL, join_key TEXT, host_name TEXT NOT NULL, join_name TEXT, offer TEXT, offer_round INTEGER, answer TEXT, answer_round INTEGER);
CREATE TABLE IF NOT EXISTS used (what TEXT NOT NULL, slot INTEGER NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (what, slot));
