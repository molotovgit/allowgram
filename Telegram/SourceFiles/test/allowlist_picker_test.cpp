#include "main/allowlist_picker.h"
#include <cassert>
#include <iostream>
using namespace Main::Allowlist;
using namespace Main::Allowlist::Picker;
int main() {
 Model m;
 const Entry user{Kind::User, 42}, chat{Kind::Chat, 42}, channel{Kind::Channel, 42};
 auto g = m.start();
 assert(!m.ready() && !m.choose(user, true));
 assert(m.cursor().pinned && m.cursor().folder == 0);
 assert(m.accept(g, {{{user, 10, 100, "Alice", true}}, true}));
 assert(!m.cursor().pinned && m.candidates().size() == 1);
 assert(m.choose(user, true) && !m.canSave());
 assert(m.accept(g, {{{user, 10, 100, "Alice", true}, {chat, 11, 101, "Work", true}}, false}));
 assert(m.cursor().peer == chat && m.cursor().message == 11 && m.cursor().date == 101);
 assert(m.candidates().size() == 2 && m.selected().contains(user));
 assert(m.accept(g, {{}, true}));
 assert(m.cursor().folder == 1 && m.cursor().pinned);
 assert(m.accept(g, {{{channel, 11, 80, "Archived pinned", true}}, true}));
 assert(m.accept(g, {{}, true}));
 assert(m.ready() && m.canSave() && m.candidates().size() == 3);
 assert(m.choose(channel, true) && m.choose(chat, true) && m.selected().size() == 3);
 assert(m.choose(user, false) && m.choose(chat, false) && m.choose(channel, false));
 assert(!m.canSave());
 assert(!m.choose({Kind::User, 999}, true));
 auto old = g; g = m.start();
 assert(!m.accept(old, {{{user, 10, 100, "Stale", true}}, true}));
 assert(m.candidates().empty());
 assert(m.accept(g, {{{user, 10, 100, "Deleted", false}}, true}));
 assert(m.candidates().empty());
 assert(m.accept(g, {{{chat, 11, 101, "Work", true}}, false}));
 assert(!m.accept(g, {{{chat, 11, 101, "Work", true}}, false}));
 assert(m.failed() && !m.canSave());
 g=m.start(); m.fail(g); assert(m.failed());
 auto next=m.start(); m.fail(g); assert(!m.failed());
 m.cancel(); assert(!m.accept(next, {{}, true}) && !m.canSave());
 // Retry is a new complete snapshot; old selections cannot smuggle missing peers.
 g=m.start(); assert(m.selected().empty());
 assert(m.accept(g, {{}, true})); assert(m.accept(g, {{}, true}));
 assert(m.accept(g, {{}, true})); assert(m.accept(g, {{}, true}));
 assert(m.ready() && m.candidates().empty() && !m.canSave());
 auto parsed=Parse("user:42", "chat:42\nchannel:42");
 assert(parsed.error==Error::None && parsed.entries.size()==3);
 Model limits;
 const auto lg=limits.start();
 Page many{{},true};
 for (std::size_t i=1;i<=kMaximumEntries+1;++i)
  many.dialogs.push_back({{Kind::User,i},int(i),int(i),"User",true});
 assert(limits.accept(lg,many)); assert(limits.accept(lg,{{},true}));
 assert(limits.accept(lg,{{},true})); assert(limits.accept(lg,{{},true}));
 for (std::size_t i=1;i<=kMaximumEntries;++i) assert(limits.choose({Kind::User,i},true));
 assert(!limits.choose({Kind::User,kMaximumEntries+1},true));
 assert(limits.choose({Kind::User,1},false));
 assert(limits.choose({Kind::User,kMaximumEntries+1},true));
 Model invalid; auto ig=invalid.start();
 assert(!invalid.accept(ig,{{{{Kind(99),1},1,1,"invalid kind",true}},true}));
 ig=invalid.start(); assert(!invalid.accept(ig,{{{{Kind::User,0},1,1,"zero",true}},true}));
 Model revoke; const auto rg=revoke.start();
 assert(revoke.accept(rg,{{{user,1,1,"User",true}},true}));
 assert(revoke.accept(rg,{{{user,1,1,"Removed",false}},true}));
 assert(revoke.candidates().empty());
 std::cout << "Picker state: pinned, archive, pagination, dedup, typed IDs, empty, stale, retry and failures PASS\n";
}
