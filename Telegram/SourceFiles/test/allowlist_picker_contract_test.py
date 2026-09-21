"""Supplement native tests with invariants for the shipping source paths."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class PickerContract(unittest.TestCase):
 def test_no_sheet_or_test_switch_in_production_widget(self):
  code=(ROOT/'window/window_allowlist.cpp').read_text()
  for forbidden in ('Sheet::','_resolver','allowlist_sheet','showManual','qEnvironmentVariable','ALLOWGRAM_PICKER_REPORT'):
   self.assertNotIn(forbidden,code)
  self.assertIn('MTPmessages_GetPinnedDialogs',code)
  self.assertIn('MTPmessages_GetDialogs',code)
  self.assertIn('f_exclude_pinned',code)
 def test_only_explicit_selection_reaches_existing_verified_save(self):
  code=(ROOT/'window/window_allowlist.cpp').read_text()
  submit=code.split('void AllowlistLockWidget::submit() {',1)[1].split('\nvoid ',1)[0]
  self.assertIn('!sameSession() || !_model.canSave()',submit)
  self.assertIn('for (const auto id : _model.selected())',submit)
  self.assertIn('configureAllowlist(',submit)
  self.assertLess(submit.index('if (!error.isEmpty())'),submit.index('UnlockAllowlistWindows('))
 def test_existing_policy_and_verified_persistence_still_used(self):
  code=(ROOT/'main/main_session.cpp').read_text()
  body=code.split('QString Session::configureAllowlist(',1)[1]
  self.assertIn('if (allowlistConfigured())',body)
  self.assertIn('writeSessionSettingsVerified',body)
 def test_lock_is_still_installed_before_main_chat_list(self):
  code=(ROOT/'mainwindow.cpp').read_text()
  self.assertIn('if (!account().session().allowlistConfigured())',code)
  self.assertIn('_allowlistLock.create(bodyWidget(), &controller());',code)
 def test_account_switch_and_session_generation_guard(self):
  code=(ROOT/'window/window_allowlist.cpp').read_text()
  self.assertIn('sessionChanges()',code)
  self.assertIn('_api.reset(); _session = {}; _model.cancel();',code)
  self.assertIn('generation != _generation',code)
if __name__=='__main__':unittest.main()
