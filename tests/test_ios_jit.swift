import Foundation
import JavaScriptCore
import SwiftUI
import UIKit

@main
struct IOSJITTests {
    @MainActor static func main() throws {
        // This reproduces the screenshot: CS_DEBUGGED is set, but nobody
        // answers traps. Displaying On or allowing an install here is wrong.
        henge_test_debugged(true)
        precondition(JITBootstrap.debuggedFlag)
        precondition(!JITBootstrap.ready)
        precondition(!JITBootstrap.prewarm())
        precondition(!JITBootstrap.ready && !JITBootstrap.isLive)

        let jit = JITCoordinator.shared
        jit.method = .stikDebug
        jit.scenePhaseChanged(.active)
        precondition(!jit.ready)
        let probesBeforeHandoff = henge_test_probe_count()
        jit.enable()
        precondition(jit.busy && !jit.ready && jit.attachGeneration == 0)
        precondition(UIApplication.shared.openedURLs.count == 1)
        let url = UIApplication.shared.openedURLs[0]
        let query = URLComponents(url: url, resolvingAgainstBaseURL: false)!.queryItems!
        precondition(query.contains { $0.name == "pid" && $0.value == String(getpid()) })
        let encoded = query.first { $0.name == "script-data" }!.value!
            .replacingOccurrences(of: "-", with: "+").replacingOccurrences(of: "_", with: "/")
        let padded = encoded + String(repeating: "=", count: (4 - encoded.count % 4) % 4)
        let script = String(data: Data(base64Encoded: padded)!, encoding: .utf8)!
        try checkScript(script)

        // An existing flag must not complete the new request before returning
        // from StikDebug, and a second tap must not issue a concurrent request.
        jit.enable()
        RunLoop.main.run(until: Date().addingTimeInterval(0.6))
        precondition(henge_test_probe_count() == probesBeforeHandoff)
        precondition(UIApplication.shared.openedURLs.count == 1)
        precondition(jit.busy && !jit.ready && jit.attachGeneration == 0)

        UIApplication.shared.applicationState = .background
        jit.scenePhaseChanged(.background)
        henge_test_debugged(false)
        UIApplication.shared.applicationState = .active
        jit.scenePhaseChanged(.active)
        // StikDebug can return to Henge before vAttach has set the flag.
        RunLoop.main.run(until: Date().addingTimeInterval(0.6))
        precondition(jit.busy && !jit.ready && jit.attachGeneration == 0)
        precondition(henge_test_probe_count() == probesBeforeHandoff)
        henge_test_debugged(true)
        // Attachment can precede the script. This failed prewarm must not be
        // cached forever, and the UI must still not announce success.
        RunLoop.main.run(until: Date().addingTimeInterval(0.6))
        precondition(jit.busy && !jit.ready && jit.attachGeneration == 0)
        precondition(henge_test_probe_count() > probesBeforeHandoff)

        // A trap responder can also fail the real allocation. Keep waiting for
        // a usable region rather than reporting success from the probe alone.
        henge_test_service_traps(true)
        RunLoop.main.run(until: Date().addingTimeInterval(0.6))
        precondition(henge_test_allocation_count() > 0)
        precondition(jit.busy && !jit.ready && !JITBootstrap.isLive)
        henge_test_allow_allocation(true)
        RunLoop.main.run(until: Date().addingTimeInterval(0.8))
        precondition(jit.ready && JITBootstrap.ready && JITBootstrap.isLive)
        precondition(!jit.busy && jit.error == nil && jit.attachGeneration == 1)
        precondition(henge_test_selftest_count() == 1)
        precondition(henge_test_detach_count() == 1)

        // The real allocator executed generated code through its RW/RX aliases.
        // Keep that region after detach, without another allocation or hand-off.
        let allocationCount = henge_test_allocation_count()
        precondition(!JITBootstrap.debuggedFlag)
        jit.scenePhaseChanged(.active)
        jit.enable()
        precondition(jit.ready && UIApplication.shared.openedURLs.count == 1)
        precondition(henge_test_allocation_count() == allocationCount)
        print("iOS JIT: readiness, stale flag, hand-off, allocation retry, execute self-test and detach checks passed")
    }

    static func checkScript(_ script: String) throws {
        let context = JSContext()!
        var failure: String?
        context.exceptionHandler = { _, value in failure = value?.toString() }
        context.evaluateScript("""
            let writes = [], stops = 0;
            function get_pid() { return 123; }
            function log() {}
            function prepare_memory_region() { return true; }
            function send_command(command) {
                if (command === 'c') {
                    ++stops;
                    if (stops > 3) throw new Error('The JIT script did not detach');
                    const commandRegister = stops === 2 ? '01' : '00';
                    return 'T05thread:1;20:0010000000000000;10:' + commandRegister
                        + '00000000000000;00:0000000000000000;01:0040000000000000;';
                }
                if (command === 'm1000,4') return stops === 1 ? '200d20d4' : 'a0013ed4';
                if (command === '_M4000,rx') return '200000000';
                if (command.startsWith('P0=')) writes.push(command);
                return 'OK';
            }
            """)
        context.evaluateScript(script)
        precondition(failure == nil, failure ?? "")
        let writes = context.evaluateScript("writes")!.toArray() as! [String]
        precondition(writes == ["P0=690000e000000000;thread:1;", "P0=0000000002000000;thread:1;"])
    }
}
