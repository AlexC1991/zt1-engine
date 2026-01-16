#include <stdio.h>
#include <stdlib.h>

#include <pe_resource_loader.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char ** argv) {
  if (argc < 2) {
    printf("No file was defined\n");
    return 1;
  }

  int return_value = 0;
  for (int i = 1; i < argc; i++) {
    printf("Bitmaps from file %s:\n", argv[i]);
    PeResourceLoader  * loader = PeResourceLoader_Open(argv[i]);
    if (!loader) {
      printf("File %s does not exist or is not a valid PE file\n", argv[i]);
      return_value = 2;
      continue;
    }

    uint32_t bitmap_count = 0;
    uint32_t * bitmap_ids = PeResourceLoader_GetResourceIds(loader, PRL_TYPE_BITMAP, &bitmap_count);
    if (bitmap_count == 0) {
      printf("No bitmaps found in file %s\n", argv[i]);
      PeResourceLoader_Close(loader);
      if (bitmap_ids) {
        free(bitmap_ids);
      }
      return_value = 3;
      continue;
    }

    uint16_t language_count = 0;
    uint32_t * languages = PeResourceLoader_GetLanguageIds(loader, &language_count);
    if (language_count == 0) {
      printf("No languages found in file %s\n", argv[i]);
      PeResourceLoader_Close(loader);
      if (bitmap_ids) {
        free(bitmap_ids);
      }
      if (languages) {
        free(languages);
      }
      return_value = 4;
      continue;
    }

    for (uint16_t li = 0; li < language_count; li++) {
      printf("Language with id %u:\n", languages[li]);
      for (uint16_t bi = 0; bi < bitmap_count; bi++) {
        printf("Found bitmap with id %u\n", bitmap_ids[bi]);
        char * file_name = calloc(34, sizeof(char));
        snprintf(file_name, 34, "%i_%u_%u.bmp", i, languages[li], bitmap_ids[bi]);
        FILE * file = fopen(file_name, "wb");
        free(file_name);
        uint32_t file_size = 0;
        void * data = PeResourceLoader_GetResource(loader, PRL_TYPE_BITMAP, languages[li], bitmap_ids[bi], &file_size);
        printf("File size is %u\n", file_size);
        fwrite(data, 1, file_size, file);
        fclose(file);
        free(data);
      }
      printf("\n");
    }
    free(languages);
    free(bitmap_ids);

    PeResourceLoader_Close(loader);
  }

  return return_value;
}
