import ActivityKit
import AppIntents
import SwiftUI
import WidgetKit

struct Entry: TimelineEntry {
    let date: Date
    let mb: Int
}

struct Provider: TimelineProvider {
    func placeholder(in context: Context) -> Entry { Entry(date: .now, mb: 0) }

    func getSnapshot(in context: Context, completion: @escaping (Entry) -> Void) {
        completion(Entry(date: .now, mb: DataCounter.remainingMb))
    }

    func getTimeline(in context: Context, completion: @escaping (Timeline<Entry>) -> Void) {
        DataCounter.tick()
        completion(Timeline(entries: [Entry(date: .now, mb: DataCounter.remainingMb)], policy: .after(.now + 15 * 60)))
    }
}

struct DataWidget: Widget {
    var body: some WidgetConfiguration {
        StaticConfiguration(kind: "DataWidget", provider: Provider()) { entry in
            Button(intent: ResetIntent()) {
                VStack {
                    Text("\(entry.mb)").font(.system(size: 44, weight: .bold)).minimumScaleFactor(0.4).lineLimit(1)
                    Text("MB left").font(.caption)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .containerBackground(.fill.tertiary, for: .widget)
        }
        .configurationDisplayName("Data left")
        .description("Tap to reset to the amount set in the app.")
        .supportedFamilies([.systemSmall])
    }
}

/// The "status bar" counter: Dynamic Island at the top of the screen, plus the Lock Screen.
struct DataActivity: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: DataAttributes.self) { context in
            Label("\(context.state.mb) MB left", systemImage: "antenna.radiowaves.left.and.right")
                .font(.title2.bold())
                .padding()
        } dynamicIsland: { context in
            DynamicIsland {
                DynamicIslandExpandedRegion(.center) {
                    Text("\(context.state.mb) MB left").font(.title2.bold())
                }
            } compactLeading: {
                Image(systemName: "antenna.radiowaves.left.and.right")
            } compactTrailing: {
                Text("\(context.state.mb)").monospacedDigit()
            } minimal: {
                Text("\(context.state.mb)").font(.caption2).minimumScaleFactor(0.5)
            }
        }
    }
}

@main
struct DataCounterWidgets: WidgetBundle {
    var body: some Widget {
        DataWidget()
        DataActivity()
    }
}
