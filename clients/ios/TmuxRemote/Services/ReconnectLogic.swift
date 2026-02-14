import Foundation

struct ReconnectLogic {
    let maxBackoff: TimeInterval = 15
    let maxTotalTime: TimeInterval = 30

    func backoff(attempt: Int) -> TimeInterval {
        let value = pow(2.0, Double(attempt - 1))
        return min(value, maxBackoff)
    }

    func shouldGiveUp(elapsedTime: TimeInterval) -> Bool {
        elapsedTime > maxTotalTime
    }
}
