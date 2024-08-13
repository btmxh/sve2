import readline
import sys

readline.parse_and_bind("tab: complete")
readline.parse_and_bind("set editing-mode vi")

dest = None

if len(sys.argv) == 2:
    dest = open(sys.argv[1], "w")

while True:
    try:
        line = input("sve2> ")
        if dest is None:
            print(line)
        else:
            _ = dest.write(f"{line}\n")
            dest.flush()
    except EOFError:
        print()
        break

if dest is not None:
    dest.close()
