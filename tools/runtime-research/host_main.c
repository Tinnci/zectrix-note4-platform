#include "probe.h"

#include <string.h>

int main(int argc, char** argv) {
    return research_probe(argc == 2 && strcmp(argv[1], "spin") == 0);
}
