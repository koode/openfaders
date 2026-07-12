// openfaders-route: a Phase 1 command-line tool that proves audio flows from the
// OpenFaders virtual devices to the RODECaster. It routes each virtual device's
// mirrored input into the matching stereo channel pair of the board's
// "Main Multitrack" output, then runs until interrupted.
//
// Usage: openfaders-route ["Output Device Name"]

#include "RoutingEngine.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile sig_atomic_t gShouldStop = 0;

static void HandleInterrupt(int signalNumber) {
    (void)signalNumber;
    gShouldStop = 1;
}

int main(int argc, const char **argv) {
    const char *outputDeviceName = "RODECaster Pro II Main Multitrack";
    if (argc > 1) {
        outputDeviceName = argv[1];
    }

    // Default Phase 1 mapping: five virtual devices to the five stereo pairs of
    // the ten-channel Multitrack output.
    const RoutingRoute routes[] = {
        {"app.openfaders.device.0", 0},
        {"app.openfaders.device.1", 2},
        {"app.openfaders.device.2", 4},
        {"app.openfaders.device.3", 6},
        {"app.openfaders.device.4", 8},
    };
    const uint32_t routeCount = sizeof(routes) / sizeof(routes[0]);

    printf("OpenFaders routing engine (Phase 1)\n");
    RoutingEngine *engine = RoutingEngineStart(outputDeviceName, routes, routeCount);
    if (engine == NULL) {
        return 1;
    }

    signal(SIGINT, HandleInterrupt);
    signal(SIGTERM, HandleInterrupt);
    printf("Routing. Play audio into an OpenFaders device to hear it on the board. Ctrl-C to stop.\n");
    while (!gShouldStop) {
        usleep(200 * 1000);
    }

    printf("\nStopping.\n");
    RoutingEngineStop(engine);
    return 0;
}
