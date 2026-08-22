# Code Formatting & Line Hygiene Rules

## 1. Strict Blank Line Control
- Never leave multiple consecutive blank lines in any YAML, Python, C++, or Markdown file.
- Between workflow steps or functions, use at most **one single blank line**.
- Inside blocks or control statements, do not leave trailing or leading empty lines.

## 2. Line Ending and Replacement Hygiene
- Always maintain consistent line endings (`LF` for YAML/Python/shell, `CRLF` for Windows C++ sources).
- When editing files, verify that replacements do not introduce redundant `\r\n` or overlapping newlines around comments and indentation.
- Always perform a sanity check on the surrounding diff to ensure no spurious blank lines are created.
