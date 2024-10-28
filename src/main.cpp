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
#include <getopt.h>
#include <filesystem>

#include "image_encoding.hpp"
#include "mbtiles.hpp"
#include "coordinates.hpp"

namespace fs = std::filesystem;

void renderTiles(int processId, int numProcesses, int maxZoom, const char *style_url, ImageFormat imageFormat, const char *dbPath)
{
    double pixelRatio = 1.0;
    uint32_t width = 512;
    uint32_t height = 512;

    using namespace mbgl;

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
            .withApiKey(""));

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

void printHelp(const char *programName)
{
    std::cout << "Usage: " << programName << " [options]\n\n"
              << "Options:\n"
              << "  -s, --style <style_url>         URL of the style to use, can be a local file or a remote URL (required!)\n"
              << "  -z, --zoom <maxZoom>            Maximum zoom level (integer)\n"
              << "  -p, --processes <numProcesses>  Number of parallel processes (integer)\n"
              << "  -o, --output <outputDbPath>     Path to the output database\n"
              << "  -f, --format <imageFormat>      Image format: 'webp', 'jpg', or 'png'\n"
              << "  -h, --help                      Display this help message\n\n"
              << "Example:\n"
              << "  " << programName << " -s https://demotiles.maplibre.org/style.json -z 6 -p 24 -o demotiles.mbtiles -f webp\n";
}

int main(int argc, char *argv[])
{

    mbgl::Log::setObserver(std::make_unique<mbgl::Log::NullObserver>());

    std::string style_url;
    int maxZoom = 5;
    int numProcesses = std::thread::hardware_concurrency();
    if (numProcesses == 0) numProcesses = 1; // fallback to 1 process if hardware_concurrency() returns 0
    std::string outputPath = "./tiles.mbtiles";
    ImageFormat imageFormat = ImageFormat::WEBP;

    // Command-line options parsing
    static struct option long_options[] = {
        {"style", required_argument, nullptr, 's'},
        {"zoom", required_argument, nullptr, 'z'},
        {"processes", required_argument, nullptr, 'p'},
        {"output", required_argument, nullptr, 'o'},
        {"format", required_argument, nullptr, 'f'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}};

    int opt;
    // Parse command-line options
    while ((opt = getopt_long(argc, argv, "s:z:p:o:f:h", long_options, nullptr)) != -1)
    {
        switch (opt)
        {
        case 's':
            style_url = optarg;
            break;
        case 'z':
            try
            {
                maxZoom = std::stoi(optarg);
                if (maxZoom < 0 || maxZoom > 22)
                {
                    throw std::out_of_range("Zoom level must be between 0 and 22.");
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error: Invalid zoom level. " << e.what() << "\n";
                return EXIT_FAILURE;
            }
            break;
        case 'p':
            try
            {
                int temp = std::stoi(optarg);
                if (temp > 0)
                {
                    numProcesses = static_cast<unsigned int>(temp);
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error: Invalid number of processes. " << e.what() << "\n";
                return EXIT_FAILURE;
            }
            break;
        case 'o':
            outputPath = optarg;
            break;
        case 'f':
        {
            std::string formatStr = optarg;
            // Convert to lowercase for case-insensitive comparison
            std::transform(formatStr.begin(), formatStr.end(), formatStr.begin(),
                           [](unsigned char c)
                           { return std::tolower(c); });

            if (formatStr == "webp")
            {
                imageFormat = ImageFormat::WEBP;
            }
            else if (formatStr == "jpg" || formatStr == "jpeg")
            {
                imageFormat = ImageFormat::JPEG;
            }
            else if (formatStr == "png")
            {
                imageFormat = ImageFormat::PNG;
            }
            else
            {
                std::cerr << "Error: Invalid image format specified. Choose 'webp', 'jpg', or 'png'.\n";
                return EXIT_FAILURE;
            }
            break;
        }
        case 'h':
            printHelp(argv[0]);
            return EXIT_SUCCESS;
        default:
            printHelp(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (style_url.empty())
    {
        std::cerr << "Error: Style URL is required.\n\n";
        printHelp(argv[0]);
        return EXIT_FAILURE;
    }

    if (fs::exists(outputPath))
    {
        std::cerr << "Error: Output file '" << outputPath << "' already exists.\nPlease choose a different output path or remove the existing file before proceeding.\n\n";
        printHelp(argv[0]);
        return EXIT_FAILURE;
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
    std::cout << "Output Path: " << outputPath << std::endl;
    std::cout << "===================================" << std::endl
              << std::endl;

    std::cout << ">>> Starting Rendering..." << std::endl;

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

    createMBTilesDatabase(outputPath.c_str(), imageFormat);
    mergeMBTiles(dbPaths, outputPath.c_str());

    for (const auto &dbPath : dbPaths)
    {
        remove(dbPath.c_str());
    }

    return 0;
}
