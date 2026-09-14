import sys

from setuptools import Extension, setup


setup(
    ext_modules=[
        Extension(
            "symphony.emulator._native_dynphony",
            ["symphony/emulator/native_emulator.c"],
            extra_compile_args=["/O2"] if sys.platform == "win32" else ["-O3", "-march=native"],
            optional=True,
        ),
        Extension(
            "symphony.emulator._native_symphony",
            ["symphony/emulator/native_emulator.c"],
            define_macros=[("DYN_SYMPHONY", "1")],
            extra_compile_args=["/O2"] if sys.platform == "win32" else ["-O3", "-march=native"],
            optional=True,
        ),
    ]
)
