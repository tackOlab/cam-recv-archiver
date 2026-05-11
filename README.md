# cam-recv-archiver

A receiver for the HTJ2K-based intelligent camera and archiver for a local MongoDB.

The receiver listens on TCP `:4001` for length-prefixed HTJ2K codestreams from the camera, wraps each frame in a JPH container, writes it to disk as `<timestamp>.jph`, and inserts a `{fname, timestamp}` document into MongoDB (`test.item1` on `mongodb://localhost:27017/`).

## Environment setup

Reference environment: **Ubuntu 24.04 LTS**, g++ 13, CMake ≥ 3.15.

### 1. System packages (apt)

```sh
sudo apt install build-essential cmake pkg-config \
                 libssl-dev libsasl2-dev libmongocrypt-dev
```

### 2. MongoDB server

Install from MongoDB's official apt repository (Ubuntu's own `mongodb` package is unmaintained); follow https://www.mongodb.com/docs/manual/tutorial/install-mongodb-on-ubuntu/. Then:

```sh
sudo systemctl enable --now mongod
```

The receiver expects `mongod` reachable at `mongodb://localhost:27017/`.

### 3. mongo-c-driver (libmongoc / libbson)

`mongocxx` 4.x requires `libmongoc` ≥ 1.28; Ubuntu's `libmongoc-dev` package is too old, so build from source. Tested with **1.29.0**.

```sh
git clone --depth 1 --branch 1.29.0 https://github.com/mongodb/mongo-c-driver.git
cd mongo-c-driver
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF
cmake --build build -j
sudo cmake --install build
sudo ldconfig
```

Installs to `/usr/local` by default.

### 4. mongo-cxx-driver (mongocxx / bsoncxx)

This is what provides the `mongo::mongocxx_shared` and `mongo::bsoncxx_shared` imported targets the `CMakeLists.txt` links against. Tested with **4.0.0**.

```sh
git clone --depth 1 --branch r4.0.0 https://github.com/mongodb/mongo-cxx-driver.git
cd mongo-cxx-driver
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBSONCXX_POLY_USE_IMPLS=ON
cmake --build build -j
sudo cmake --install build
sudo ldconfig
```

After this, `find_package(mongocxx CONFIG REQUIRED)` will resolve via `/usr/local/lib/cmake/mongocxx-4.0.0/`.

## Build & run

```sh
cmake -B build
cmake --build build -j
./build/recv
```

If the driver was installed to a non-standard prefix, pass `-DCMAKE_PREFIX_PATH=/your/prefix` to the configure step. If the loader can't find `libmongocxx.so` at runtime, prepend `LD_LIBRARY_PATH=/usr/local/lib`.

The process writes `.jph` files into its current working directory.
