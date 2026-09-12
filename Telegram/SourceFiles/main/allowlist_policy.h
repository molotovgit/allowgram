/*
This file is part of Allowgram, a modification of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Main::Allowlist {

inline constexpr auto kMaximumEntries = 10000;
inline constexpr auto kMaximumInputBytes = 512 * 1024;
inline constexpr auto kMaximumBareId = std::uint64_t(0xFFFFFFFFFFFFULL);
inline constexpr auto kChannelOffset = std::uint64_t(1000000000000ULL);

enum class Kind { User, Chat, Channel };
enum class Error { None, Empty, InvalidId, WrongField, TooMany, TooLong };

[[nodiscard]] bool CanPresentPeerProfile(Kind kind, bool conversationAllowed);
[[nodiscard]] bool CanStartUserSession(int authorizedAccounts);

struct Entry {
	Kind kind = Kind::User;
	std::uint64_t id = 0;

	friend auto operator<=>(const Entry &, const Entry &) = default;
};

struct ParseResult {
	std::vector<Entry> entries;
	Error error = Error::None;
	std::string token;
};

[[nodiscard]] ParseResult Parse(
	std::string_view users,
	std::string_view groups);

} // namespace Main::Allowlist
