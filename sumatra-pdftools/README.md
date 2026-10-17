# SumatraPDF editing tools

Foxit-PDF-Editor-style editing commands added to SumatraPDF's **File** menu.

SumatraPDF is open source (GPLv3), so nothing needs reverse engineering, and nothing needs rebuilding:
SumatraPDF's own `ExternalViewers` setting adds menu entries that run a program on the open PDF
(`"%1"` = file, `%p` = current page). `pdftools.py` is that program. SumatraPDF reloads the file
when it changes, so you see each edit right away. It keeps working when you update SumatraPDF.

## What you get

SumatraPDF already has annotations (highlight, notes, ink, stamps, free text) and area redaction.
The pre-release builds also add form filling, merge, delete/extract pages, compress, encrypt and signing.
This adds the rest, and covers the pre-release-only features for users of the stable 3.6.1:

| Organize | Edit | Protect | Convert |
|---|---|---|---|
| Delete pages | Find and replace text | Search and redact text | OCR scanned pages |
| Extract pages to new PDF | Text watermark | Password-protect (AES-256) | Compress (downsample images) |
| Split into files of N pages | Page numbers / header / footer / Bates | Remove password | Convert to Word |
| Rotate pages (saved) | Place image or signature | Sanitize (metadata, scripts, attachments) | |
| Move pages, insert blank page, insert PDFs/images | | | **Undo last edit** |

Page lists use `1,3-5,8-` (`8-` = page 8 to the end). The current page is the default.

## Install (Windows)

1. Install Python 3 from python.org (Tk is included).
2. `pip install -r requirements.txt` (add `pdf2docx` for Convert to Word, and
   [Tesseract](https://github.com/UB-Mannheim/tesseract/wiki) for OCR).
3. `python pdftools.py --settings` prints an `ExternalViewers [...]` block with your paths filled in.
4. In SumatraPDF: **Settings > Advanced Options...**, replace the `ExternalViewers [ ]` line with that block, save.
   Restart SumatraPDF if the entries don't show up in the **File** menu.
   To give a command a hotkey, add a line like `Key = Ctrl+Shift+D` inside its `[ ]`.

## How edits are saved

- Organize, edit and convert commands overwrite the PDF and keep the previous version as `FILE.pdf.bak`.
  **PDF: Undo last edit** restores it (one level).
- Redact, password-protect and sanitize write a new file (`_redacted`, `_protected`, `_sanitized`)
  so no unprotected copy is left behind as a backup.
- Save your SumatraPDF annotations before running a command; the reload drops unsaved ones.

## Limits

- Find and replace draws the new text in Helvetica (Latin only) and shrinks it to fit; it does not reflow paragraphs.
- OCR re-renders scanned pages at 300 dpi. Use OCRmyPDF if you must keep the original image data.
- No digital (certificate) signatures: use SumatraPDF pre-release's Sign command.
- PyMuPDF is AGPL, pdf2docx is GPL; fine for personal use, read the licenses before redistributing.

## Test

`python -m unittest test_pdftools` (OCR and Word tests are skipped if Tesseract or pdf2docx are missing).
