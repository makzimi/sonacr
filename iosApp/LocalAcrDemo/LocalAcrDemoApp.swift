import SwiftUI

@main
struct LocalAcrDemoApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView(controller: makeController())
        }
    }

    private func makeController() -> DemoController {
        let databaseUrl = Bundle.main.url(forResource: "venue-demo", withExtension: "lacrdb")
        let recognizer = SharedDemoRecognizer(databasePath: databaseUrl?.path ?? "")
        return DemoController(recognizer: recognizer)
    }
}
