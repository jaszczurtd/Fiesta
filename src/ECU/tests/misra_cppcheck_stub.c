/* Model cppcheck's input handling without requiring an analyzer in CTest. */
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if ((argc == 2) && (strcmp(argv[1], "--version") == 0)) {
    puts("Cppcheck input fixture");
    return 0;
  }
  for (int i = 1; i < argc; ++i) {
    const size_t length = strlen(argv[i]);
    if ((length < 2U) || (strcmp(argv[i] + length - 2U, ".c") != 0)) {
      continue;
    }
    FILE *const source = fopen(argv[i], "r");
    /* Like cppcheck, ignore an unmatched glob alongside valid inputs. */
    if (source == NULL) {
      continue;
    }
    char line[80];
    if ((fgets(line, sizeof(line), source) != NULL) &&
        (strstr(line, "misra-probe-error") != NULL)) {
      printf("%s:1:1: style: fixture finding [misra-c2012-15.5]\n", argv[i]);
    }
    if (fclose(source) != 0) {
      return 2;
    }
  }
  return 0;
}
