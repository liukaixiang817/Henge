import Foundation

/// A synthetic identity belonging to one new Mac. It never reads the host's
/// identifiers or contains Apple Account credentials. Keep it on export/import.
struct SoftwareIdentity: Codable, Hashable, Sendable {
    var version = 1
    var id: UUID
    var serial: String
    var mac: String
    var mlb: String
    var model = "Macmini8,1"

    static func validMLB(_ value: String) -> Bool {
        let alphabet = Array("0123456789ABCDEFGHJKLMNPQRSTUVWXYZ".utf8)
        guard value.utf8.count == 17 else { return false }
        var sum = 0
        for (i, byte) in value.utf8.enumerated() {
            guard let digit = alphabet.firstIndex(of: byte) else { return false }
            sum += digit * (i % 2 == 0 ? 1 : 3)
        }
        return sum % 34 == 0
    }

    /// The classic board date is decoded from the serial's encoded fields by
    /// macserial, including its half-year offset and year-boundary convention.
    /// Keep this separate from a calendar week calculation.
    private static func boardPrefix(for serial: String) -> String? {
        let chars = Array(serial)
        let years = Array("CDFGHJKLMNPQRSTVWXYZ")
        guard chars.count == 12, let yearIndex = years.firstIndex(of: chars[3]),
              "CDFGHJKLWXYZ".contains(chars[3]) else { return nil }
        let half = yearIndex % 2 == 0 ? "123456789CDFGHJKLMNPQRTVWX" : "123456789CDFGHJKLMNPQRTVWXY"
        guard half.contains(chars[4]),
              let weekIndex = Array("123456789CDFGHJKLMNPQRSTVWXYZ").firstIndex(of: chars[4]) else { return nil }
        var year = yearIndex / 2
        var week = weekIndex + (yearIndex % 2 == 0 ? 0 : 27)
        if week == 0 { week = 53; year = (year + 9) % 10 }
        return "C07\(year)\(String(format: "%02d", week))"
    }

    static func make() -> SoftwareIdentity {
        // Classic formats and default model/board choices:
        // https://github.com/acidanthera/OpenCorePkg/tree/master/Utilities/macserial
        // Generate independent values; no example or physical Mac identity is reused.
        let alphabet = Array("0123456789ABCDEFGHJKLMNPQRSTUVWXYZ")
        let years = Array("CDFGHJKLMNPQRSTVWXYZ")
        let weeks = Array("0123456789CDFGHJKLMNPQRTVWX123456789CDFGHJKLMNPQRTVWXY")
        let year = Int.random(in: 2018...2023), week = Int.random(in: 1...52)
        let base = year >= 2020 ? 2020 : 2010
        let yearCode = years[(year - base) * 2 + (week >= 27 ? 1 : 0)]
        let productionLine = Int.random(in: 0...3399)
        let copy = productionLine > 1155 ? (productionLine - 1155 + 67) / 68 : 0
        let remainder = productionLine - copy * 68
        let line = String([alphabet[copy], alphabet[remainder / 34], alphabet[remainder % 34]])
        let serial = "C07\(yearCode)\(weeks[week])\(line)JYVX"
        // The public Apple OUI used by the successful GenSMBIOS-style control.
        // Only the prefix is shared; each new Mac generates its own suffix.
        let bytes: [UInt8] = [0x00, 0x16, 0xcb] + (0..<3).map { _ in UInt8.random(in: .min ... .max) }
        let mac = bytes.map { String(format: "%02x", $0) }.joined(separator: ":")
        let prefix = boardPrefix(for: serial)!
        let parts = ["200", "600", "403", "404", "405", "303", "108", "207", "609", "501", "306", "102", "701", "301", "501", "101", "300", "130", "100", "270", "310", "902", "104", "401", "902", "500", "700", "802"]
        var mlb: String
        repeat {
            mlb = prefix + parts.randomElement()! + ["GU", "4N", "J9", "QX", "OP", "CD", "GU"].randomElement()!
                + "KXPG" + ["1H", "1M", "AD", "1F", "A8", "UE", "JA", "JC", "8C", "CB", "FB"].randomElement()!
        } while !validMLB(mlb)
        return SoftwareIdentity(version: 2, id: UUID(), serial: serial, mac: mac, mlb: mlb)
    }

    var isValid: Bool {
        let allowed = Set("0123456789ABCDEFGHJKLMNPQRSTUVWXYZ".utf8)
        let parts = mac.split(separator: ":", omittingEmptySubsequences: false)
        let bytes = parts.compactMap { $0.count == 2 ? UInt8($0, radix: 16) : nil }
        let common = model == "Macmini8,1" && Self.validMLB(mlb)
            && serial.utf8.count == 12 && serial.utf8.allSatisfy(allowed.contains)
            && parts.count == 6 && bytes.count == 6
        guard common else { return false }
        if version == 1 { return bytes[0] & 3 == 2 } // keep existing identities unchanged
        guard version == 2, bytes.starts(with: [0x00, 0x16, 0xcb]),
              serial.hasPrefix("C07"), serial.hasSuffix("JYVX"),
              let prefix = Self.boardPrefix(for: serial) else { return false }
        return mlb.hasPrefix(prefix) && mlb.dropFirst(11).hasPrefix("KXPG")
    }

    var manifest: [String: Any] {
        ["version": version, "id": id.uuidString, "serial": serial, "mac": mac, "mlb": mlb, "model": model]
    }

    /// The native boot API already takes its per-session settings from the environment.
    /// Clear every field for ordinary Macs so a previous session cannot supply an identity.
    static func prepare(_ identity: SoftwareIdentity?) -> Bool {
        for name in ["HV_SOFTWARE_IDENTITY", "HV_SOFTWARE_SERIAL", "HV_SOFTWARE_MAC", "HV_SOFTWARE_MLB"] { unsetenv(name) }
        guard let identity else { return true }
        guard identity.isValid else { return false }
        setenv("HV_SOFTWARE_SERIAL", identity.serial, 1)
        setenv("HV_SOFTWARE_MAC", identity.mac, 1)
        setenv("HV_SOFTWARE_MLB", identity.mlb, 1)
        setenv("HV_SOFTWARE_IDENTITY", "1", 1)
        return true
    }
}
