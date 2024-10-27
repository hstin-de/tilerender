#include <iostream>
#include <string>
#include <cstdlib>
#include <sqlite3.h>

#include "image_encoding.hpp"

void createMBTilesDatabase(const char *dbPath, ImageFormat imageFormat)
{
    sqlite3 *db;
    int rc = sqlite3_open(dbPath, &db);
    if (rc)
    {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        exit(1);
    }

    const char *createTilesTableSQL = "CREATE TABLE IF NOT EXISTS tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB);";
    rc = sqlite3_exec(db, createTilesTableSQL, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "SQL error (create tiles table): " << sqlite3_errmsg(db) << std::endl;
    }

    const char *createMetadataTableSQL = "CREATE TABLE IF NOT EXISTS metadata (name TEXT, value TEXT);";
    rc = sqlite3_exec(db, createMetadataTableSQL, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "SQL error (create metadata table): " << sqlite3_errmsg(db) << std::endl;
    }

    const char *createTileIndexSQL = "CREATE UNIQUE INDEX IF NOT EXISTS tile_index ON tiles (zoom_level, tile_column, tile_row);";
    rc = sqlite3_exec(db, createTileIndexSQL, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "SQL error (create tile index): " << sqlite3_errmsg(db) << std::endl;
    }

    std::string metadataInsertSQL = "INSERT OR REPLACE INTO metadata (name, value) VALUES "
                                    "('name', 'raster'), "
                                    "('type', 'baselayer'), "
                                    "('version', '1.0'), "
                                    "('description', 'rendered vector tiles to " +
                                    imageString(imageFormat) + "'), "
                                                               "('format', '" +
                                    imageString(imageFormat) + "');";
    rc = sqlite3_exec(db, metadataInsertSQL.c_str(), nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "SQL error (insert metadata): " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_close(db);
}

void createTemporaryTileDatabase(const char *dbPath)
{
    sqlite3 *db;
    int rc = sqlite3_open(dbPath, &db);
    if (rc)
    {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        exit(1);
    }

    const char *createTilesTableSQL = "CREATE TABLE IF NOT EXISTS tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB);";
    rc = sqlite3_exec(db, createTilesTableSQL, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "SQL error (create tiles table): " << sqlite3_errmsg(db) << std::endl;
    }

    const char *createTileIndexSQL = "CREATE UNIQUE INDEX IF NOT EXISTS tile_index ON tiles (zoom_level, tile_column, tile_row);";
    rc = sqlite3_exec(db, createTileIndexSQL, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "SQL error (create tile index): " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_close(db);
}

void mergeMBTiles(const std::vector<std::string> &dbPaths, const char *outputDbPath)
{
    sqlite3 *outDb;
    int rc = sqlite3_open(outputDbPath, &outDb);
    if (rc)
    {
        std::cerr << "Can't open output database: " << sqlite3_errmsg(outDb) << std::endl;
        sqlite3_close(outDb);
        exit(1);
    }

    rc = sqlite3_exec(outDb, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Failed to begin transaction on output database: " << sqlite3_errmsg(outDb) << std::endl;
    }

    for (const auto &dbPath : dbPaths)
    {
        sqlite3 *inDb;
        rc = sqlite3_open(dbPath.c_str(), &inDb);
        if (rc)
        {
            std::cerr << "Can't open input database " << dbPath << ": " << sqlite3_errmsg(inDb) << std::endl;
            sqlite3_close(inDb);
            continue;
        }

        sqlite3_stmt *selectStmt;
        const char *selectSQL = "SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles;";
        rc = sqlite3_prepare_v2(inDb, selectSQL, -1, &selectStmt, nullptr);
        if (rc != SQLITE_OK)
        {
            std::cerr << "Failed to prepare select statement on " << dbPath << ": " << sqlite3_errmsg(inDb) << std::endl;
            sqlite3_close(inDb);
            continue;
        }

        const char *insertSQL = "INSERT OR IGNORE INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?, ?, ?, ?);";
        sqlite3_stmt *insertStmt;
        rc = sqlite3_prepare_v2(outDb, insertSQL, -1, &insertStmt, nullptr);
        if (rc != SQLITE_OK)
        {
            std::cerr << "Failed to prepare insert statement on output database: " << sqlite3_errmsg(outDb) << std::endl;
            sqlite3_finalize(selectStmt);
            sqlite3_close(inDb);
            continue;
        }

        while ((rc = sqlite3_step(selectStmt)) == SQLITE_ROW)
        {
            int zoom_level = sqlite3_column_int(selectStmt, 0);
            int tile_column = sqlite3_column_int(selectStmt, 1);
            int tile_row = sqlite3_column_int(selectStmt, 2);
            const void *tile_data = sqlite3_column_blob(selectStmt, 3);
            int tile_data_size = sqlite3_column_bytes(selectStmt, 3);

            sqlite3_bind_int(insertStmt, 1, zoom_level);
            sqlite3_bind_int(insertStmt, 2, tile_column);
            sqlite3_bind_int(insertStmt, 3, tile_row);
            sqlite3_bind_blob(insertStmt, 4, tile_data, tile_data_size, SQLITE_TRANSIENT);

            int insertRc = sqlite3_step(insertStmt);
            if (insertRc != SQLITE_DONE)
            {
                std::cerr << "Failed to insert tile into output database: " << sqlite3_errmsg(outDb) << std::endl;
            }

            sqlite3_reset(insertStmt);
            sqlite3_clear_bindings(insertStmt);
        }

        if (rc != SQLITE_DONE)
        {
            std::cerr << "Error while reading from " << dbPath << ": " << sqlite3_errmsg(inDb) << std::endl;
        }

        sqlite3_finalize(selectStmt);
        sqlite3_finalize(insertStmt);
        sqlite3_close(inDb);
    }

    rc = sqlite3_exec(outDb, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Failed to commit transaction on output database: " << sqlite3_errmsg(outDb) << std::endl;
    }

    sqlite3_close(outDb);
}