# CPGE2026 Repository Instructions

These instructions apply to all work in this repository.

1. Never compile or build the code unless the user explicitly instructs you to do so. The user performs compilation manually by default.
2. Maintain clear structural indentation throughout the code, including preprocessor conditional directives and their contents.
3. Use only features supported by the C++17 standard.
4. Treat runtime performance, latency, memory use, and allocation cost as important design constraints because this is a gaming engine. Fully analyze performance implications before implementing changes, while preserving correctness and maintainability.
5. Perform a full analysis of the requested task and the relevant existing code before changing any code.
6. Add useful comments that explain the purpose and behavior of changed or newly written code, especially where intent, performance decisions, synchronization, ownership, or control flow are not immediately obvious. Avoid comments that merely repeat the code.
7. Consult the `Docs` folder for project help, subsystem usage, and examples relevant to the task.
8. Consult and work with applicable Claude.ai context stored in the `.claude` folder. Treat `.claude/settings.local.json` as configuration and permissions rather than architectural documentation unless additional memory files are added there.
9. Preserve existing project conventions and keep changes focused on the requested task.
10. Do not overwrite, revert, or discard unrelated user changes.
