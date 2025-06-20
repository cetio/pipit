# Pipit

> [!TIP]
> Pipit is still under development, if you would like to contribute, view the guidelines:
> 
> 👉 [CONTRIBUTE](CONTRIBUTING.md)

Exceedingly fast terminal editor built from the ground up in C for reliability, performance, and minimal overhead. 
Pipit uses hashmaps written specifically for the editor leveraging SIMD for batch processing, bulk data processing and virtual buffers for rendering with minimal overhead, using the heap as little as possible to prevent memory pressure,
and rapidhash for incredibly efficient backend processing.

**No bloat. No hiccups. Everything is guaranteed.**

## Why?

Editors like *VIM*, *Vi*, *NVim*, *EMacs*, etc. are incredibly efficient, well designed, and capable, no doubt.
The problem arises with the complexity of editors, as programmers have a nasty habit of developing tools with complex learning curves and frankly, too much happening, the human race likely began its downfall after whatever editor was created after Nano.
*Micro* is a common solution to this problem, offering a level of simplicity akin to Nano but with significantly more configurability and settings, but is that really what we need?

Pipit aims to solve this problem by being incredibly efficient, prematurely optimized by design, and incredibly tight, minimizing the actual surface and complexity of the editor.
Even Windows or MacOS users should be able to understand how to use it, and not be put off by the difference from GUI editors, while gaining immense performance increases.

- Ultra‑low latency real‑time editing, even on huge files
- Familiar, minimal command set similar to nano—no steep learning curve
- Written entirely in C with testing and development suites in D
- Unnecessarily quick processing and management backend
- **ZERO** dependencies

Built for rapid iteration, extensibility, and it doesn't use Lua anywhere.

## 🚧 Roadmap

Currently, the biggest hurdle with the project is the Myzomela allocator and implementing sequences which will be null characters in the buffer used to reference certain actions, such as text insertion, deletion, and things like tabs which act as special multi-character sequences.

- [ ] Myzomela arbitrage and full implementation
- [ ] D or C# based extensible plugin integration and sandboxing
- [ ] Syntax trees
- [ ] Proper build system using Make
- [ ] Line structures should be stored alongside buffers
- [ ] Tabs
- [ ] File and directory viewer
- [ ] Rendering buffers should heuristically be chosen to be saved or killed per-buffer
- [ ] Documentation
- [ ] Dynamic file retrieval (for if modified by external sources)
- [ ] Sequences
- [ ] Improve the rendering system
- [ ] Standardization
