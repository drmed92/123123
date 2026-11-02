import Foundation
import ActivityKit

/// What the Live Activity (Dynamic Island + Lock Screen) shows.
struct DataAttributes: ActivityAttributes {
    struct ContentState: Codable, Hashable { var mb: Int }
}

/// Counts cellular bytes since the last reset. The app and the widget share this state through an App Group.
enum DataCounter {
    static let defaults = UserDefaults(suiteName: "group.com.drmed.datacounter")!
    static let mb = 1_000_000 // same unit as Settings > Cellular

    static var quotaMb: Int {
        get { defaults.integer(forKey: "quota") }
        set { defaults.set(newValue, forKey: "quota") }
    }

    static var remainingMb: Int { max(0, quotaMb * mb - defaults.integer(forKey: "used")) / mb }

    static func reset() {
        defaults.set(0, forKey: "used")
        defaults.set(counters(), forKey: "last")
        defaults.set(bootTime(), forKey: "boot")
    }

    static func tick() {
        let cur = counters(), boot = bootTime()
        let last = defaults.dictionary(forKey: "last") as? [String: Int] ?? [:]
        // Boot time also shifts when the clock is set, so a reboot must also have pulled a counter down.
        let rebooted = abs(boot - defaults.integer(forKey: "boot")) > 300 && cur.contains { $0.value < last[$0.key] ?? 0 }
        let used = cur.reduce(defaults.integer(forKey: "used")) { $0 + delta(last: last[$1.key], cur: $1.value, rebooted: rebooted) }
        defaults.set(used, forKey: "used")
        defaults.set(cur, forKey: "last")
        defaults.set(boot, forKey: "boot")
    }

    /// Bytes since the last reading of one counter. iOS counters are 32-bit (wrap at 4 GiB) and restart at 0 on reboot.
    static func delta(last: Int?, cur: Int, rebooted: Bool) -> Int {
        guard !rebooted, let last else { return cur }
        return cur >= last ? cur - last : cur + (1 << 32) - last
    }

    /// Byte counters of the cellular interfaces, e.g. ["pdp_ip0.in": 123, "pdp_ip0.out": 45].
    static func counters() -> [String: Int] {
        var out: [String: Int] = [:]
        var head: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&head) == 0 else { return out }
        defer { freeifaddrs(head) }
        var p = head
        while let ifa = p?.pointee {
            p = ifa.ifa_next
            let name = String(cString: ifa.ifa_name)
            guard name.hasPrefix("pdp_ip"), ifa.ifa_addr?.pointee.sa_family == UInt8(AF_LINK),
                  let data = ifa.ifa_data?.assumingMemoryBound(to: if_data.self).pointee else { continue }
            out[name + ".in"] = Int(data.ifi_ibytes)
            out[name + ".out"] = Int(data.ifi_obytes)
        }
        return out
    }

    static func bootTime() -> Int {
        var tv = timeval(), size = MemoryLayout<timeval>.size
        sysctlbyname("kern.boottime", &tv, &size, nil, 0)
        return tv.tv_sec
    }
}
