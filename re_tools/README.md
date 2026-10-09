# Disassembly helpers

Small capstone + pyelftools scripts used to reverse-engineer the Kindle discovery
filter (FINDINGS.md section 4). Run with:

    uv run --with pyelftools --with capstone python -I re_tools/<script>.py <elf> <args>

| Script | Use |
|---|---|
| `dis.py` / `disarm.py` | Disassemble `.text` from an address (Thumb / ARM). Args: `<addr hex> <bytes decimal>` |
| `sym.py` / `symarm.py` | Disassemble an exported symbol (Thumb / ARM) |
| `xref.py` | Find code that references strings containing the given substrings (Thumb, `ldr`+`add pc`) |
| `callers.py` | Find Thumb call sites of a PLT stub by GOT address |
| `adcalls2.py` | Find ARM call sites of named PLT imports with preceding instructions |
| `pltname.py` | Resolve ARM PLT stub addresses to symbol names |

`acsbtfd` is Thumb; `libbluetooth.so` is ARM. Libraries not in `extracted/` were
pulled from `extracted_fw/rootfs.img` with `7zz e`.
