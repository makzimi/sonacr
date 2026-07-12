import Foundation

final class IosBridgeSwiftContract {
    func typecheckBridgeSymbols() {
        let invalidStart = lacr_ios_capture_start(nil)
        _ = invalidStart == LACR_IOS_CAPTURE_INVALID_ARGUMENT
        lacr_ios_capture_stop(nil)
        lacr_ios_capture_destroy(nil)
    }
}
