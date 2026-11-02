import AppIntents
import WidgetKit

/// Widget tap: reset the counter to the amount set in the app.
/// A LiveActivityIntent compiled into the app runs in the app's process, which is the only place ActivityKit works.
struct ResetIntent: LiveActivityIntent {
    static let title: LocalizedStringResource = "Reset data counter"

    func perform() async throws -> some IntentResult {
        DataCounter.reset()
        #if WIDGET
        WidgetCenter.shared.reloadAllTimelines()
        #else
        await LiveData.refresh(restart: true)
        #endif
        return .result()
    }
}
