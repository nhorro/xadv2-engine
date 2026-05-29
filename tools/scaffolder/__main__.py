"""Allow `python -m tools.scaffolder`."""
from .scaffold import main

if __name__ == "__main__":
    raise SystemExit(main())
