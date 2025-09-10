# libdivecomputer - dsf2csv Fork

This is a fork of `libdivecomputer` focused on providing a command-line tool to convert Divesoft Freedom dive logs into a plain text CSV format.

The primary tool included is `dsf2csv`.

## Build Status & Important Note on `.DLF` files

This version contains a patched version of the Divesoft Freedom parser. During development, it was discovered that `.DLF` files from the Divesoft Freedom Plus model use a different file structure than the `.dsf` files the parser was originally designed for.

A patch has been applied to `src/divesoft_freedom_parser.c` to handle this `.DLF` variant. Specifically, it forces the parser to use a 32-byte header and V1 format logic even though the file has a V2 signature. This allows the header to be parsed with more accuracy.

**However, a problem remains:** The format of the dive profile's sample data is still not compatible with the parser. This results in a "Timestamp moved backwards" error, and the final CSV file will be empty except for the header row.

To fully support this file format, the 16-byte sample record structure would need to be reverse-engineered and the parser updated accordingly. This task is beyond the scope of the current effort.

The tool as-is provides a working build system and a solid foundation for parsing Divesoft files, but it will require further development to support this specific `.DLF` file variant.

## Building the Tool

The project uses standard autotools. To build the `dsf2csv` executable, follow these steps:

1.  **Install dependencies:** You will need `build-essential` (for `make`, `gcc`, etc.) and `autoconf`, `automake`, `libtool`. On a Debian/Ubuntu system, you can install them with:
    ```sh
    sudo apt-get update
    sudo apt-get install build-essential autoconf automake libtool
    ```

2.  **Configure the project:** Run the `autoreconf` and `configure` scripts to prepare the build environment.
    ```sh
    autoreconf --install
    ./configure
    ```

3.  **Compile:** Run `make` to build the `libdivecomputer` library and the `dsf2csv` tool.
    ```sh
    make
    ```
    The final executable will be located at `tools/dsf2csv`.

## Using dsf2csv

The tool is straightforward to use. It takes a single argument: the path to the input file. It will create a new file with the same name, but with the `.csv` extension, in the same directory.

**Syntax:**
```sh
./tools/dsf2csv <input_file>
```

**Example:**
```sh
./tools/dsf2csv my_dive_log.DLF
```
This will produce `my_dive_log.csv`. Due to the issue mentioned above, this file will likely only contain the header.
