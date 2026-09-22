# Workspace scope

This checkout is the Basic product on `codex/fmcw-base`. Read WORKSPACE.md and docs/workspaces_ko.md before changing product scope or packaging.

- Keep work inside this product unless the user explicitly asks for cross-project changes.
- Preserve acquisition, FFT, live visualization, storage and RAW replay.
- Do not add CenterPoint inference, weights controls or cuDNN dependencies to this product.
- PCD file playback belongs to the separate PCDReplay product; keep its UI out of Basic.
- Share common fixes as focused commits; do not merge entire product branches together without explicit user instruction.
- Do not overwrite old packages or reuse another workspace's build cache. Use this workspace's named preset and package directory.
- Keep machine-specific SDK paths in ignored user presets or command-line arguments.
