"""Small runtime-agnostic text editor core.

The core stores text as a list of lines and exposes editing operations that a
device shell can call from keyboard events. Keep this file boring. Boring is
good here; boring means the PicoCalc UI layer has fewer traps to invent.
"""


class TextBuffer:
    def __init__(self, text=""):
        self.lines = text.split("\n")
        if not self.lines:
            self.lines = [""]
        self.row = 0
        self.col = 0
        self.dirty = False

    @classmethod
    def from_file(cls, path):
        with open(path, "r", encoding="utf-8") as handle:
            return cls(handle.read())

    def save(self, path):
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(self.text())
        self.dirty = False

    def text(self):
        return "\n".join(self.lines)

    def line(self):
        return self.lines[self.row]

    def insert(self, value):
        if not value:
            return
        for char in value:
            if char == "\n":
                self.newline()
            else:
                current = self.line()
                self.lines[self.row] = current[: self.col] + char + current[self.col :]
                self.col += 1
                self.dirty = True

    def newline(self):
        current = self.line()
        self.lines[self.row] = current[: self.col]
        self.lines.insert(self.row + 1, current[self.col :])
        self.row += 1
        self.col = 0
        self.dirty = True

    def backspace(self):
        if self.col > 0:
            current = self.line()
            self.lines[self.row] = current[: self.col - 1] + current[self.col :]
            self.col -= 1
            self.dirty = True
            return
        if self.row > 0:
            previous_length = len(self.lines[self.row - 1])
            self.lines[self.row - 1] += self.lines.pop(self.row)
            self.row -= 1
            self.col = previous_length
            self.dirty = True

    def delete(self):
        current = self.line()
        if self.col < len(current):
            self.lines[self.row] = current[: self.col] + current[self.col + 1 :]
            self.dirty = True
            return
        if self.row < len(self.lines) - 1:
            self.lines[self.row] += self.lines.pop(self.row + 1)
            self.dirty = True

    def move_left(self):
        if self.col > 0:
            self.col -= 1
        elif self.row > 0:
            self.row -= 1
            self.col = len(self.line())

    def move_right(self):
        if self.col < len(self.line()):
            self.col += 1
        elif self.row < len(self.lines) - 1:
            self.row += 1
            self.col = 0

    def move_up(self):
        if self.row > 0:
            self.row -= 1
            self.col = min(self.col, len(self.line()))

    def move_down(self):
        if self.row < len(self.lines) - 1:
            self.row += 1
            self.col = min(self.col, len(self.line()))

    def move_home(self):
        self.col = 0

    def move_end(self):
        self.col = len(self.line())


class Viewport:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.top = 0
        self.left = 0

    def follow(self, buffer):
        if buffer.row < self.top:
            self.top = buffer.row
        elif buffer.row >= self.top + self.height:
            self.top = buffer.row - self.height + 1

        if buffer.col < self.left:
            self.left = buffer.col
        elif buffer.col >= self.left + self.width:
            self.left = buffer.col - self.width + 1

    def render(self, buffer):
        self.follow(buffer)
        rows = []
        for index in range(self.top, self.top + self.height):
            if index < len(buffer.lines):
                line = buffer.lines[index][self.left : self.left + self.width]
            else:
                line = ""
            rows.append(line.ljust(self.width))
        return rows

    def cursor(self, buffer):
        self.follow(buffer)
        return buffer.row - self.top, buffer.col - self.left

