#include "main/allowlist_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace Main::Allowlist;
auto Checks = 0;

void Check(bool condition, const char *message) {
	++Checks;
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		std::exit(1);
	}
}

void CheckInvalid(std::string_view value, bool users, Error expected) {
	const auto result = Parse(users ? value : "", users ? "" : value);
	Check(result.error == expected, "Invalid input was accepted or misclassified");
	Check(result.entries.empty(), "Invalid input returned a partially allowed list");
}

} // namespace

int main() {
	using namespace Main::Allowlist;
	Check(!CanPresentPeerProfile(Kind::User, true),
		"Allowlisted member profile presentation accepted");
	Check(!CanPresentPeerProfile(Kind::User, false),
		"Nonallowlisted member profile presentation accepted");
	Check(CanPresentPeerProfile(Kind::Chat, true),
		"Allowed group information rejected");
	Check(CanPresentPeerProfile(Kind::Channel, true),
		"Allowed channel information rejected");
	Check(!CanPresentPeerProfile(Kind::Chat, false),
		"Denied group information accepted");
	const auto parsed = Parse(
		"123, user:456\n123; 000123",
		"-789 channel:42 -1000000000042 chat:789");
	Check(parsed.error == Error::None, "Valid mixed list rejected");
	Check(parsed.entries == std::vector<Entry>{
		{ Kind::User, 123 },
		{ Kind::User, 456 },
		{ Kind::Chat, 789 },
		{ Kind::Channel, 42 },
	}, "Typed IDs or deduplication incorrect");
	const auto multiline = Parse(
		"123\r\n456\r\n789",
		"-123\r\n-456\r\n-1000000000789");
	Check(multiline.error == Error::None, "Multiline fields rejected");
	Check(multiline.entries.size() == 6, "Multiline fields lost IDs");
	const auto collision = Parse("42", "chat:42 channel:42");
	Check(collision.entries.size() == 3, "Different peer types were collapsed");
	Check(Parse("", "-100123").entries == std::vector<Entry>{
		{ Kind::Chat, 100123 },
	}, "Negative basic-group ID was mistaken for a channel prefix");
	Check(Parse("", "-2000000000000").entries == std::vector<Entry>{
		{ Kind::Channel, 1000000000000ULL },
	}, "Large channel ID arithmetic incorrect");
	Check(Parse("281474976710655", "").error == Error::None,
		"Maximum supported ID rejected");
	Check(Parse("", "-282474976710655").error == Error::None,
		"Maximum supported marked channel ID rejected");
	CheckInvalid(" \r\n,;\t", true, Error::Empty);
	for (const auto value : {
		"0", "-0", "+1", "1.0", "1e4", "0x123", "12x", "user:",
		"user:-123", "user:+123", "user:0", "user:12:3", "USER:123",
		"@username", "https://t.me/test", "18446744073709551616",
		"281474976710656", "123\xE2\x80\x8B", "\xD9\xA1", "*",
	}) {
		CheckInvalid(value, true, Error::InvalidId);
	}
	for (const auto value : { "-1", "chat:1", "channel:1" }) {
		CheckInvalid(value, true, Error::WrongField);
	}
	for (const auto value : { "123", "user:123" }) {
		CheckInvalid(value, false, Error::WrongField);
	}
	for (const auto value : {
		"-1000000000000", "channel:0", "chat:0", "--123",
		"-282474976710656", "channel:281474976710656",
	}) {
		CheckInvalid(value, false, Error::InvalidId);
	}
	CheckInvalid("123,456,bad,789", true, Error::InvalidId);
	auto many = std::string();
	for (auto i = 1; i <= kMaximumEntries; ++i) {
		many += std::to_string(i) + '\n';
	}
	Check(Parse(many, "").entries.size() == kMaximumEntries,
		"Maximum list capacity rejected");
	Check(Parse(many, "chat:1").error == Error::TooMany,
		"Cross-field capacity not enforced");
	Check(Parse(many + "1\n", "").error == Error::None,
		"Duplicate at capacity incorrectly rejected");
	Check(Parse(std::string(kMaximumInputBytes + 1, ' '), "").error
		== Error::TooLong, "Oversized input accepted");
	Check(Parse(std::string(kMaximumInputBytes / 2 + 1, ' '),
		std::string(kMaximumInputBytes / 2, ' ')).error == Error::TooLong,
		"Combined size limit bypassed");
	std::cout << Checks << " allow-list parser checks passed.\n";
}
