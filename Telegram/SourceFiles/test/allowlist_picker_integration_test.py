"""Source integration guards supplement the executable native picker fixture."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
class PickerIntegration(unittest.TestCase):
    def test_native_peer_rows_preserved(self):
        code = (ROOT / 'window/window_allowlist.cpp').read_text()
        self.assertIn('PeerListContent', code)
        self.assertIn('std::make_unique<PeerListRow>(peer)', code)
        self.assertIn('peerListSetRowChecked', code)
        self.assertIn('_model.choose', code)
    def test_session_owns_peer_list_lifetime(self):
        code = (ROOT / 'window/window_allowlist.cpp').read_text()
        self.assertIn('sessionChanges()', code)
        callback = code.split('sessionChanges()', 1)[1]
        self.assertLess(callback.index('clearRows()'), callback.index('_session = {}'))
        self.assertIn('clearRows()', code)
    def test_history_reload_preserved(self):
        code = (ROOT / 'data/data_session.cpp').read_text()
        body = code.split('void Session::refreshAllowlist()', 1)[1]
        self.assertIn('history->clear(History::ClearType::Unload)', body)
    def test_no_manual_or_sheet_override(self):
        code = (ROOT / 'window/window_allowlist.cpp').read_text()
        for fragment in ('CollectIds(', 'showManual', 'Sheet::', 'class IdRow'):
            self.assertNotIn(fragment, code)
if __name__ == '__main__':
    unittest.main()
