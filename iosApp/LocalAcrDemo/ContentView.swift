import AVFoundation
import SwiftUI

struct ContentView: View {
    @StateObject private var controller: DemoController
    @State private var permissionGranted = false

    init(controller: DemoController) {
        _controller = StateObject(wrappedValue: controller)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Text("Local ACR Demo")
                .font(.largeTitle)
                .bold()
            Text("Listen for a venue cue and show a local promotion.")

            Text("Status: \(statusText)")
                .font(.headline)

            if !permissionGranted {
                Button("Allow microphone") {
                    requestPermission()
                }
                .buttonStyle(.borderedProminent)
            }

            if let message = controller.state.errorMessage {
                Text("Recognition error: \(message)")
                    .foregroundStyle(.red)
            }

            if let promotion = controller.state.promotion {
                promotionCard(promotion)
            }

            if let localActionMessage = controller.state.localActionMessage {
                Text(localActionMessage)
                    .font(.callout)
            }
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)
        .onAppear {
            refreshPermissionAndStart()
        }
        .onDisappear {
            controller.screenHidden()
        }
    }

    private var statusText: String {
        switch controller.state.status {
        case .idle: "Idle"
        case .permissionRequired: "Permission required"
        case .preparing: "Preparing"
        case .listening: "Listening"
        case .error: "Error"
        }
    }

    @ViewBuilder
    private func promotionCard(_ promotion: DemoPromotion) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(promotion.title)
                .font(.title2)
                .bold()
            Text(promotion.body)
            Text("Trigger: \(promotion.triggerId) · confidence \(promotion.confidence)")
                .font(.caption)
                .foregroundStyle(.secondary)
            HStack {
                Button(promotion.cta) {
                    controller.promotionCta()
                }
                .buttonStyle(.borderedProminent)
                Button("Dismiss") {
                    controller.dismissPromotion()
                }
                .buttonStyle(.bordered)
            }
        }
        .padding()
        .background(.thinMaterial)
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }

    private func refreshPermissionAndStart() {
        switch AVAudioSession.sharedInstance().recordPermission {
        case .granted:
            permissionGranted = true
            controller.screenVisible(permissionGranted: true)
        case .denied, .undetermined:
            permissionGranted = false
            controller.screenVisible(permissionGranted: false)
        @unknown default:
            permissionGranted = false
            controller.screenVisible(permissionGranted: false)
        }
    }

    private func requestPermission() {
        AVAudioSession.sharedInstance().requestRecordPermission { granted in
            DispatchQueue.main.async {
                permissionGranted = granted
                controller.screenVisible(permissionGranted: granted)
            }
        }
    }
}
