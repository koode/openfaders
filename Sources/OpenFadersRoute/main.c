// openfaders-route: a command-line diagnostic that proves audio flows from the
// OpenFaders virtual devices to the RODECaster. It routes each virtual device's
// mirrored input into the matching stereo channel pair of the board's
// "Main Multitrack" output, then runs until interrupted. The menu-bar app uses
// the same OpenFadersEngine library; this tool is the headless way to exercise it.
//
// Usage: openfaders-route [--adaptive] ["Output Device Name"]
//   --adaptive  enable clock-sync drift correction (needs the updated driver)

#include "OpenFadersEngine.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t gShouldStop = 0;

static void HandleInterrupt(int signalNumber) {
    (void)signalNumber;
    gShouldStop = 1;
}

int main(int argc, const char **argv) {
    const char *outputDeviceName = "RODECaster Pro II Main Multitrack";
    bool adaptiveSync = false;
    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        if (strcmp(argv[argIndex], "--adaptive") == 0) {
            adaptiveSync = true;
        } else {
            outputDeviceName = argv[argIndex];
        }
    }

    // Default mapping: five virtual devices to the five stereo pairs of the
    // ten-channel Multitrack output (0/1 System, 2/3 Game, 4/5 Music, 6/7 Virtual
    // A, 8/9 Virtual B).
    static const struct {
        const char *uid;
        uint32_t pairStart;
    } defaults[] = {
        {"app.openfaders.device.0", 0}, {"app.openfaders.device.1", 2}, {"app.openfaders.device.2", 4},
        {"app.openfaders.device.3", 6}, {"app.openfaders.device.4", 8},
    };

    OpenFadersEngineConfig config;
    memset(&config, 0, sizeof(config));
    strncpy(config.outputDeviceName, outputDeviceName, sizeof(config.outputDeviceName) - 1);
    config.syncMode = adaptiveSync ? kOpenFadersSyncAdaptive : kOpenFadersSyncHardwareTimed;
    config.routeCount = (uint32_t)(sizeof(defaults) / sizeof(defaults[0]));
    for (uint32_t index = 0; index < config.routeCount; ++index) {
        strncpy(config.routes[index].virtualDeviceUID, defaults[index].uid,
                sizeof(config.routes[index].virtualDeviceUID) - 1);
        config.routes[index].outputChannelPairStart = defaults[index].pairStart;
        config.routes[index].enabled = true;
    }

    printf("OpenFaders routing engine\n");
    OpenFadersEngine *engine = OpenFadersEngineCreate();
    if (engine == NULL) {
        fprintf(stderr, "OpenFaders: out of memory.\n");
        return 1;
    }

    char error[256] = {0};
    if (!OpenFadersEngineStart(engine, &config, error, sizeof(error))) {
        fprintf(stderr, "OpenFaders: %s\n", error);
        OpenFadersEngineDestroy(engine);
        return 1;
    }

    OpenFadersEngineStatus status;
    OpenFadersEngineGetStatus(engine, &status);
    printf("Output: \"%s\" — routing %u device(s), %s. Play audio into an OpenFaders device to hear "
           "it on the board. Ctrl-C to stop.\n",
           outputDeviceName, status.activeRouteCount,
           adaptiveSync ? "adaptive clock sync" : "hardware-timed");

    signal(SIGINT, HandleInterrupt);
    signal(SIGTERM, HandleInterrupt);
    while (!gShouldStop) {
        usleep(200 * 1000);
    }

    printf("\nStopping.\n");
    OpenFadersEngineDestroy(engine);
    return 0;
}
