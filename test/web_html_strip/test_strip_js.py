"""The web pages lose their whole-line // comments at build time, and nothing else."""
import ast
import pathlib
import unittest

SOURCE = pathlib.Path(__file__).resolve().parents[2] / 'scripts' / 'build_html.py'
tree = ast.parse(SOURCE.read_text())
code = ''.join(ast.get_source_segment(SOURCE.read_text(), node) + '\n' for node in tree.body
               if (isinstance(node, ast.FunctionDef) and node.name == 'strip_js_comment_lines')
               or (isinstance(node, ast.Assign) and node.targets[0].id.startswith('JS_REGEX')))
names = {}
exec(code, names)
strip = names['strip_js_comment_lines']


class StripJsCommentLines(unittest.TestCase):
    def test_comment_lines_go(self):
        self.assertEqual(strip('a();\n  // note\nb(); // tail stays\n'), 'a();\nb(); // tail stays\n')

    def test_strings_templates_and_regex_keep_their_slashes(self):
        js = ("const u = 'http://x';\nconst t = `line\n// inside a template\n${f({a: 1})}\n// still inside`;\n"
              "const r = /\\/\\//g;\nconst s = x.replace(/`/g, '');\n// gone\n")
        self.assertEqual(strip(js), js.replace('// gone\n', ''))

    def test_a_regex_after_a_condition_is_a_regex(self):
        js = "if (ok) /`/.test(s);\n// a comment\nconst t = `\n// template text\n`;\n"
        self.assertEqual(strip(js), js.replace('// a comment\n', ''))

    def test_an_unbalanced_scan_keeps_the_block(self):
        # A backtick the scanner cannot place (here inside a division it reads as a regex start) leaves it
        # inside a template at the end: the block is kept as it is.
        js = "x = a / b /`/;\n// a comment\n"
        self.assertEqual(strip(js), js)

    def test_two_regexes_after_conditions_keep_the_template(self):
        js = "if (ok) /`/.test(s);\nconst t = `\n// template text\n`;\nif (ok) /`/.test(s);\n// gone\n"
        self.assertEqual(strip(js), js.replace('// gone\n', ''))

    def test_a_slash_after_increment_divides(self):
        js = "let a = i++ / 2; const t = `\n// template text\n`;\n// gone\n"
        self.assertEqual(strip(js), js.replace('// gone\n', ''))

    def test_block_comments_hide_line_comments(self):
        self.assertEqual(strip('/*\n// in a block\n*/\nx();\n'), '/*\n// in a block\n*/\nx();\n')


if __name__ == '__main__':
    unittest.main()
