# SumatraPDF editing tools

Foxit-PDF-Editor-style editing commands added to SumatraPDF's **File** menu.

SumatraPDF is open source (GPLv3), so nothing needs reverse engineering, and nothing needs rebuilding:
SumatraPDF's own `ExternalViewers` setting adds menu entries that run a program on the open PDF
(`"%1"` = file, `%p` = current page). `pdftools.py` is that program. Each command opens one small dialog,
and SumatraPDF reloads the file when it changes, so you see each edit right away.

## What you get

SumatraPDF already has annotations (highlight, notes, ink, stamps, free text) and area redaction.
The pre-release builds also add form filling and signing. This adds the rest:

| Organize | Edit | Protect | Convert |
|---|---|---|---|
| Split: every N pages, page ranges, or top-level bookmarks | Find and replace text | Search and redact text | OCR scanned pages |
| Merge PDFs/images into a new file, in the order you choose | Edit paragraph: text, font, size, colour, align, justify, line spacing | Password-protect (AES-256) | Compress (downsample images) |
| Delete, extract, rotate, move pages | Add text box: font, size, colour, align, justify | Unlock: remove password and restrictions | Convert to Word |
| Insert blank page, insert PDFs/images | Watermark: add, edit, remove | Sanitize: metadata, scripts, attachments | |
| | Page numbers / header / footer / Bates: font, align | | |
| | Place image or signature | | **Undo, several steps** |

Page lists use `1,3-5,8-` (`8-` = page 8 to the end). The current page is the default.

**Fonts.** Font fields list Helvetica, Times and Courier (regular, bold, italic) plus every font in
`C:\Windows\Fonts`, or click `...` to pick any `.ttf`/`.otf`. Choose a font file such as Arial for non-Latin
text; Arabic is shaped and laid out right to left. "Match original" picks the standard font closest to the
text being replaced. Only the characters used are embedded.

**Watermarks** are stored as tagged watermark artifacts, so **Edit watermark** opens with the current settings
and **Remove watermark** takes them out completely. Remove also takes out other programs' watermarks when they
are tagged the standard way in their own content stream, and watermark annotations.

**Unlock** removes the open password (you must know it) and the print/copy/edit restrictions.

## Install (Windows)

1. Install Python 3 from python.org (Tk is included).
2. `pip install -r requirements.txt` (add `pdf2docx` for Convert to Word, and
   [Tesseract](https://github.com/UB-Mannheim/tesseract/wiki) for OCR).
3. `python pdftools.py --settings` prints an `ExternalViewers [...]` block with your paths filled in.
4. In SumatraPDF: **Settings > Advanced Options...**, replace the `ExternalViewers [ ]` line with that block, save.
   Restart SumatraPDF if the entries don't show up in the **File** menu.
   To give a command a hotkey, add a line like `Key = Ctrl+Shift+D` inside its `[ ]`.

## How edits are saved

- Edits overwrite the PDF. The previous 20 versions are kept in `FILE.pdf.undo\`.
  **PDF: Undo** lists them ("3 steps back: Add watermark (17 Oct 15:02)"). Pick how far back to go.
  An undo is itself a step, so you can undo it again to redo. Delete the folder to clear the history.
- Split, merge, extract, redact, password-protect, sanitize and Convert to Word write new files next to the PDF
  and leave it unchanged. Redacted and protected copies have no undo history, so no unprotected version is left behind.
- Save your SumatraPDF annotations before running a command; the reload drops unsaved ones.

## Limits

- Find and replace shrinks longer text to fit the old space instead of moving the rest of the line.
  Use Edit paragraph to re-flow a whole paragraph.
- Edit paragraph uses one font, size and colour for the whole paragraph. It lays the new text out inside the
  old paragraph's box and tells you if it had to shrink it.
- "Match original" uses the closest standard font, not the PDF's own embedded font.
- OCR re-renders scanned pages at 300 dpi. Use OCRmyPDF if you must keep the original image data.
- No digital (certificate) signatures: use SumatraPDF pre-release's Sign command.
- PyMuPDF is AGPL, pdf2docx is GPL; fine for personal use, read the licenses before redistributing.

## Test

`python -m unittest test_pdftools`. The dialog tests need a display; OCR and Word tests are skipped if
Tesseract or pdf2docx are missing.
