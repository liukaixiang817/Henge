import Foundation

enum Entitlements {
    static let memoryLimitAtLaunch = 4_000 << 20
}

enum HengeLog {
    static let deviceModel = "test-TXM-device"
    static func expectsTXM(model: String) -> Bool { true }
    static func log(_ category: String, _ message: String) {}
}

final class OnDevicePairing {
    static let shared = OnDevicePairing()
    func cancel() {}
}

@MainActor
enum HengeBuiltInJIT {
    static var isAvailable: Bool { false }
    static var unavailableReason: String? { "Helper extensions are not run by this host test." }
    static func send(_ request: HengeJITRequest, started: @escaping () -> Void = {},
                     completion: @escaping (Result<HengeJITRequest.Response, Error>) -> Void) {
        preconditionFailure("The external debugger test must not start a helper extension")
    }
}
