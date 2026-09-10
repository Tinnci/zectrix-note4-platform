#include "probe.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc == 1) return research_probe(PROBE_NORMAL);
    if (argc == 2) {
        if (strcmp(argv[1], "spin") == 0) return research_probe(PROBE_SPIN);
        if (strcmp(argv[1], "start") == 0) return research_probe(PROBE_START);
        if (strcmp(argv[1], "post") == 0) return research_probe(PROBE_POST);
    }
    fputs("Usage: runtime_probe [spin|start|post]\n", stderr);
    return 2;
}
