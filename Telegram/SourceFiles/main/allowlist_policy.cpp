/*
This file is part of Allowgram, a modification of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/allowlist_policy.h"

#include <charconv>
#include <set>

namespace Main::Allowlist {
namespace {

[[nodiscard]] bool IsSeparator(char value) {
	return std::string_view(" \t\r\n,;").find(value) != std::string_view::npos;
}

[[nodiscard]] Error ParseEntry(
		std::string_view token,
		bool users,
		Entry &entry) {
	auto kind = Kind::User;
	auto digits = token;
	auto marked = false;
	const auto colon = token.find(':');
	if (colon != std::string_view::npos) {
		const auto prefix = token.substr(0, colon);
		digits = token.substr(colon + 1);
		if (prefix == "user") {
			kind = Kind::User;
		} else if (prefix == "chat") {
			kind = Kind::Chat;
		} else if (prefix == "channel") {
			kind = Kind::Channel;
		} else {
			return Error::InvalidId;
		}
	} else if (!digits.empty() && digits.front() == '-') {
		marked = true;
		kind = Kind::Chat;
		digits.remove_prefix(1);
	}
	if (digits.empty() || digits.front() < '0' || digits.front() > '9') {
		return Error::InvalidId;
	}
	auto id = std::uint64_t(0);
	const auto parsed = std::from_chars(
		digits.data(),
		digits.data() + digits.size(),
		id);
	if (parsed.ec != std::errc()
		|| parsed.ptr != digits.data() + digits.size()
		|| !id) {
		return Error::InvalidId;
	}
	if (marked && id >= kChannelOffset) {
		kind = Kind::Channel;
		id -= kChannelOffset;
	}
	if (!id || id > kMaximumBareId) {
		return Error::InvalidId;
	}
	if (users != (kind == Kind::User)) {
		return Error::WrongField;
	}
	entry = { kind, id };
	return Error::None;
}

[[nodiscard]] ParseResult ParseField(
		std::string_view input,
		bool users,
		std::set<Entry> &entries) {
	auto offset = std::size_t(0);
	while (offset != input.size()) {
		if (IsSeparator(input[offset])) {
			++offset;
			continue;
		}
		const auto start = offset;
		while (offset != input.size() && !IsSeparator(input[offset])) {
			++offset;
		}
		const auto token = input.substr(start, offset - start);
		auto entry = Entry();
		const auto error = ParseEntry(token, users, entry);
		if (error != Error::None) {
			return { {}, error, std::string(token.substr(0, 80)) };
		}
		entries.insert(entry);
		if (entries.size() > kMaximumEntries) {
			return { {}, Error::TooMany, {} };
		}
	}
	return {};
}

} // namespace

bool CanPresentPeerProfile(Kind kind, bool conversationAllowed) {
	return kind != Kind::User && conversationAllowed;
}

bool CanStartUserSession(int authorizedAccounts) {
	return authorizedAccounts == 0;
}

bool ContainsEmoji(std::u32string_view text) {
	struct Range {
		char32_t first;
		char32_t last;
	};
	static constexpr Range ranges[] = {
#include "main/allowlist_emoji_ranges.inc"
	};
	for (const auto scalar : text) {
		for (const auto &range : ranges) {
			if (scalar < range.first) {
				break;
			} else if (scalar <= range.last) {
				return true;
			}
		}
	}
	return false;
}

ParseResult Parse(std::string_view users, std::string_view groups) {
	if (users.size() > kMaximumInputBytes
		|| groups.size() > kMaximumInputBytes
		|| users.size() + groups.size() > kMaximumInputBytes) {
		return { {}, Error::TooLong, {} };
	}
	auto entries = std::set<Entry>();
	for (const auto usersField : { true, false }) {
		auto result = ParseField(usersField ? users : groups, usersField, entries);
		if (result.error != Error::None) {
			return result;
		}
	}
	if (entries.empty()) {
		return { {}, Error::Empty, {} };
	}
	return { { entries.begin(), entries.end() }, Error::None, {} };
}

} // namespace Main::Allowlist
