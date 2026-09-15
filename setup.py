import sys

from setuptools import Extension, setup


setup(
    ext_modules=[
        Extension(
            "symphony.emulator._native",
            ["symphony/emulator/native_emulator.c"],
            extra_compile_args=["/O2"] if sys.platform == "win32" else ["-O3", "-march=native"],
            optional=True,
        ),
    ]
)
