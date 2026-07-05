"""Crude terminal poke harness for the editor core.

This is not the target UI. It is just a fast way to prove the core moves before
we bolt it to PicoCalc keyboard and display code.
"""

from editor_core import TextBuffer, Viewport


def main():
    buffer = TextBuffer("PicoCalc text editor\nfirst scratch line")
    viewport = Viewport(40, 8)

    print("Commands: text inserts, /left /right /up /down /bs /del /nl /q")
    while True:
        print("")
        for row in viewport.render(buffer):
            print("|" + row + "|")
        cursor_row, cursor_col = viewport.cursor(buffer)
        print("cursor:", cursor_row, cursor_col, "dirty:", buffer.dirty)

        command = input("> ")
        if command == "/q":
            break
        if command == "/left":
            buffer.move_left()
        elif command == "/right":
            buffer.move_right()
        elif command == "/up":
            buffer.move_up()
        elif command == "/down":
            buffer.move_down()
        elif command == "/bs":
            buffer.backspace()
        elif command == "/del":
            buffer.delete()
        elif command == "/nl":
            buffer.newline()
        else:
            buffer.insert(command)


if __name__ == "__main__":
    main()

