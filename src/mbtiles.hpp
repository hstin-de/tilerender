#ifndef MBTILES_HPP
#define MBTILES_HPP

#include <string>
#include "image_encoding.hpp"

void mergeMBTiles(const std::vector<std::string> &dbPaths, const char *outputDbPath);

void createMBTilesDatabase(const char *dbPath, ImageFormat imageFormat);

void createTemporaryTileDatabase(const char *dbPath);

#endif // MBTILES_HPP
