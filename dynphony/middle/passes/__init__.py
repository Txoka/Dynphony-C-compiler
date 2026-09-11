"""Optimization passes over the common IR."""

from .pipeline import optimize
from .manager import FixedPointPassManager

__all__ = ["FixedPointPassManager", "optimize"]
