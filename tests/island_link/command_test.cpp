#include <helpers/bridges/IslandLinkCommand.h>
#include <assert.h>
#include <stdio.h>

int main() {
  const char* commands[] = {"get ilink", "set ilink on", "set ilink off"};
  const char* endings[] = {"", " ", "\r", "\n", "\r\n", " \t\r\n\v\f"};
  for (const char* command : commands) {
    for (const char* ending : endings) {
      char input[64];
      snprintf(input, sizeof(input), "%s%s", command, ending);
      island_link::trimCommandWhitespace(input);
      assert(strcmp(input, command) == 0);
    }
  }
  char empty[] = "";
  char blank[] = " \r\n\t";
  char invalid[] = "set ilink on extra\r\n";
  char high_byte[] = {'g', char(0xff), '\n', 0};
  island_link::trimCommandWhitespace(empty);
  island_link::trimCommandWhitespace(blank);
  island_link::trimCommandWhitespace(invalid);
  island_link::trimCommandWhitespace(high_byte);
  assert(empty[0] == 0 && blank[0] == 0);
  assert(strcmp(invalid, "set ilink on extra") == 0);
  assert(high_byte[0] == 'g' && (unsigned char)high_byte[1] == 0xff && high_byte[2] == 0);
  puts("PASS: ilink CLI trailing whitespace, exact arguments and byte boundaries");
}
