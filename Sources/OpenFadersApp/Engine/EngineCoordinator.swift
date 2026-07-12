import Foundation

// A physical output device the engine can target, as surfaced to the UI.
struct OutputDevice: Identifiable, Hashable {
    let uid: String
    let name: String
    let outputChannelCount: Int
    let looksLikeMultitrackBoard: Bool

    var id: String { uid.isEmpty ? name : uid }
}

// A snapshot of the engine's runtime state for the UI to render.
struct EngineRuntimeStatus: Equatable {
    var running: Bool
    var outputDevicePresent: Bool
    var activeRouteCount: Int
    var overrunFrames: UInt64
    var underrunFrames: UInt64

    static let idle = EngineRuntimeStatus(running: false, outputDevicePresent: false,
                                          activeRouteCount: 0, overrunFrames: 0, underrunFrames: 0)
}

struct EngineStartError: Error {
    let message: String
}

// Reads a fixed-size C char array (imported into Swift as a tuple of CChar) as a
// Swift string. The engine null-terminates every such field, so reading up to the
// terminator is safe.
func stringFromCCharTuple<Tuple>(_ tuple: Tuple) -> String {
    var mutableTuple = tuple
    return withUnsafeBytes(of: &mutableTuple) { rawBuffer in
        guard let base = rawBuffer.baseAddress else { return "" }
        return String(cString: base.assumingMemoryBound(to: CChar.self))
    }
}

// Thin Swift facade over the C OpenFadersEngine library. All calls are expected on
// one thread (the app's main actor); the real-time audio work happens inside the
// engine on Core Audio's own threads.
final class EngineCoordinator {
    private let engine: OpaquePointer?

    init() {
        engine = OpenFadersEngineCreate()
    }

    deinit {
        OpenFadersEngineDestroy(engine)
    }

    func enumerateOutputDevices() -> [OutputDevice] {
        let available = Int(OpenFadersEngineCopyOutputDevices(nil, 0))
        guard available > 0 else { return [] }

        var buffer = [OpenFadersOutputDevice](repeating: OpenFadersOutputDevice(), count: available)
        let written = Int(OpenFadersEngineCopyOutputDevices(&buffer, UInt32(available)))
        return buffer.prefix(written).map { device in
            OutputDevice(uid: stringFromCCharTuple(device.uid),
                         name: stringFromCCharTuple(device.name),
                         outputChannelCount: Int(device.outputChannelCount),
                         looksLikeMultitrackBoard: device.looksLikeMultitrackBoard)
        }
    }

    func start(configuration: AppConfiguration) throws {
        var config = OpenFadersEngineConfig()
        OpenFadersEngineConfigInit(&config)
        OpenFadersEngineConfigSetOutput(&config, configuration.outputDeviceUID,
                                        configuration.outputDeviceName)
        for device in configuration.devices {
            OpenFadersEngineConfigAddRoute(&config, device.uid, UInt32(device.channelPairStart),
                                           device.enabled)
        }

        var errorBuffer = [CChar](repeating: 0, count: 256)
        let started = OpenFadersEngineStart(engine, &config, &errorBuffer, 256)
        if !started {
            throw EngineStartError(message: String(cString: errorBuffer))
        }
    }

    func stop() {
        OpenFadersEngineStop(engine)
    }

    func status() -> EngineRuntimeStatus {
        var raw = OpenFadersEngineStatus()
        OpenFadersEngineGetStatus(engine, &raw)
        return EngineRuntimeStatus(running: raw.running,
                                   outputDevicePresent: raw.outputDevicePresent,
                                   activeRouteCount: Int(raw.activeRouteCount),
                                   overrunFrames: raw.overrunFrames,
                                   underrunFrames: raw.underrunFrames)
    }
}
