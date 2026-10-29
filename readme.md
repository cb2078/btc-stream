btc-stream is a command-line application that shows the top bid and ask prices and quantities of the BTC/USD trading pair on the Gemini exchange.

It is written in C++ using the [Cinatra](https://github.com/qicosmos/cinatra) library for asynchronous WebSocket communication and the [simdjson](https://github.com/simdjson/simdjson) library for fast JSON parsing.

The order book is implemented using a map and hash-map; it provides constant-time lookup, update and removal of price-levels, as well as logarithmic-time insertion of new price-levels.

**Building**

[Conan](https://conan.io), CMake and Git are required to build the project.

To configure the project (this only needs to be run the first time):

```batch
git clone --recurse-submodules https://github.com/qicosmos/cinatra.git external\cinatra
conan install . --build=missing -c tools.build:jobs=8 -s compiler.cppstd=23 -s build_type=Release
cmake --preset conan-release
```

To compile:

```
cmake --build --preset conan-release
```

**Running**

To start the program, run its executable from the build directory.

On Mac / Linux:

```sh
./build/Release/btc-stream
```

On Windows:

```batch
.\build\Release\btc-stream.exe
```

To exit, use Ctrl-C.
