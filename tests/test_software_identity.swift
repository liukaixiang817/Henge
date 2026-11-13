import Foundation

@main
struct IdentityTests {
    static func main() throws {
        var checks = 0
        func check(_ condition: @autoclosure () -> Bool, _ name: String) {
            checks += 1
            if !condition() { fatalError(name) }
        }
        var identities = Set<UUID>(), macs = Set<String>()
        var samples: [SoftwareIdentity] = []
        for i in 0..<1000 {
            let identity = SoftwareIdentity.make()
            check(identity.isValid, "generated profile is valid")
            check(identities.insert(identity.id).inserted, "separate machine identifier")
            macs.insert(identity.mac)
            check(identity.version == 2, "new installations use the verified format")
            check(identity.mac.hasPrefix("00:16:cb:"), "new ROM uses the diagnostic control's public OUI")
            let data = try JSONEncoder().encode(identity)
            let decoded = try JSONDecoder().decode(SoftwareIdentity.self, from: data)
            check(identity == decoded, "identity survives persistence")
            let manifest = try JSONSerialization.data(withJSONObject: identity.manifest)
            let imported = try JSONDecoder().decode(SoftwareIdentity.self, from: manifest)
            check(identity == imported, "export/import keeps the identity")
            if i < 25 || CommandLine.arguments.count >= 2 { samples.append(identity) }
        }
        // A public OUI leaves a 24-bit random suffix. Allow incidental sample
        // collisions, but catch accidental use of a constant test-machine MAC.
        check(macs.count > 990, "new machines generate independent ROM suffixes")
        var legacy = samples[0]
        legacy.version = 1; legacy.mac = "02:48:56:12:34:50"
        check(legacy.isValid, "old locally administered profiles remain valid")
        let decodedLegacy = try JSONDecoder().decode(SoftwareIdentity.self, from: JSONEncoder().encode(legacy))
        check(decodedLegacy == legacy, "old profiles survive loading without replacement")
        var bad = samples[0]
        check(SoftwareIdentity.validMLB("C07323700J9KXPG1M"), "known OpenCore board checksum")
        check(!SoftwareIdentity.validMLB("C07323700J9KXPG1H"), "bad board checksum rejected")
        bad.mlb = "0000"
        check(!bad.isValid, "incomplete board identity rejected")
        bad = samples[0]; bad.model = "Macmini9,1"
        check(!bad.isValid, "another platform cannot reuse this profile")
        bad = samples[0]
        bad.mac = "01:00:00:00:00:01"
        check(!bad.isValid, "multicast MAC rejected")
        bad.mac = "02:00:00:00:00"
        check(!bad.isValid, "truncated MAC rejected")
        bad = samples[0]; bad.version = 3
        check(!bad.isValid, "unknown profile version rejected")
        check(!SoftwareIdentity.prepare(bad), "invalid profile is not activated")
        check(getenv("HV_SOFTWARE_IDENTITY") == nil, "invalid profile leaves no mode behind")
        check(SoftwareIdentity.prepare(samples[0]), "valid profile activated")
        check(getenv("HV_SOFTWARE_IDENTITY").map { String(cString: $0) } == "1", "mode is explicit")
        check(getenv("HV_SOFTWARE_MLB").map { String(cString: $0) } == samples[0].mlb, "board identity reaches native boot")
        check(getenv("HV_SOFTWARE_MAC").map { String(cString: $0) } == samples[0].mac, "public-OUI ROM reaches native boot")
        bad = samples[0]; bad.mac = legacy.mac
        check(!bad.isValid, "v2 cannot silently revert to the old ROM format")
        bad = legacy; bad.mac = samples[0].mac
        check(!bad.isValid, "v1 imports cannot masquerade as the new profile")
        bad = samples[0]
        var board = Array(bad.mlb)
        board[3] = board[3] == "0" ? "1" : "0"
        for char in "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ" {
            board[16] = char
            if SoftwareIdentity.validMLB(String(board)) { break }
        }
        bad.mlb = String(board)
        check(SoftwareIdentity.validMLB(bad.mlb) && !bad.isValid, "checksum alone cannot validate a mismatched board date")
        check(SoftwareIdentity.prepare(nil), "ordinary machine clears compatibility")
        check(getenv("HV_SOFTWARE_IDENTITY") == nil && getenv("HV_SOFTWARE_SERIAL") == nil && getenv("HV_SOFTWARE_MAC") == nil && getenv("HV_SOFTWARE_MLB") == nil,
              "no previous machine identity leaks into another Mac")
        if CommandLine.arguments.count >= 2 {
            try JSONEncoder().encode(samples).write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
        }
        if CommandLine.arguments.count == 3 {
            // The runtime control is supplied locally, never compiled into the app.
            var reference = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[2]))) as! [String: Any]
            reference["version"] = 2
            let control = try JSONDecoder().decode(SoftwareIdentity.self, from: JSONSerialization.data(withJSONObject: reference))
            check(control.isValid, "the successful runtime control satisfies v2 validation")
        }
        print("software identity: \(checks) checks, 0 failures")
    }
}
