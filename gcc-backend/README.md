# GCC dynphony backend (bootstrap, for comparison only)

A minimal GCC machine-description target for the dynphony/Symphony machine
(`dynphony.cc`, `dynphony.h`, `dynphony.md`, `dynphony.opt`,
`dynphony-protos.h`), built to compile the same example C programs with real
GCC (`-Os`/`-O2`) as a benchmark baseline for the dyncc optimizer roadmap
(see the `project-dyncc-optimizer-roadmap` memory).

This is not wired into dyncc's own build — it's a standalone GCC target
description meant to be dropped into a GCC source tree's `gcc/config/dynphony/`
and built as a cross-compiler, purely so dyncc's codegen/optimizer output can
be measured against GCC's on the same source files.
