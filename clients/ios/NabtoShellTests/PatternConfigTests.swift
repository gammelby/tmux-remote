import XCTest
@testable import NabtoShell

final class PatternConfigTests: XCTestCase {

    func testDecodeV3RulesFormat() {
        let json = """
        {
          "version": 3,
          "agents": {
            "test-agent": {
              "name": "Test Agent",
              "rules": [
                {
                  "id": "yes_prompt",
                  "type": "yes_no",
                  "prompt_regex": "Continue\\\\?.*\\\\(y\\\\/n\\\\)",
                  "actions": [
                    { "label": "Yes", "keys": "y" },
                    { "label": "No", "keys": "n" }
                  ]
                }
              ]
            }
          }
        }
        """.data(using: .utf8)!

        let config = PatternConfigLoader.load(from: json)
        XCTAssertNotNil(config)
        XCTAssertEqual(config?.version, 3)
        XCTAssertEqual(config?.agents.count, 1)

        let agent = config?.agents["test-agent"]
        XCTAssertEqual(agent?.name, "Test Agent")
        XCTAssertEqual(agent?.rules.count, 1)

        let rule = agent?.rules[0]
        XCTAssertEqual(rule?.id, "yes_prompt")
        XCTAssertEqual(rule?.type, .yesNo)
        XCTAssertEqual(rule?.actions?.count, 2)
        XCTAssertEqual(rule?.promptRegex, "Continue\\?.*\\(y\\/n\\)")
    }

    func testDecodeNumberedMenuRule() {
        let json = """
        {
          "version": 3,
          "agents": {
            "a": {
              "name": "A",
              "rules": [
                {
                  "id": "menu",
                  "type": "numbered_menu",
                  "prompt_regex": "Pick one",
                  "option_regex": "^\\\\s*([0-9]+)\\\\.\\\\s+(.+)$",
                  "action_template": { "keys": "{number}\\n" },
                  "max_scan_lines": 8
                }
              ]
            }
          }
        }
        """.data(using: .utf8)!

        let config = PatternConfigLoader.load(from: json)
        XCTAssertNotNil(config)

        let rule = config?.agents["a"]?.rules[0]
        XCTAssertEqual(rule?.type, .numberedMenu)
        XCTAssertEqual(rule?.actionTemplate?.keys, "{number}\n")
        XCTAssertEqual(rule?.maxScanLines, 8)
        XCTAssertEqual(rule?.optionRegex, "^\\s*([0-9]+)\\.\\s+(.+)$")
    }

    func testDecodeEmptyRules() {
        let json = """
        {
          "version": 3,
          "agents": {
            "empty": {
              "name": "Empty",
              "rules": []
            }
          }
        }
        """.data(using: .utf8)!

        let config = PatternConfigLoader.load(from: json)
        XCTAssertNotNil(config)
        XCTAssertEqual(config?.agents["empty"]?.rules.count, 0)
    }

    func testInvalidJSON() {
        let json = "not json".data(using: .utf8)!
        let config = PatternConfigLoader.load(from: json)
        XCTAssertNil(config)
    }

    func testMissingRequiredFields() {
        let json = """
        {
          "agents": {}
        }
        """.data(using: .utf8)!
        let config = PatternConfigLoader.load(from: json)
        XCTAssertNil(config)
    }
}
