#include "common.h"
#include "misc.h"
#include "dbsql.h"

int db_open(int createifnotfound)
{
	int rc, createdb = 0;
	char dbfilename[512];
	struct stat filestat;

	snprintf(dbfilename, 512, "%s/%s", cfg.dbdir, DATABASEFILE);

	/* create database if file doesn't exist */
	if (stat(dbfilename, &filestat) != 0) {
		if (errno == ENOENT && createifnotfound) {
			createdb = 1;
		} else {
			if (debug)
				printf("Error: Handling database \"%s\" failed: %s\n", dbfilename, strerror(errno));
			return 0;
		}
	} else {
		if (filestat.st_size == 0) {
			if (createifnotfound) {
				createdb = 1;
			} else {
				printf("Error: Database \"%s\" contains 0 bytes and isn't a valid database, exiting.\n", dbfilename);
				exit(EXIT_FAILURE);
			}
		}
	}

	rc = sqlite3_open(dbfilename, &db);

	if (rc) {
		if (debug)
			printf("Error: Can't open database \"%s\": %s\n", dbfilename, sqlite3_errmsg(db));
		return 0;
	} else {
		if (debug)
			printf("Database \"%s\" open\n", dbfilename);
	}

	if (createdb) {
		if (!spacecheck(cfg.dbdir)) {
			printf("Error: Not enough free diskspace available in \"%s\", exiting.\n", cfg.dbdir);
			exit(EXIT_FAILURE);
		}
		if (!db_create()) {
			if (debug)
				printf("Error: Creating database \"%s\" structure failed\n", dbfilename);
			return 0;
		} else {
			if (debug)
				printf("Database \"%s\" structure created\n", dbfilename);
			if (!db_setinfo("dbversion", SQLDBVERSION, 1)) {
				if (debug)
					printf("Error: Writing version info to database \"%s\" failed\n", dbfilename);
				return 0;
			}
		}
	}

	if (createifnotfound) {
		if (!db_setinfo("vnstatversion", VNSTATVERSION, 1)) {
			return 0;
		}
	}

	return 1;
}

int db_exec(char *sql)
{
	int rc;
	sqlite3_stmt *sqlstmt;

	rc = sqlite3_prepare_v2(db, sql, -1, &sqlstmt, NULL);
	if (rc) {
		if (debug)
			printf("Error: Insert \"%s\" prepare failed (%d): %s\n", sql, rc, sqlite3_errmsg(db));
		return 0;
	}

	rc = sqlite3_step(sqlstmt);
	if (rc != SQLITE_DONE) {
		if (debug)
			printf("Error: Insert \"%s\" step failed (%d): %s\n", sql, rc, sqlite3_errmsg(db));
		return 0;
	}

	rc = sqlite3_finalize(sqlstmt);
	if (rc) {
		if (debug)
			printf("Error: Finalize \"%s\" failed (%d): %s\n", sql, rc, sqlite3_errmsg(db));
		return 0;
	}

	return 1;
}

int db_create(void)
{
	int rc, i;
	char *sql;
	char *datatables[] = {"fiveminute", "hour", "day", "month", "year"};

	/* TODO: check: COMMIT or END may be missing in error cases and return gets called before COMMIT */

	rc = sqlite3_exec(db, "BEGIN", 0, 0, 0);
	if (rc != 0) {
		return 0;
	}

	sql = "CREATE TABLE info(\n" \
		"  id       INTEGER PRIMARY KEY,\n" \
		"  name     TEXT UNIQUE NOT NULL,\n" \
		"  value    TEXT NOT NULL);";

	if (!db_exec(sql)) {
		return 0;
	}

	sql = "CREATE TABLE interface(\n" \
		"  id           INTEGER PRIMARY KEY,\n" \
		"  name         TEXT UNIQUE NOT NULL,\n" \
		"  alias        TEXT,\n" \
		"  active       INTEGER NOT NULL,\n" \
		"  created      DATE NOT NULL,\n" \
		"  updated      DATE NOT NULL,\n" \
		"  rxcounter    INTEGER NOT NULL,\n" \
		"  txcounter    INTEGER NOT NULL,\n" \
		"  rxtotal      INTEGER NOT NULL,\n" \
		"  txtotal      INTEGER NOT NULL);";

	if (!db_exec(sql)) {
		return 0;
	}

	for (i=0; i<5; i++) {
		sql = malloc(sizeof(char)*512);

		snprintf(sql, 512, "CREATE TABLE %s(\n" \
			"  id           INTEGER PRIMARY KEY,\n" \
			"  interface    INTEGER REFERENCES interface ON DELETE CASCADE,\n" \
			"  date         DATE NOT NULL,\n" \
			"  rx           INTEGER NOT NULL,\n" \
			"  tx           INTEGER NOT NULL,\n" \
			"  CONSTRAINT u UNIQUE (interface, date));", datatables[i]);

		if (!db_exec(sql)) {
			free(sql);
			return 0;
		}
	}

	rc = sqlite3_exec(db, "COMMIT", 0, 0, 0);
	if (rc != 0) {
		return 0;
	}

	return 1;
}

int db_addinterface(char *iface)
{
	char sql[1024];

	snprintf(sql, 1024, "insert into interface (name, active, created, updated, rxcounter, txcounter, rxtotal, txtotal) values ('%s', 1, datetime('now'), datetime('now'), 0, 0, 0, 0);", iface);
	return db_exec(sql);
}

sqlite3_int64 db_getinterfaceid(char *iface, int createifnotfound)
{
	int rc;
	char sql[512];
	sqlite3_int64 ifaceid = 0;
	sqlite3_stmt *sqlstmt;

	snprintf(sql, 512, "select id from interface where name='%s'", iface);
	rc = sqlite3_prepare_v2(db, sql, -1, &sqlstmt, NULL);
	if (!rc) {
		if (sqlite3_step(sqlstmt) == SQLITE_ROW) {
			ifaceid = sqlite3_column_int64(sqlstmt, 0);
		}
		sqlite3_finalize(sqlstmt);
	}

	if (ifaceid == 0 && createifnotfound) {
		if (!db_addinterface(iface)) {
			return 0;
		}
		ifaceid = sqlite3_last_insert_rowid(db);
	}

	return ifaceid;
}

int db_setactive(char *iface, int active)
{
	char sql[512];
	sqlite3_int64 ifaceid = 0;

	ifaceid = db_getinterfaceid(iface, 0);
	if (ifaceid == 0) {
		return 0;
	}

	snprintf(sql, 512, "update interface set active=%d where id=%"PRId64";", active, (int64_t)ifaceid);
	return db_exec(sql);
}

int db_setalias(char *iface, char *alias)
{
	char sql[512];
	sqlite3_int64 ifaceid = 0;

	ifaceid = db_getinterfaceid(iface, 0);
	if (ifaceid == 0) {
		return 0;
	}

	snprintf(sql, 512, "update interface set nick='%s' where id=%"PRId64";", alias, (int64_t)ifaceid);
	return db_exec(sql);
}

int db_setinfo(char *name, char *value, int createifnotfound)
{
	int rc;
	char sql[512];

	snprintf(sql, 512, "update info set value='%s' where name='%s';", value, name);
	rc = db_exec(sql);
	if (!rc) {
		return rc;
	}
	if (!sqlite3_changes(db) && createifnotfound) {
		snprintf(sql, 512, "insert into info (name, value) values ('%s', '%s');", name, value);
		rc = db_exec(sql);
	}
	return rc;
}

int db_addtraffic(char *iface, uint64_t rx, uint64_t tx)
{
	int i;
	char sql[1024];
	sqlite3_int64 ifaceid = 0;

	char *datatables[] = {"fiveminute", "hour", "day", "month", "year"};
	char *datadates[] = {"datetime('now', ('-' || (strftime('%M', 'now')) || ' minutes'), ('-' || (strftime('%S', 'now')) || ' seconds'), ('+' || (round(strftime('%M', 'now')/5,0)*5) || ' minutes'))", \
			"strftime('%Y-%m-%d %H:00:00', 'now')", \
			"date('now')", "strftime('%Y-%m-01', 'now')", \
			"strftime('%Y-01-01', 'now')"};

	if (rx == 0 && tx == 0) {
		return 1;
	}

	ifaceid = db_getinterfaceid(iface, 1);
	if (ifaceid == 0) {
		return 0;
	}

	if (debug)
		printf("add %s (%"PRId64"): rx %"PRIu64" - tx %"PRIu64"\n", iface, (int64_t)ifaceid, rx, tx);

	sqlite3_exec(db, "BEGIN", 0, 0, 0);

	/* total */
	snprintf(sql, 1024, "update interface set rxtotal=rxtotal+%"PRIu64", txtotal=txtotal+%"PRIu64", updated=datetime('now'), active=1 where id=%"PRId64";", rx, tx, (int64_t)ifaceid);
	db_exec(sql);

	/* time specific */
	for (i=0; i<5; i++) {
		snprintf(sql, 1024, "insert or ignore into %s (interface, date, rx, tx) values (%"PRId64", %s, 0, 0);", datatables[i], (int64_t)ifaceid, datadates[i]);
		db_exec(sql);
		snprintf(sql, 1024, "update %s set rx=rx+%"PRIu64", tx=tx+%"PRIu64" where interface=%"PRId64" and date=%s;", datatables[i], rx, tx, (int64_t)ifaceid, datadates[i]);
		db_exec(sql);
	}

	sqlite3_exec(db, "COMMIT", 0, 0, 0);

	return 1;
}
