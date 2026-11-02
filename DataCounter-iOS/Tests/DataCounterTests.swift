import XCTest
@testable import DataCounter

final class DataCounterTests: XCTestCase {
    func testDelta() {
        XCTAssertEqual(DataCounter.delta(last: 1000, cur: 1500, rebooted: false), 500)
        XCTAssertEqual(DataCounter.delta(last: (1 << 32) - 100, cur: 50, rebooted: false), 150) // 32-bit wrap
        XCTAssertEqual(DataCounter.delta(last: 5000, cur: 300, rebooted: true), 300)
        XCTAssertEqual(DataCounter.delta(last: nil, cur: 300, rebooted: false), 300) // new counter starts at 0
    }
}
