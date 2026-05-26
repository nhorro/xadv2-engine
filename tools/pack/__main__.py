"""Allow `python -m tools.pack <root> <output>`."""
import sys

from .pack import main

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
