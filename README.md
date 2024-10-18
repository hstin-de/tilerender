# tilerender

**tilerender** is a tool for prerendering vector tiles into raster images efficiently and effortlessly.

## Features

- **Simple Integration:** Utilize your existing vector tile styles to generate raster tiles without any additional configuration.
- **High Performance:** Leverages multiple CPU cores to accelerate the rendering process.
- **Configurable Zoom Levels:** Set the maximum zoom level to render only the tiles you need, optimizing resource usage.
- **Flexible Output:** Save rendered tiles in the widely-supported MBTiles format for easy integration with various platforms.
- **Docker Support:** Streamline setup and execution using Docker, ensuring consistency across different environments.

## Requirements
- **Docker:** Recommended for easy setup and execution. [Install Docker](https://docs.docker.com/get-docker/)
- **For Native Execution:** An X-Server is required. You can use **Xvfb** (X Virtual Framebuffer) to create a virtual display.

## Installation / Usage

Execute tilerender using Docker with the following command structure:

```bash
docker run --rm -it -v "$PWD:/data/" ghcr.io/hstin-de/tilerender -s <style_url> [options]
```

### Parameters

- `-s` **(required):**  
  URL to the style JSON used for rendering.

- `-z` **(optional):**  
  Maximum zoom level for rendering.  
  **Default:** `5`

- `-p` **(optional):**  
  Number of parallel processes to use.  
  **Default:** All available CPU cores

- `-o` **(optional):**  
  Path for the output MBTiles where rendered tiles will be stored.  
  **Default:** `./tiles.mbtiles`

### Example

```bash
docker run --rm -it -v "$PWD:/data/" ghcr.io/hstin-de/tilerender \
  -s https://demotiles.maplibre.org/style.json \
  -z 6 \
  -p 24 \
  -o demotiles.mbtiles
```

This command generates WebP tiles from the vector tiles found [here](https://demotiles.maplibre.org/) up to zoom level 6 using 24 threads.

### Running Natively

If you prefer to run tilerender without Docker, follow these steps:

1. **Clone the Repository:**

   ```bash
   git clone https://github.com/hstin-de/tilerender.git
   cd tilerender
   ```

2. **Install Dependencies:**

   Follow the [MapLibre Native Linux instructions](https://github.com/maplibre/maplibre-native/tree/main/platform/linux) to install the necessary libraries and dependencies.

3. **Set Up an X-Server:**

   tilerender requires an X-Server to run. You can use **Xvfb** (X Virtual Framebuffer) to create a virtual display:

   ```bash
   sudo apt-get install xvfb
   Xvfb :99 -screen 0 1024x768x24 &
   export DISPLAY=:99
   ```

4. **Build & Run tilerender:**

   Compile tilerender using CMake and then execute it with the desired parameters:

   ```bash
   cmake -B build -GNinja \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DMLN_WITH_CLANG_TIDY=OFF \
    -DMLN_WITH_COVERAGE=OFF \
    -DMLN_DRAWABLE_RENDERER=ON \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
    
    
   cd build && ninja -j $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null)
   
   ./bin/tilerender -s <style_url> [options]
   ```

## Contributing

Contributions are welcome! Please [open an issue](https://github.com/hstin-de/tilerender/issues) or submit a pull request for any improvements or bug fixes. :)

## License

Licensed under the [MIT License](LICENSE).
