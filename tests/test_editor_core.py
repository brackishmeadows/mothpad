import unittest

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from editor_core import TextBuffer, Viewport


class TextBufferTests(unittest.TestCase):
    def test_insert_and_newline(self):
        buffer = TextBuffer()

        buffer.insert("abc")
        buffer.newline()
        buffer.insert("de")

        self.assertEqual(buffer.text(), "abc\nde")
        self.assertEqual((buffer.row, buffer.col), (1, 2))
        self.assertTrue(buffer.dirty)

    def test_backspace_joins_lines(self):
        buffer = TextBuffer("abc\ndef")
        buffer.row = 1
        buffer.col = 0

        buffer.backspace()

        self.assertEqual(buffer.text(), "abcdef")
        self.assertEqual((buffer.row, buffer.col), (0, 3))

    def test_delete_joins_next_line(self):
        buffer = TextBuffer("abc\ndef")
        buffer.row = 0
        buffer.col = 3

        buffer.delete()

        self.assertEqual(buffer.text(), "abcdef")
        self.assertEqual((buffer.row, buffer.col), (0, 3))

    def test_vertical_movement_clamps_column(self):
        buffer = TextBuffer("abcdef\nxy")
        buffer.col = 6

        buffer.move_down()

        self.assertEqual((buffer.row, buffer.col), (1, 2))

    def test_viewport_follows_cursor(self):
        buffer = TextBuffer("one\ntwo\nthree\nfour")
        viewport = Viewport(5, 2)
        buffer.row = 3
        buffer.col = 4

        rows = viewport.render(buffer)

        self.assertEqual(rows, ["three", "four "])
        self.assertEqual(viewport.cursor(buffer), (1, 4))


if __name__ == "__main__":
    unittest.main()

