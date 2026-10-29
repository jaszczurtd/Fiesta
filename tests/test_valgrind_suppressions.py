"""Every Valgrind suppression is written once per module."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ValgrindSuppressionTests(unittest.TestCase):
    def test_each_module_names_every_suppression_once(self):
        modules = sorted({path.parents[1] for path in ROOT.glob("src/*/tests/*.supp")})
        self.assertTrue(modules)
        for module in modules:
            seen = {}
            for path in sorted(module.glob("tests/*.supp")):
                text = path.read_text(encoding="utf-8")
                for name in re.findall(r"\{\s*\n\s*(\S+)\s*\n", text):
                    self.assertNotIn(name, seen, f"{path.name} repeats {name} "
                                     f"from {seen.get(name)}")
                    seen[name] = path.name


if __name__ == "__main__":
    unittest.main()
