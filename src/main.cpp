#include <mbgl/map/map.hpp>
#include <mbgl/map/map_options.hpp>
#include <mbgl/util/image.hpp>
#include <mbgl/util/run_loop.hpp>
#include <mbgl/util/logging.hpp>
#include <mbgl/util/image.hpp>
#include <mbgl/util/premultiply.hpp>
#include <mbgl/gfx/backend.hpp>
#include <mbgl/gfx/headless_frontend.hpp>
#include <mbgl/style/style.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <memory>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <sqlite3.h>
#include <dirent.h>
#include <webp/encode.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace mbgl
{

    std::string encodeWebP(const PremultipliedImage &pre)
    {
        const auto src = util::unpremultiply(pre.clone());

        uint8_t *output_data = nullptr;
        size_t output_size = WebPEncodeRGBA(src.data.get(), src.size.width, src.size.height, src.stride(), 75.0f, &output_data);

        if (output_size == 0 || output_data == nullptr)
        {
            throw std::runtime_error("WebP encoding failed");
        }

        std::string webpData(reinterpret_cast<char *>(output_data), output_size);
        WebPFree(output_data);

        return webpData;
    }

} // namespace mbgl

// Utility function for string formatting
template <typename... Args>
std::string string_format(const std::string &format, Args... args)
{
    int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
    if (size_s <= 0)
    {
        throw std::runtime_error("Error during formatting.");
    }
    auto size = static_cast<size_t>(size_s);
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(), buf.get() + size - 1);
}

struct Coordinates
{
    double lon;
    double lat;
};

Coordinates convertTilesToCoordinates(int x, int y, int zoom)
{
    double n = std::pow(2.0, zoom);
    double lon = (static_cast<double>(x) / n) * 360.0 - 180.0;
    double latRad = std::atan(std::sinh(M_PI * (1.0 - (2.0 * static_cast<double>(y)) / n)));
    double lat = latRad * 180.0 / M_PI;
    return {lon, lat};
}

Coordinates calculateNormalizedCenterCoords(int x, int y, int zoom)
{
    Coordinates nw = convertTilesToCoordinates(x, y, zoom);
    Coordinates se = convertTilesToCoordinates(x + 1, y + 1, zoom);

    double mercatorNwY = std::log(std::tan(M_PI / 4.0 + (nw.lat * M_PI) / 360.0));
    double mercatorSeY = std::log(std::tan(M_PI / 4.0 + (se.lat * M_PI) / 360.0));
    double avgMercatorY = (mercatorNwY + mercatorSeY) / 2.0;
    double centerLat = (std::atan(std::exp(avgMercatorY)) * 360.0) / M_PI - 90.0;

    double centerLon = (nw.lon + se.lon) / 2.0;

    return {centerLon, centerLat};
}

void initializeDatabase(const char *dbPath)
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

    const char *metadataInsertSQL = "INSERT OR REPLACE INTO metadata (name, value) VALUES "
                                    "('name', 'raster'), "
                                    "('type', 'baselayer'), "
                                    "('version', '1.0'), "
                                    "('description', 'rendered vector tiles to webp'), "
                                    "('format', 'webp');";
    rc = sqlite3_exec(db, metadataInsertSQL, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "SQL error (insert metadata): " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_close(db);
}

void renderTiles(int processId, int numProcesses, int maxZoom, const char *style_url, const char *dbPath)
{
    double pixelRatio = 1.0;
    uint32_t width = 512;
    uint32_t height = 512;

    using namespace mbgl;

    auto mapTilerConfiguration = TileServerOptions::MapTilerConfiguration();

    util::RunLoop loop;

    HeadlessFrontend frontend({width, height}, static_cast<float>(pixelRatio));
    Map map(
        frontend,
        MapObserver::nullObserver(),
        MapOptions()
            .withMapMode(MapMode::Tile)
            .withSize(frontend.getSize())
            .withPixelRatio(static_cast<float>(pixelRatio)),
        ResourceOptions()
            .withCachePath("")
            .withMaximumCacheSize(0)
            .withAssetPath("")
            .withApiKey("")
            .withTileServerOptions(mapTilerConfiguration));

    map.getStyle().loadURL(style_url);

    sqlite3 *db;
    int rc = sqlite3_open(dbPath, &db);
    if (rc)
    {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        exit(1);
    }

    const char *insertSQL = "INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        exit(1);
    }

    for (int zoom = 0; zoom <= maxZoom; zoom++)
    {
        int numOfTiles = 1 << zoom;

        rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK)
        {
            std::cerr << "Failed to begin transaction: " << sqlite3_errmsg(db) << std::endl;
        }

        for (int x = 0; x < numOfTiles; x++)
        {
            for (int y = processId; y < numOfTiles; y += numProcesses)
            {

                Coordinates coords = calculateNormalizedCenterCoords(x, y, zoom);
                map.jumpTo(CameraOptions()
                               .withCenter(LatLng{coords.lat, coords.lon})
                               .withZoom(zoom));

                auto image = frontend.render(map).image;
                auto webpData = encodeWebP(image);

                int tmsY = (1 << zoom) - 1 - y;

                rc = sqlite3_bind_int(stmt, 1, zoom);
                rc |= sqlite3_bind_int(stmt, 2, x);
                rc |= sqlite3_bind_int(stmt, 3, tmsY);
                rc |= sqlite3_bind_blob(stmt, 4, webpData.data(), webpData.size(), SQLITE_TRANSIENT);

                if (rc != SQLITE_OK)
                {
                    std::cerr << "Failed to bind parameters: " << sqlite3_errmsg(db) << std::endl;
                }

                rc = sqlite3_step(stmt);
                if (rc != SQLITE_DONE)
                {
                    std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db) << std::endl;
                }

                sqlite3_reset(stmt);
                sqlite3_clear_bindings(stmt);
            }
        }

        rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK)
        {
            std::cerr << "Failed to commit transaction: " << sqlite3_errmsg(db) << std::endl;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void mergeDatabases(const std::vector<std::string> &dbPaths, const char *outputDbPath)
{
    sqlite3 *outDb;
    int rc = sqlite3_open(outputDbPath, &outDb);
    if (rc)
    {
        std::cerr << "Can't open output database: " << sqlite3_errmsg(outDb) << std::endl;
        sqlite3_close(outDb);
        exit(1);
    }

    initializeDatabase(outputDbPath);

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

int main(int argc, char *argv[])
{

    mbgl::Log::setObserver(std::make_unique<mbgl::Log::NullObserver>());

    std::string style_url;
    int maxZoom = 5;
    int numProcesses = std::thread::hardware_concurrency();
    std::string outputDbPath = "./tiles.mbtiles";

    // Command-line options parsing
    int opt;
    while ((opt = getopt(argc, argv, "s:z:p:o:")) != -1)
    {
        switch (opt)
        {
        case 's':
            style_url = optarg;
            break;
        case 'z':
            maxZoom = std::stoi(optarg);
            break;
        case 'p':
            numProcesses = std::stoi(optarg);
            break;
        case 'o':
            outputDbPath = optarg;
            break;
        default:
            std::cerr << "Usage: " << argv[0] << " -s style_url [-z maxZoom] [-p numProcesses] [-o outputDbPath]\n";
            return 1;
        }
    }

    if (style_url.empty())
    {
        std::cerr << "Error: style URL must be specified with -s\n";
        return 1;
    }

    if (style_url.find("http://") != 0 && style_url.find("https://") != 0 && style_url.find("file://") != 0)
    {
        style_url = "file://" + style_url;
    }

        
    std::cout << "===================================" << std::endl;
    std::cout << "Style URL: " << style_url << std::endl;
    std::cout << "Max Zoom: " << maxZoom << std::endl;
    std::cout << "Number of Processes: " << numProcesses << std::endl;
    std::cout << "Output Database: " << outputDbPath << std::endl;
    std::cout << "===================================" << std::endl << std::endl;

    
    std::cout << ">>> Starting Rendering" << std::endl;

    fs::path outputPath = fs::path(outputDbPath).parent_path();
    std::vector<pid_t> pids;
    std::vector<std::string> dbPaths;

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int processId = 0; processId < numProcesses; ++processId)
    {
        fs::path dbPath = outputPath / ("output_" + std::to_string(processId) + ".mbtiles");

        pid_t pid = fork();
        if (pid == 0)
        {
            initializeDatabase(dbPath.c_str());
            renderTiles(processId, numProcesses, maxZoom, style_url.c_str(), dbPath.c_str());
            exit(0);
        }
        else if (pid > 0)
        {
            pids.push_back(pid);
            dbPaths.push_back(dbPath);
        }
        else
        {
            std::cerr << "Failed to fork process" << std::endl;
            return 1;
        }
    }

    // Wait
    for (pid_t pid : pids)
    {
        int status;
        waitpid(pid, &status, 0);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedTime = endTime - startTime;
    std::cout << ">>> Finished Rendering in " << elapsedTime.count() << " seconds." << std::endl;

    mergeDatabases(dbPaths, outputDbPath.c_str());

    for (const auto &dbPath : dbPaths)
    {
        remove(dbPath.c_str());
    }

    return 0;
}
