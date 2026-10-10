"""Exercise the compiled UI translator and validate translated format arguments."""
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'components/muse/muse_i18n.c'


class MuseI18nTest(unittest.TestCase):
    def entries(self):
        literal = r'"(?:\\.|[^"\\])*"'
        return [(json.loads(a), json.loads(b)) for a, b in re.findall(
            r'\{\s*(' + literal + r'),\s*(' + literal + r')\s*\}', SOURCE.read_text())]

    def test_translated_formats_preserve_arguments(self):
        seen = set()
        conversion = re.compile(r'%(?:[-+#0]*\d*(?:\.\d+)?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcsp]|%)')
        for en, zh in self.entries():
            self.assertNotIn(en, seen)
            seen.add(en)
            self.assertTrue(zh)
            self.assertEqual(conversion.findall(en), conversion.findall(zh), en)
        self.assertIn('Password', seen)
        self.assertIn('SETTINGS', seen)
        self.assertIn('Reset pairing', seen)

    def test_runtime_switch_and_passthrough(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / 'esp_err.h').write_text('typedef int esp_err_t;\n')
            # A fake setting lets the real C implementation switch both ways.
            cases = '\n'.join('    check(' + json.dumps(en, ensure_ascii=False) + ', '
                              + json.dumps(zh, ensure_ascii=False) + ');' for en, zh in self.entries())
            (p / 'main.c').write_text('''#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "muse_i18n.h"
static bool chinese;
bool muse_settings_chinese(void) { return chinese; }
static void check(const char *en, const char *zh) {
    chinese = false; assert(strcmp(muse_tr(en), en) == 0);
    chinese = true; assert(strcmp(muse_tr(en), zh) == 0);
    chinese = false; assert(strcmp(muse_tr(en), en) == 0);
}
int main(void) {
''' + cases + '''
    check("Home-用户-123", "Home-用户-123");
    check("", "");
    return 0;
}
''')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(p),
                            '-I', str(SOURCE.parent), str(p / 'main.c'), str(SOURCE),
                            '-o', str(p / 'i18n-test')], check=True, capture_output=True)
            subprocess.run([str(p / 'i18n-test')], check=True, capture_output=True)
