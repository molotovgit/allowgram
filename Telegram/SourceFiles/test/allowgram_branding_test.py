"""Source-contract checks complement the actual native branding fixture."""
from pathlib import Path
import unittest

SOURCE = Path(__file__).resolve().parents[1]


class BrandingContractTest(unittest.TestCase):
    def test_generic_window_titles_use_allowgram(self):
        for name in ("main_window.cpp", "window_restore_shell.cpp", "window_saved_windows.cpp"):
            with self.subTest(source=name):
                text = (SOURCE / "window" / name).read_text()
                self.assertNotIn('u"Telegram"_q', text)
                self.assertIn('u"Allowgram"_q', text)

    def test_language_restart_uses_runtime_branding(self):
        text = (SOURCE / "lang/lang_instance.cpp").read_text()
        hook = text.split("QString BrandValue(", 1)[1].split("std::vector<QString> PrepareDefaultValues()", 1)[0]
        self.assertIn("key == tr::lng_sure_save_language.base", hook)


if __name__ == "__main__":
    unittest.main()
