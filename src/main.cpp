#include <mbgl/map/map.hpp>
#include <mbgl/map/map_options.hpp>
#include <mbgl/util/image.hpp>
#include <mbgl/util/run_loop.hpp>
#include <mbgl/util/logging.hpp>
#include <mbgl/gfx/headless_frontend.hpp>
#include <mbgl/style/style.hpp>
#include <iostream>
#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <sqlite3.h>

#include "image_encoding.hpp"
#include "mbtiles.hpp"
#include "coordinates.hpp"

void renderTiles(int processId, int numProcesses, int maxZoom, const char *style_url, ImageFormat imageFormat, const char *dbPath)
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

                LatLng center = calculateNormalizedCenterCoords(x, y, zoom);

                map.jumpTo(CameraOptions()
                               .withCenter(center)
                               .withZoom(zoom));

                auto image = frontend.render(map).image;
                std::string encodedData;

                switch (imageFormat)
                {
                case ImageFormat::WEBP:
                    encodedData = encodeWebP(image);
                    break;
                case ImageFormat::JPEG:
                    encodedData = encodeJPEG(image);
                    break;
                case ImageFormat::PNG:
                    encodedData = encodePNG(image);
                    break;
                default:
                    encodedData = encodeWebP(image);
                    break;
                }

                int tmsY = (1 << zoom) - 1 - y;

                rc = sqlite3_bind_int(stmt, 1, zoom);
                rc |= sqlite3_bind_int(stmt, 2, x);
                rc |= sqlite3_bind_int(stmt, 3, tmsY);
                rc |= sqlite3_bind_blob(stmt, 4, encodedData.data(), encodedData.size(), SQLITE_TRANSIENT);

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

int main(int argc, char *argv[])
{

    mbgl::Log::setObserver(std::make_unique<mbgl::Log::NullObserver>());

    std::string style_url;
    int maxZoom = 5;
    int numProcesses = std::thread::hardware_concurrency();
    std::string outputDbPath = "./tiles.mbtiles";
    ImageFormat imageFormat = ImageFormat::WEBP;

    // Command-line options parsing
    int opt;
    while ((opt = getopt(argc, argv, "s:z:p:o:f:")) != -1)
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
        case 'f':
            if (std::string(optarg) == "webp")
            {
                imageFormat = ImageFormat::WEBP;
            }
            else if (std::string(optarg) == "jpg")
            {
                imageFormat = ImageFormat::JPEG;
            }
            else if (std::string(optarg) == "png")
            {
                imageFormat = ImageFormat::PNG;
            }
            else
            {
                std::cerr << "Error: invalid image format specified, must be one of 'webp', 'jpg', or 'png'\n";
                return 1;
            }
            break;
        default:
            std::cerr << "Usage: " << argv[0] << " -s style_url [-z maxZoom] [-p numProcesses] [-o outputDbPath] [-f imageFormat]\n";
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
    std::cout << "Image Format: " << imageString(imageFormat) << std::endl;
    std::cout << "Output Database: " << outputDbPath << std::endl;
    std::cout << "===================================" << std::endl
              << std::endl;

    std::cout << ">>> Starting Rendering" << std::endl;

    std::vector<pid_t> pids;
    std::vector<std::string> dbPaths;

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int processId = 0; processId < numProcesses; ++processId)
    {
        std::string dbPath = "/tmp/output_" + std::to_string(processId) + ".mbtiles";

        pid_t pid = fork();
        if (pid == 0)
        {
            createTemporaryTileDatabase(dbPath.c_str());
            renderTiles(processId, numProcesses, maxZoom, style_url.c_str(), imageFormat, dbPath.c_str());
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

    createMBTilesDatabase(outputDbPath.c_str(), imageFormat);
    mergeMBTiles(dbPaths, outputDbPath.c_str());

    for (const auto &dbPath : dbPaths)
    {
        remove(dbPath.c_str());
    }

    return 0;
}
