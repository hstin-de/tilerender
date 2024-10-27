#include <mbgl/util/geo.hpp>
#include <cmath>
#include <iostream>

namespace mbgl
{

    LatLng convertTilesToCoordinates(int x, int y, int zoom)
    {
        double n = std::pow(2.0, zoom);
        double lon = (static_cast<double>(x) / n) * 360.0 - 180.0;
        double latRad = std::atan(std::sinh(M_PI * (1.0 - (2.0 * static_cast<double>(y)) / n)));
        double lat = latRad * 180.0 / M_PI;
        return LatLng{lat, lon, mbgl::LatLng::Unwrapped};
    }

    LatLng calculateNormalizedCenterCoords(int x, int y, int zoom)
    {
        LatLng nw = convertTilesToCoordinates(x, y, zoom);
        LatLng se = convertTilesToCoordinates(x + 1, y + 1, zoom);

        double mercatorNwY = std::log(std::tan(M_PI / 4.0 + (nw.latitude() * M_PI) / 360.0));
        double mercatorSeY = std::log(std::tan(M_PI / 4.0 + (se.latitude() * M_PI) / 360.0));
        double avgMercatorY = (mercatorNwY + mercatorSeY) / 2.0;
        double centerLat = (std::atan(std::exp(avgMercatorY)) * 360.0) / M_PI - 90.0;

        double centerLon = (nw.longitude() + se.longitude()) / 2.0;

        return LatLng{centerLat, centerLon, mbgl::LatLng::Wrapped};
    }

} // namespace mbgl