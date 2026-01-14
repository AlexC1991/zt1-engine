#include <stdio.h>
#include <stdlib.h>

#include <pe_resource_loader.h>

int main(int argc, char ** argv) {
  if (argc < 2) {
    printf("No file was defined\n");
    return 1;
  }

  int return_value = 0;
  for(int i = 1; i < argc; i++) {
    printf("String from file %s:\n", argv[i]);
    PeResourceLoader * loader = PeResourceLoader_Open(argv[i]);
    if (!loader) {
      printf("File %s does not exist or is not a valid PE file\n", argv[i]);
      return_value = 2;
      continue;
    }

    uint32_t string_count = 0;
    uint32_t * string_ids = PeResourceLoader_GetResourceIds(loader, PRL_TYPE_STRING, &string_count);
    if (string_count == 0) {
      printf("No strings found in file %s\n", argv[i]);
      PeResourceLoader_Close(loader);
      if (string_ids) {
        free(string_ids);
      }
      return_value = 3;
      continue;
    }

    uint16_t language_count = 0;
    uint32_t * languages = PeResourceLoader_GetLanguageIds(loader, &language_count);
    if (language_count == 0) {
      printf("No languages found in file %s\n", argv[i]);
      PeResourceLoader_Close(loader);
      if (string_ids) {
        free(string_ids);
      }
      if (languages) {
        free(languages);
      }
      return_value = 4;
      continue;
    }

    for (uint16_t li = 0; li < language_count; li++) {
      printf("Strings for language with id %u:\n", languages[li]);
      for (uint16_t si = 0; si < string_count; si++) {
        uint32_t length = 0;
        char * string = (char *) PeResourceLoader_GetResource(loader, PRL_TYPE_STRING, languages[li], string_ids[si], &length);
        if (string) {
          printf("%u: %s\n", string_ids[si], string);
        }
        free(string);
      }
      printf("\n");
    }
    free(languages);
    free(string_ids);

    PeResourceLoader_Close(loader);
  }

  return return_value;
}
