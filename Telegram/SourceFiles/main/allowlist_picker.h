/* Allowgram chat picker state. No network, UI, persistence or profile secrets. */
#pragma once
#include "main/allowlist_policy.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>

namespace Main::Allowlist::Picker {
struct Cursor {
 int folder = 0;
 bool pinned = true;
 Entry peer;
 int message = 0;
 int date = 0;
 friend auto operator<=>(const Cursor &, const Cursor &) = default;
};
struct Dialog {
 Entry peer;
 int message = 0;
 int date = 0;
 std::string title;
 bool selectable = false;
};
struct Page { std::vector<Dialog> dialogs; bool complete = false; };

class Model {
public:
 std::uint64_t start() {
  ++_generation; _cursor={}; _candidates.clear(); _selected.clear();
  _seen.clear(); _pages=0; _state=State::Loading; return _generation;
 }
 void cancel() { ++_generation; _state=State::Failed; _selected.clear(); }
 void fail(std::uint64_t generation) {
  if (generation==_generation && _state==State::Loading) _state=State::Failed;
 }
 bool accept(std::uint64_t generation, const Page &page) {
  if (generation!=_generation || _state!=State::Loading) return false;
  if (++_pages>1000) { fail(generation); return false; }
  for (const auto &row : page.dialogs) {
   if (!row.selectable) { _candidates.erase(row.peer); _selected.erase(row.peer); continue; }
   if (!row.peer.id || row.peer.id>kMaximumBareId || row.title.empty()
     || (row.peer.kind!=Kind::User && row.peer.kind!=Kind::Chat && row.peer.kind!=Kind::Channel)) {
    fail(generation); return false;
   }
   _candidates[row.peer]=row.title;
   if (_candidates.size()>20000) { fail(generation); return false; }
  }
  if (_cursor.pinned) { _cursor.pinned=false; return true; }
  if (page.complete || page.dialogs.empty()) {
   if (_cursor.folder==0) { _cursor={}; _cursor.folder=1; }
   else _state=State::Ready;
   return true;
  }
  for (auto i=page.dialogs.rbegin(); i!=page.dialogs.rend(); ++i) {
   if (!i->peer.id || i->message<=0 || i->date<=0) continue;
   auto next=Cursor{_cursor.folder, false, i->peer, i->message, i->date};
   if (next==_cursor || !_seen.insert(next).second) break;
   _cursor=next; return true;
  }
  fail(generation); return false; // Never label an incomplete/stalled list ready.
 }
 bool choose(Entry peer, bool checked) {
  if (_state==State::Failed || !_candidates.contains(peer)) return false;
  if (checked) {
   if (_selected.size()>=kMaximumEntries && !_selected.contains(peer)) return false;
   _selected.insert(peer);
  } else _selected.erase(peer);
  return true;
 }
 [[nodiscard]] bool ready() const { return _state==State::Ready; }
 [[nodiscard]] bool failed() const { return _state==State::Failed; }
 [[nodiscard]] bool canSave() const { return ready() && !_selected.empty(); }
 [[nodiscard]] const Cursor &cursor() const { return _cursor; }
 [[nodiscard]] const auto &candidates() const { return _candidates; }
 [[nodiscard]] const auto &selected() const { return _selected; }
private:
 enum class State { Loading, Ready, Failed };
 State _state=State::Failed;
 Cursor _cursor;
 std::uint64_t _generation=0;
 int _pages=0;
 std::map<Entry, std::string> _candidates;
 std::set<Entry> _selected;
 std::set<Cursor> _seen;
};
} // namespace Main::Allowlist::Picker
