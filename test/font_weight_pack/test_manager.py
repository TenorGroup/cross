"""Compatibility entry point for the shared production manager regression suite."""
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'reader_ink_v108/test_manager.py'
spec = importlib.util.spec_from_file_location('reader_ink_manager', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
ManagerTest = module.ManagerTest

if __name__ == '__main__':
    unittest.main()
