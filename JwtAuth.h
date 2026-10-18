#pragma once
#include <jwt-cpp/jwt.h>
#include <optional>
#include <sstream>
#include <map>

namespace dara {
constexpr int JwtTolerance = 30;
struct AuthenticatedLogin {
    std::string characterName;
    std::string zone; // Client-supplied, not authenticated by JWT.
};
struct LoginClock {
    jwt::date value;
    jwt::date now() const { return value; }
};
inline std::optional<AuthenticatedLogin> VerifyLogin(
    const std::string& message, const std::string& secret,
    jwt::date now = std::chrono::system_clock::now(),
    std::string* failureReason = nullptr) {
    std::string stage = "Invalid login message";
    auto reject = [&](const std::string& reason) -> std::optional<AuthenticatedLogin> {
        if (failureReason) *failureReason = reason;
        return std::nullopt;
    };
    if (failureReason) failureReason->clear();
    if (secret.empty()) return reject("Server JWT secret is missing or empty");
    if (message.rfind("connect:", 0) != 0) return reject("Expected connect: login text frame");
    try {
        std::map<std::string, std::string> fields;
        std::istringstream input(message.substr(8));
        std::string part;
        while (std::getline(input, part, ';')) {
            auto pos = part.find('=');
            if (pos == std::string::npos || pos == 0 ||
                !fields.emplace(part.substr(0, pos), part.substr(pos + 1)).second)
                return reject("Malformed login field or duplicate field; expected key=value separated by semicolons");
        }
        if (!fields.count("name") || fields.at("name").empty()) return reject("Missing or empty login name");
        if (!fields.count("pass") || fields.at("pass").empty()) return reject("Missing or empty JWT in pass field");
        stage = "Malformed JWT; expected a compact header.payload.signature token";
        auto token = jwt::decode(fields.at("pass"));
        stage = "Missing or invalid JWT alg header; HS256 is required";
        if (token.get_algorithm() != "HS256") return reject("JWT algorithm rejected; only HS256 is allowed");
        // Require integer NumericDates and bound conversion to system_clock's range.
        const auto maxSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            jwt::date::duration::max()).count() - JwtTolerance;
        for (const auto* claim : {"iat", "exp"}) {
            stage = std::string("Missing or invalid JWT ") + claim + "; expected integer Unix seconds";
            auto value = token.get_payload_claim(claim).as_integer();
            if (value < 0 || value > maxSeconds) return reject(stage + " within supported range");
        }
        stage = "Missing or invalid JWT iss; expected dara-game-server";
        if (token.get_issuer() != "dara-game-server") return reject(stage);
        stage = "Missing or invalid JWT aud; expected dara-chat";
        if (!token.get_audience().count("dara-chat")) return reject(stage);
        if (token.get_expires_at() <= token.get_issued_at()) return reject("JWT exp must be later than iat");
        if (now >= token.get_expires_at() + std::chrono::seconds(JwtTolerance))
            return reject("JWT expired; obtain a new token from Unreal (30-second tolerance)");
        if (now < token.get_issued_at() - std::chrono::seconds(JwtTolerance))
            return reject("JWT iat is in the future; check game/chat server clocks (30-second tolerance)");
        stage = "JWT verification failed; check signature and optional nbf claim";
        std::error_code error;
        jwt::verify<LoginClock, jwt::traits::kazuho_picojson>(LoginClock{now})
            .allow_algorithm(jwt::algorithm::hs256{secret})
            .with_issuer("dara-game-server")
            .with_audience("dara-chat")
            .leeway(JwtTolerance)
            .verify(token, error);
        if (error) {
            if (error.category() == jwt::error::signature_verification_error_category())
                return reject("JWT signature verification failed; check that Unreal and chat use the same raw UTF-8 secret and token is unchanged");
            if (error == jwt::error::make_error_code(jwt::error::token_verification_error::token_expired))
                return reject("JWT timestamp validation failed; check optional nbf and server clocks");
            return reject(stage);
        }
        stage = "Missing or invalid JWT characterName; expected a nonempty string";
        auto name = token.get_payload_claim("characterName").as_string();
        if (name.empty()) return reject(stage);
        if (fields.at("name") != name) return reject("Login name does not exactly match verified JWT characterName (case-sensitive)");
        return AuthenticatedLogin{name, fields["zone"]};
    } catch (const std::exception&) {
        // Never expose library exceptions: they may contain credential data.
        return reject(stage);
    }
}
} // namespace dara
