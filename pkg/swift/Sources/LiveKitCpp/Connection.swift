import Foundation

public struct TokenResponse: Sendable {
    public let serverURL: String
    public let token: String
    public init(serverURL: String, token: String) { self.serverURL = serverURL; self.token = token }
}

public struct TokenRequest: Sendable {
    public let roomName: String
    public let participantIdentity: String
    public let participantName: String
    public let forceRefresh: Bool
}

final class TokenContext {
    let fetch: @Sendable (TokenRequest) throws -> TokenResponse
    let strings = CallbackStrings()
    init(_ fetch: @escaping @Sendable (TokenRequest) throws -> TokenResponse) { self.fetch = fetch }
}

extension Room {
    /// Fetch runs on calling/recovery threads. Do not call room operations from fetch.
    public func connect(tokenSource: @escaping @Sendable (TokenRequest) throws -> TokenResponse,
                        fetchOptions: inout lk_token_source_fetch_options_t,
                        options: inout lk_room_connect_options_t) throws {
        try withNativeHandle { room in
            let context = TokenContext(tokenSource)
            // Retain even after a failed connect: the native engine may retain the callback.
            retainContext(context)
            try LiveKitError.check(lk_room_connect_with_token_source_and_options(room, { user, options, refresh, response in
                guard let user, let response else { return LK_STATUS_INVALID_ARGUMENT }
                let context = Unmanaged<TokenContext>.fromOpaque(user).takeUnretainedValue()
                do {
                    let token = try context.fetch(TokenRequest(roomName: copyString(options?.pointee.room_name),
                        participantIdentity: copyString(options?.pointee.participant_identity),
                        participantName: copyString(options?.pointee.participant_name), forceRefresh: refresh != 0))
                    lk_token_source_response_init(response)
                    response.pointee.server_url = context.strings.pointer(token.serverURL)
                    response.pointee.participant_token = context.strings.pointer(token.token)
                    return LK_STATUS_OK
                } catch { return LK_STATUS_OPERATION_FAILED }
            }, Unmanaged.passUnretained(context).toOpaque(), &fetchOptions, &options))
        }
    }
}
