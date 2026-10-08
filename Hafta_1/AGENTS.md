## Text encoding

- Read and write text files with an explicit UTF-8 encoding. Never use the
  Windows locale default, including Python `read_text()` / `write_text()`
  without `encoding="utf-8"`.
- Write UTF-8 without BOM and preserve each existing file's line endings.
  Encoding and CRLF/LF are separate settings.
- Preserve Turkish characters. Do not replace them with ASCII equivalents.
- After changing text files, run `python scripts/check_text_encoding.py <files>`
  on the changed files. Strict UTF-8 decoding alone is insufficient: incorrectly
  decoded and re-encoded text can still be valid UTF-8.
