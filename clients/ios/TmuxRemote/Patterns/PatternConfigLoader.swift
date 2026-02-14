import Foundation

enum PatternConfigLoader {
    static func loadBundled() -> PatternConfig? {
        guard let url = Bundle.main.url(forResource: "patterns", withExtension: "json") else {
            return nil
        }
        return load(from: url)
    }

    static func load(from url: URL) -> PatternConfig? {
        guard let data = try? Data(contentsOf: url) else {
            return nil
        }
        return load(from: data)
    }

    static func load(from data: Data) -> PatternConfig? {
        let decoder = JSONDecoder()
        return try? decoder.decode(PatternConfig.self, from: data)
    }
}
