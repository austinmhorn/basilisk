# Inter font assets

Basilisk uses the static TrueType fonts from [Inter 4.1](https://github.com/rsms/inter/releases/tag/v4.1),
published by the official Inter project. CMake downloads the release archive
with a pinned SHA-256 digest and packages the Regular, Medium, SemiBold, and
Bold faces with `BasiliskGame`.

Inter is licensed under the SIL Open Font License 1.1. The release's
`LICENSE.txt` is packaged beside the fonts as `Inter-LICENSE.txt` for native
and WebAssembly builds.
