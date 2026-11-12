import Foundation

/// Only app switching is simulated. SwiftUI, timers, the coordinator and the
/// native allocator run as compiled production code on the Mac.
public final class UIApplication {
    public enum State { case active, inactive, background }
    public static let shared = UIApplication()
    public var applicationState: State = .active
    public private(set) var openedURLs: [URL] = []

    public func canOpenURL(_ url: URL) -> Bool { url.scheme == "stikjit" }
    public func open(_ url: URL, options: [String: Any] = [:], completionHandler: ((Bool) -> Void)? = nil) {
        openedURLs.append(url)
        completionHandler?(true)
    }
}
