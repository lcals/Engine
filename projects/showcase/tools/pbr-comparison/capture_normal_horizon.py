"""Convenience entry for the normal/ORM case in the current PBR capture suite.

Historical captures are preserved; every run now writes an independent manifest.
"""
from capture_cases import main

if __name__ == '__main__':
    main(['normal-orm'])
