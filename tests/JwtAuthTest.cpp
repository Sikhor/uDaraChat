#include "JwtAuth.h"
#include <iostream>
#include <cstdlib>

int main() {
    const auto now = jwt::date(std::chrono::seconds(1792310400));
    const std::string secret = "raw-UTF8-secret-ä-not-base64";
    int checks = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
    };
    auto payload = [&]() {
        return jwt::create().set_issuer("dara-game-server").set_audience("dara-chat")
            .set_subject("0").set_issued_at(now).set_expires_at(now + std::chrono::seconds(1800))
            .set_payload_claim("characterName", jwt::claim(std::string("Alice")));
    };
    auto login = [&](const std::string& token, const std::string& name = "Alice") {
        return "connect:name=" + name + ";zone=Dungeon;group=;pass=" + token;
    };
    auto valid = payload().sign(jwt::algorithm::hs256{secret});
    auto result = dara::VerifyLogin(login(valid), secret, now);
    check(result && result->characterName == "Alice" && result->zone == "Dungeon", "valid raw UTF-8 secret and character identity (sub=0)");
    check(result && !result->isAdmin, "legacy player token has no admin access");
    auto adminToken = payload().set_payload_claim("roles", jwt::claim(picojson::array{
        picojson::value(std::string("player")), picojson::value(std::string("chat_admin"))})).sign(jwt::algorithm::hs256{secret});
    auto admin = dara::VerifyLogin(login(adminToken), secret, now);
    check(admin && admin->isAdmin && admin->expiresAt == now + std::chrono::seconds(1800), "signed admin role and expiry recognized");
    check(!dara::VerifyLogin(login(adminToken), "wrong", now), "forged admin signature rejected");
    auto player = dara::VerifyLogin(login(payload().set_payload_claim("roles", jwt::claim(picojson::array{
        picojson::value(std::string("player"))})).sign(jwt::algorithm::hs256{secret})), secret, now);
    check(player && !player->isAdmin, "other roles do not grant admin");
    auto spoof = dara::VerifyLogin(login(valid) + ";roles=chat_admin", secret, now);
    check(spoof && !spoof->isAdmin, "login fields cannot grant admin");
    for (const auto& malformed : {picojson::value(std::string("chat_admin")),
            picojson::value(true), picojson::value(picojson::array{picojson::value(true)})})
        check(!dara::VerifyLogin(login(payload().set_payload_claim("roles", jwt::claim(malformed)).sign(
            jwt::algorithm::hs256{secret})), secret, now), "malformed roles rejected");
    check(dara::VerifyLogin("connect:pass=" + valid + ";name=Alice", secret, now).has_value(), "field order independent");
    check(!dara::VerifyLogin(login(valid, "alice"), secret, now), "exact name match");
    check(!dara::VerifyLogin(login(valid), "wrong", now), "wrong signature");
    check(!dara::VerifyLogin(login(valid), "", now), "missing secret");
    check(!dara::VerifyLogin("connect:name=Alice;zone=X", secret, now), "missing token");
    check(!dara::VerifyLogin(login(""), secret, now), "empty token");
    check(!dara::VerifyLogin(login("malformed"), secret, now), "malformed token");
    check(!dara::VerifyLogin(login(std::string(40, 'a')), secret, now), "legacy hash rejected");
    check(!dara::VerifyLogin(login(valid) + ";name=Bob", secret, now), "duplicate fields rejected");
    check(!dara::VerifyLogin(login(payload().sign(jwt::algorithm::hs512{secret})), secret, now), "HS512 rejected");
    check(!dara::VerifyLogin(login(payload().sign(jwt::algorithm::none{})), secret, now), "none rejected");
    for (const auto* claim : {"iss", "aud", "iat", "exp", "characterName"}) {
        auto p = payload();
        p.set_payload_claim(claim, jwt::claim(picojson::value()));
        check(!dara::VerifyLogin(login(p.sign(jwt::algorithm::hs256{secret})), secret, now), "null required claim");
        auto json = jwt::decode(valid).get_payload_json();
        json.erase(claim);
        auto missing = jwt::create();
        for (const auto& field : json) missing.set_payload_claim(field.first, jwt::claim(field.second));
        check(!dara::VerifyLogin(login(missing.sign(jwt::algorithm::hs256{secret})), secret, now), "missing required claim");
    }
    for (const auto* claim : {"iss", "aud", "iat", "exp", "characterName"}) {
        auto p = payload().set_payload_claim(claim, jwt::claim(std::string("wrong")));
        check(!dara::VerifyLogin(login(p.sign(jwt::algorithm::hs256{secret})), secret, now), "invalid claim");
    }
    check(!dara::VerifyLogin(login(payload().set_payload_claim("characterName", jwt::claim(std::string(""))).sign(jwt::algorithm::hs256{secret})), secret, now), "empty character");
    check(!dara::VerifyLogin(login(payload().set_payload_claim("iat", jwt::claim(picojson::value(1792310400.5))).sign(jwt::algorithm::hs256{secret})), secret, now), "fractional timestamp rejected");
    auto future = payload().set_issued_at(now + std::chrono::seconds(31)).sign(jwt::algorithm::hs256{secret});
    check(!dara::VerifyLogin(login(future), secret, now), "future beyond tolerance");
    auto tolerated = payload().set_issued_at(now + std::chrono::seconds(30)).sign(jwt::algorithm::hs256{secret});
    check(dara::VerifyLogin(login(tolerated), secret, now).has_value(), "30 second iat tolerance");
    check(dara::VerifyLogin(login(valid), secret, now + std::chrono::seconds(1829)).has_value(), "expiry tolerance");
    check(!dara::VerifyLogin(login(valid), secret, now + std::chrono::seconds(1830)), "reconnect expired at tolerance boundary");
    auto tampered = valid;
    tampered[tampered.rfind('.') + 1] = tampered[tampered.rfind('.') + 1] == 'A' ? 'B' : 'A';
    check(!dara::VerifyLogin(login(tampered), secret, now), "tampered signature");
    auto inverted = payload().set_expires_at(now - std::chrono::seconds(1)).sign(jwt::algorithm::hs256{secret});
    check(!dara::VerifyLogin(login(inverted), secret, now), "exp must follow iat");
    auto extreme = payload().set_payload_claim("exp", jwt::claim(picojson::value(INT64_MAX))).sign(jwt::algorithm::hs256{secret});
    check(!dara::VerifyLogin(login(extreme), secret, now), "timestamp overflow rejected");
    auto reasonCheck = [&](const std::string& message, const std::string& key,
                           jwt::date time, const char* expected) {
        std::string reason;
        auto verified = dara::VerifyLogin(message, key, time, &reason);
        check(!verified && reason.find(expected) != std::string::npos, "specific rejection reason");
        check(reason.find(valid) == std::string::npos && reason.find(secret) == std::string::npos,
              "diagnostics omit credentials");
    };
    reasonCheck(login(valid), "wrong", now, "signature verification failed");
    reasonCheck(login(valid), secret, now + std::chrono::seconds(1830), "JWT expired");
    reasonCheck(login(future), secret, now, "iat is in the future");
    reasonCheck(login(valid, "alice"), secret, now, "case-sensitive");
    reasonCheck(login("malformed"), secret, now, "Malformed JWT");
    reasonCheck("connect:name=Alice", secret, now, "Missing or empty JWT");
    reasonCheck(login(payload().set_issuer("wrong").sign(jwt::algorithm::hs256{secret})), secret, now, "iss");
    reasonCheck(login(payload().set_audience("wrong").sign(jwt::algorithm::hs256{secret})), secret, now, "aud");
    reasonCheck(login(payload().sign(jwt::algorithm::none{})), secret, now, "only HS256");
    std::string reason = "old failure";
    check(dara::VerifyLogin(login(valid), secret, now, &reason).has_value() && reason.empty(),
          "successful login clears diagnostic");
    std::cout << checks << " authentication checks passed\n";
}
