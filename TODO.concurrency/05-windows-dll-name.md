# 05 — Windows DLL naming

User report: the DLL is `leptris.dll` while every other platform
produces `libleptris.*`; vendoring tooling special-cases it.

- CMake: on WIN32, leptris_shared gets OUTPUT_NAME `libleptris`
  (elsewhere OUTPUT_NAME stays `leptris` → lib prefix does the job).

DONE 2026-08-23: conditional OUTPUT_NAME; Windows now produces
libleptris.dll. check_dll_exports.py updated to accept the new name.
