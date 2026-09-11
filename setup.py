import sys

from setuptools import Extension, setup


setup(
    ext_modules=[
        Extension(
            "dynphony.emulator._native",
            ["dynphony/emulator/native_emulator.c"],
            extra_compile_args=["/O3"] if sys.platform == "win32" else ["-O3"],
            optional=True,
        )
    ]
)
