# PE Resource Loader

Allow you to load resources from Windows exe and dll files even outside of Windows. It automatically converts strings (PRL_TYPE_STRING), bitmaps (PRL_TYPE_BITMAP), icons (PRL_TYPE_ICON) and cursors (PRL_TYPE_CURSOR) into usable formats, other formats just give you the plain data as found in the binary.

Currently PE Resource Loader requires GCC and only works on little-endian systems.

## Using the PE Resource Loader Library

Here is a basic example of how the PE Resource Loader library can be used in a program to extract a specific string from a DLL file:

```c
#include <stdio.h>
#include <stdlib.h>

#include <pe_resource_loader.h>

int main(int argc, char ** argv) {
  // Load your dll or exe file from which you want to extract strings
  PeResourceLoader * loader = PeResourceLoader_Open("strings.dll");
  if (!loader) {
    printf("PE file failed to load\n");
    return 1;
  }

  // In this example we load string 107 in US English and we don't save the length
  char * string = (char *) PeResourceLoader_GetResource(loader, PRL_TYPE_STRING, PRL_LANG_EN_US, 107, NULL);
  if (!string) {
    printf("Could not find string 107\n");
    return 2;
  }
  printf("%s\n", string);

  // Don't forget to free strings that are no longer used
  free(string);

  // Always close the loader
  PeResourceLoader_Close(loader);

  return 0; 
}
```

Make sure to link your code to `pe_resource_loader`. The code for the bundled pe_string_loader and pe_bitmap_loader programs contain another more complex examples.

## Using PE String Loader Program

The bundled `pe_string_loader` program is mostly there to serve as an example of how the PE Resource Loader library can be used, but it does allow getting strings from binaries and DLLs without any additional code. Here is a basic example:

```sh
./pe_string_loader my-file.dll
```

It will output a list of string for each included language. If the file contains no strings, nothing will be printed.

## Using PE Bitmap Loader Program

The bundled `pe_bitmap_loader` program is mostly there to serve as an example of how the PE Resource Loader library can be used, but it does allow getting bitmaps from binaries and DLLs without any additional code. Here is a basic example:

```sh
./pe_bitmap_loader my-file.dll
```

It will output a list of bmp files found for each included language. It will create `.bmp` files in the current working directory if it finds any. If the file contains no bitmaps, only the languages found in the binary will be printed.

## Building

Building the code can be done simply by running the following commands:

```sh
git clone https://github.com/sharkwouter/pe-resource-loader.git
cd pe-resource-loader
mkdir build
cd build
cmake ..
make
```

Installing can be done with the following command after running the ones above:

```sh
sudo make install
```

No dependencies are needed other than a C compiler. This code will only work on little endian systems for now.

Building without building the `pe_string_loader` program can be done by running the cmake command with `-DPE_STRING_LOADER=OFF`. For building without `pe_bitmap_loader` use `-DPE_BITMAP_LOADER=OFF`.

## Licensing

This project is released under the [zlib license](LICENSE.txt), which requires the license to be included in source distributions, but allows for shipping binaries without any requirements.

For conversion from utf16 to utf-8 tm_unicode.h is used from the  [tm repo](https://github.com/to-miz/tm/), which is licensed under the unlicense.
