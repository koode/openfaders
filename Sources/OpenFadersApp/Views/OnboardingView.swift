import SwiftUI

// First-run onboarding: install the audio driver (with the standard macOS admin
// prompt, no terminal), and two short reminders for the hardware side. Reused as a
// "set up" window from the menu whenever the driver needs installing or repairing.
struct OnboardingView: View {
    @Environment(AppModel.self) private var model
    @State private var launchAtLogin = LoginItem.isEnabled

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    driverStep
                    micStep
                    multitrackStep
                    audioStep
                }
                .padding(20)
            }
            Divider()
            footer
        }
        .frame(width: 480, height: 600)
    }

    private var header: some View {
        VStack(spacing: 8) {
            Image(systemName: "waveform")
                .font(.system(size: 40))
                .foregroundStyle(.tint)
            Text("Welcome to OpenFaders")
                .font(.title2).bold()
            Text("Route each app to its own fader on your RODECaster Pro II.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
        .padding(.top, 24)
        .padding(.bottom, 18)
        .padding(.horizontal, 20)
    }

    private var driverStep: some View {
        stepCard(number: 1, title: "Install the audio driver") {
            Text("OpenFaders needs a small Core Audio driver in your system's audio plug-in folder. Installing asks for your password once, the standard macOS way.")
                .font(.callout)
                .foregroundStyle(.secondary)
            driverStatusRow
            if let error = model.lastInstallError {
                Text(error)
                    .font(.caption)
                    .foregroundStyle(.red)
            }
        }
    }

    private var micStep: some View {
        stepCard(number: 2, title: "Allow microphone access") {
            Text("OpenFaders captures the audio you send into its virtual devices to forward it to the board. macOS treats that as microphone access; nothing from a microphone is recorded.")
                .font(.callout)
                .foregroundStyle(.secondary)
            micStatusRow
        }
    }

    @ViewBuilder private var micStatusRow: some View {
        switch model.micState {
        case .granted:
            Label("Microphone access granted", systemImage: "checkmark.circle.fill")
                .foregroundStyle(.green)
        case .notDetermined:
            Button("Allow microphone access") {
                Task { await model.requestMicrophoneAccess() }
            }
            .buttonStyle(.borderedProminent)
        case .denied:
            VStack(alignment: .leading, spacing: 6) {
                Label("Microphone access denied", systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(.orange)
                Text("Turn it on for OpenFaders in System Settings, then routing can start.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Button("Open System Settings") { MicrophonePermission.openSettings() }
            }
        case .restricted:
            VStack(alignment: .leading, spacing: 6) {
                Label("Microphone access is restricted", systemImage: "lock.fill")
                    .foregroundStyle(.orange)
                Text("A system policy (Screen Time or device management) is blocking microphone access, so routing can't capture audio.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    @ViewBuilder private var driverStatusRow: some View {
        if model.isInstallingDriver {
            HStack(spacing: 8) {
                ProgressView().controlSize(.small)
                Text("Installing…").foregroundStyle(.secondary)
            }
        } else if model.driverInstallState == .installed {
            HStack {
                Label("Audio driver installed", systemImage: "checkmark.circle.fill")
                    .foregroundStyle(.green)
                Spacer()
                Button("Reinstall") { Task { await model.installDriver() } }
                    .controlSize(.small)
            }
        } else {
            Button(model.driverInstallState == .outdated ? "Update audio driver" : "Install audio driver") {
                Task { await model.installDriver() }
            }
            .buttonStyle(.borderedProminent)
        }
    }

    private var footer: some View {
        HStack {
            Toggle("Open at login", isOn: Binding(
                get: { launchAtLogin },
                set: { newValue in
                    LoginItem.setEnabled(newValue)
                    launchAtLogin = LoginItem.isEnabled
                }))
            Spacer()
            Button(model.driverInstallState == .installed ? "Done" : "Close") {
                OnboardingPresenter.shared.close()
            }
            .keyboardShortcut(.defaultAction)
        }
        .padding(16)
    }

    private var multitrackStep: some View {
        stepCard(number: 3, title: "Set the RODECaster to Multitrack mode") {
            if model.multitrackDetected {
                Label("Multitrack mode detected (10-channel output)", systemImage: "checkmark.circle.fill")
                    .foregroundStyle(.green)
            } else {
                Text("On the board: Settings ▸ Outputs ▸ Multitrack ▸ USB 1 Input ▸ Expanded. That exposes the ten-channel return OpenFaders routes into, and OpenFaders detects it automatically. No software can toggle it for you.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var audioStep: some View {
        stepCard(number: 4, title: "Send an app to a fader") {
            Text("Choose an OpenFaders device (System, Game, Music, Virtual A or B) as an app's output, then bring the matching source up on a fader. Only System is monitored by default.")
                .font(.callout)
                .foregroundStyle(.secondary)
            if model.audioDetected {
                Label("Audio detected — it's working", systemImage: "checkmark.circle.fill")
                    .foregroundStyle(.green)
            } else {
                Label("Waiting for audio through an OpenFaders device…", systemImage: "waveform")
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func stepCard<Content: View>(number: Int, title: String,
                                         @ViewBuilder content: () -> Content) -> some View {
        HStack(alignment: .top, spacing: 12) {
            ZStack {
                Circle().fill(Color.accentColor.opacity(0.15)).frame(width: 26, height: 26)
                Text("\(number)").font(.footnote).bold().foregroundStyle(.tint)
            }
            VStack(alignment: .leading, spacing: 6) {
                Text(title).font(.headline)
                content()
            }
            Spacer(minLength: 0)
        }
    }
}
