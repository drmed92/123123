import ActivityKit
import BackgroundTasks
import SwiftUI
import WidgetKit

@main
struct DataCounterApp: App {
    var body: some Scene {
        WindowGroup { ContentView() }
            .backgroundTask(.appRefresh(LiveData.taskId)) { await LiveData.refresh(restart: false) }
    }
}

struct ContentView: View {
    @Environment(\.scenePhase) private var phase
    @State private var text = DataCounter.quotaMb > 0 ? String(DataCounter.quotaMb) : ""
    @State private var left = DataCounter.remainingMb

    var body: some View {
        Form {
            Section("Left now") { Text("\(left) MB").font(.largeTitle.bold()) }
            Section {
                TextField("MB remaining", text: $text).keyboardType(.numberPad)
                Button("Set") {
                    guard text.count <= 9, let mb = Int(text) else { return }
                    DataCounter.quotaMb = mb
                    DataCounter.reset()
                    update()
                }
            } footer: {
                Text("Tapping the widget resets the counter to this value.")
            }
        }
        .onChange(of: phase, initial: true) { _, p in if p == .active { update() } }
    }

    private func update() {
        Task {
            await LiveData.refresh(restart: true)
            left = DataCounter.remainingMb
        }
    }
}

/// Keeps the widget and the Live Activity in sync with the counter.
enum LiveData {
    static let taskId = "com.drmed.datacounter.refresh"

    static func refresh(restart: Bool) async {
        DataCounter.tick()
        WidgetCenter.shared.reloadAllTimelines()
        let content = ActivityContent(state: DataAttributes.ContentState(mb: DataCounter.remainingMb), staleDate: nil)
        if restart { // a new activity resets iOS's 8-hour limit; only allowed in the foreground or from the widget tap
            for a in Activity<DataAttributes>.activities { await a.end(nil, dismissalPolicy: .immediate) }
            _ = try? Activity<DataAttributes>.request(attributes: DataAttributes(), content: content)
        } else {
            for a in Activity<DataAttributes>.activities { await a.update(content) }
        }
        let next = BGAppRefreshTaskRequest(identifier: taskId)
        next.earliestBeginDate = .now + 15 * 60 // iOS decides the real time
        try? BGTaskScheduler.shared.submit(next)
    }
}
