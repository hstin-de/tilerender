#ifndef COORDINATES_H
#define COORDINATES_H

#include <mbgl/util/geo.hpp>

namespace mbgl
{

    LatLng convertTilesToCoordinates(int x, int y, int zoom);

    LatLng calculateNormalizedCenterCoords(int x, int y, int zoom);

} // namespace mbgl

#endif // COORDINATES_H