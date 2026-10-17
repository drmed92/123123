"""PDF editing commands for SumatraPDF, added to its File menu through the ExternalViewers setting.

Usage:
  pdftools.py --settings                 print the block to paste into SumatraPDF-settings.txt
  pdftools.py COMMAND FILE PAGE [ARG...] run a command; missing ARGs are asked for in a dialog
"""
import os
import shutil
import sys

import pymupdf

TITLE = 'SumatraPDF tools'


def ask(args, prompt, default='', secret=False):
    if args:
        return args.pop(0)
    from tkinter import Tk, simpledialog
    root = Tk()
    root.withdraw()
    value = simpledialog.askstring(TITLE, prompt, initialvalue=default, show='*' if secret else None, parent=root)
    root.destroy()
    if value is None:
        sys.exit(0)  # user pressed Cancel
    return value


def pick(args, title, many=False):
    if args:
        files = args[:] if many else args[:1]
        del args[:len(files)]
        return files
    from tkinter import Tk, filedialog
    root = Tk()
    root.withdraw()
    files = filedialog.askopenfilenames(title=title) if many else [filedialog.askopenfilename(title=title)]
    root.destroy()
    files = [f for f in files if f]
    if not files:
        sys.exit(0)
    return files


def show(msg, error=False):
    try:
        from tkinter import Tk, messagebox
        root = Tk()
        root.withdraw()
        (messagebox.showerror if error else messagebox.showinfo)(TITLE, msg, parent=root)
        root.destroy()
    except Exception:  # no Tk or no display: fall back to the console
        print(msg, file=sys.stderr if error else sys.stdout)


def parse_pages(spec, n):
    """'1,3-5,8-' -> 0-based page indexes, in the order given."""
    out = []
    for part in spec.replace(' ', '').split(','):
        a, dash, b = part.partition('-')
        lo, hi = int(a or 1), int(b or n) if dash else int(a)
        if not 1 <= lo <= hi <= n:
            raise ValueError(f'Bad page range "{part}" (document has {n} pages)')
        out += range(lo - 1, hi)
    return list(dict.fromkeys(out))


def new_name(path, suffix, ext='.pdf'):
    base = f'{os.path.splitext(path)[0]}_{suffix}'
    out, i = base + ext, 1
    while os.path.exists(out):  # never overwrite the user's files
        i += 1
        out = f'{base}_{i}{ext}'
    return out


def save(doc, path, **kw):
    """Overwrite the open PDF, keeping the previous version as FILE.bak for Undo."""
    tmp = path + '.tmp'
    doc.save(tmp, **{'garbage': 3, 'deflate': True, 'encryption': pymupdf.PDF_ENCRYPT_KEEP, **kw})
    doc.close()
    shutil.copy2(path, path + '.bak')
    # copy over instead of os.replace: SumatraPDF keeps large files open without FILE_SHARE_DELETE
    shutil.copyfile(tmp, path)
    os.remove(tmp)


# --- Organize ---------------------------------------------------------------

def delete_pages(doc, path, page, args):
    "Delete pages"
    drop = set(parse_pages(ask(args, 'Pages to delete (e.g. 2,4-6):', str(page + 1)), len(doc)))
    keep = [i for i in range(len(doc)) if i not in drop]
    if not keep:
        raise ValueError('Cannot delete every page')
    doc.select(keep)
    save(doc, path)


def extract_pages(doc, path, page, args):
    "Extract pages to new PDF"
    spec = ask(args, 'Pages to extract (e.g. 1-3,7):', str(page + 1))
    doc.select(parse_pages(spec, len(doc)))
    out = new_name(path, 'pages_' + spec.replace(' ', ''))
    doc.save(out, garbage=3, deflate=True)
    return f'Saved {out}'


def split(doc, path, page, args):
    "Split into files of N pages"
    n = int(ask(args, 'Pages per file:', '1'))
    if n < 1:
        raise ValueError('Pages per file must be at least 1')
    for part, start in enumerate(range(0, len(doc), n), 1):
        out = pymupdf.open()
        out.insert_pdf(doc, from_page=start, to_page=min(start + n, len(doc)) - 1)
        out.save(new_name(path, f'part{part}'), garbage=3, deflate=True)
    return f'Wrote {part} files next to {path}'


def rotate(doc, path, page, args):
    "Rotate pages (saved to file)"
    sel = parse_pages(ask(args, 'Pages to rotate (1- = all):', str(page + 1)), len(doc))
    angle = int(ask(args, 'Degrees clockwise (90, 180, 270):', '90'))
    if angle % 90:
        raise ValueError('Angle must be a multiple of 90')
    for i in sel:
        doc[i].set_rotation((doc[i].rotation + angle) % 360)
    save(doc, path)


def move_pages(doc, path, page, args):
    "Move pages"
    sel = parse_pages(ask(args, 'Pages to move:', str(page + 1)), len(doc))
    before = int(ask(args, f'Put them before page (1-{len(doc) + 1}, {len(doc) + 1} = end):', '1'))
    if not 1 <= before <= len(doc) + 1:
        raise ValueError(f'Target must be 1-{len(doc) + 1}')
    rest = [i for i in range(len(doc)) if i not in sel]
    k = sum(i < before - 1 for i in rest)
    doc.select(rest[:k] + sel + rest[k:])
    save(doc, path)


def insert_blank(doc, path, page, args):
    "Insert blank page after this one"
    r = doc[page].rect
    doc.new_page(page + 1, width=r.width, height=r.height)
    save(doc, path)


def insert_files(doc, path, page, args):
    "Insert PDFs or images after this page"
    at = page + 1
    for f in pick(args, 'PDFs or images to insert', many=True):
        n = len(doc)
        doc.insert_file(f, start_at=at)
        at += len(doc) - n
    save(doc, path)


# --- Edit -------------------------------------------------------------------

def replace_text(doc, path, page, args):
    "Find and replace text"
    old = ask(args, 'Find text (all pages):')
    new = ask(args, 'Replace with:')
    count = 0
    for p in doc:
        hits = p.search_for(old)
        if not hits:
            continue
        spans = [s for b in p.get_text('dict')['blocks'] for line in b.get('lines', []) for s in line['spans']]
        todo = []
        for r in hits:
            s = max(spans, key=lambda s: abs(pymupdf.Rect(s['bbox']) & r))  # span the hit sits in
            todo.append((pymupdf.Point(r.x0, s['origin'][1]), s['size'], r.width, pymupdf.sRGB_to_pdf(s['color'])))
            p.add_redact_annot(r, fill=False)
        p.apply_redactions(images=pymupdf.PDF_REDACT_IMAGE_NONE, graphics=pymupdf.PDF_REDACT_LINE_ART_NONE)
        for point, size, width, color in todo:
            if new:
                # shortcut: Helvetica (Latin only), shrunk to the old width instead of reflowing the paragraph
                size = min(size, size * width / pymupdf.get_text_length(new, fontsize=size))
                p.insert_text(point, new, fontsize=size, color=color)
        count += len(hits)
    if not count:
        return f'"{old}" not found'
    save(doc, path)


def watermark(doc, path, page, args):
    "Add text watermark"
    text = ask(args, 'Watermark text:', 'CONFIDENTIAL')
    for p in doc:
        size = p.rect.width * 1.1 / max(len(text), 1)
        w = pymupdf.get_text_length(text, fontsize=size)
        c = (p.rect.tl + p.rect.br) / 2
        # p.rect is the page as shown; insert_text wants unrotated coordinates
        p.insert_text((c - (w / 2, -size / 3)) * p.derotation_matrix, text, fontsize=size, rotate=p.rotation,
                      color=(0.5, 0.5, 0.5), fill_opacity=0.3, morph=(c * p.derotation_matrix, pymupdf.Matrix(45)))
    save(doc, path)


def page_numbers(doc, path, page, args):
    "Add page numbers / header / footer / Bates"
    fmt = ask(args, 'Text: {n} = page, {total} = page count, {n:06} = Bates number', 'Page {n} of {total}')
    top = ask(args, 'Position: top or bottom', 'bottom').strip().lower() == 'top'
    for i, p in enumerate(doc):
        text = fmt.format(n=i + 1, total=len(doc))
        w = pymupdf.get_text_length(text, fontsize=10)
        point = pymupdf.Point((p.rect.width - w) / 2, 30 if top else p.rect.height - 20)
        p.insert_text(point * p.derotation_matrix, text, fontsize=10, rotate=p.rotation)
    save(doc, path)


def place_image(doc, path, page, args):
    "Place image or signature on this page"
    img = pick(args, 'Image (PNG/JPG)')[0]
    box = ask(args, 'Box as left,top,right,bottom in % of the page:', '60,80,95,95')
    x0, y0, x1, y1 = (float(v) / 100 for v in box.split(','))
    r = doc[page].rect
    doc[page].insert_image(pymupdf.Rect(x0 * r.width, y0 * r.height, x1 * r.width, y1 * r.height), filename=img)
    save(doc, path)


# --- Protect (results go to a new file so no unprotected copy is left behind as .bak) ---

def redact_text(doc, path, page, args):
    "Search and redact text"
    text = ask(args, 'Text to black out on every page (removed, not just covered):')
    count = 0
    for p in doc:
        for r in p.search_for(text):
            p.add_redact_annot(r, fill=(0, 0, 0))
            count += 1
        p.apply_redactions()
    if not count:
        return f'"{text}" not found'
    out = new_name(path, 'redacted')
    doc.save(out, garbage=4, deflate=True)
    return f'Redacted {count} matches into {out}'


def encrypt(doc, path, page, args):
    "Password-protect (AES-256)"
    pw = ask(args, 'New password:', secret=True)
    if not pw or pw != ask(args, 'Repeat password:', secret=True):
        raise ValueError('Passwords are empty or differ')
    out = new_name(path, 'protected')
    doc.save(out, garbage=3, deflate=True, encryption=pymupdf.PDF_ENCRYPT_AES_256, owner_pw=pw, user_pw=pw)
    return f'Saved {out}'


def decrypt(doc, path, page, args):
    "Remove password"
    save(doc, path, encryption=pymupdf.PDF_ENCRYPT_NONE)


def sanitize(doc, path, page, args):
    "Sanitize (remove metadata, scripts, attachments)"
    doc.scrub()
    out = new_name(path, 'sanitized')
    doc.save(out, garbage=4, deflate=True)
    return f'Saved {out}'


# --- Convert / optimize -----------------------------------------------------

def ocr(doc, path, page, args):
    "OCR scanned pages (needs Tesseract)"
    lang = ask(args, 'Tesseract language(s), e.g. eng or eng+fra:', 'eng')
    done = 0
    for i in range(len(doc)):
        if doc[i].get_text().strip():
            continue  # page already has text
        # shortcut: re-renders the scanned page at 300 dpi; use OCRmyPDF to keep the original image bytes
        scan = pymupdf.open('pdf', doc[i].get_pixmap(dpi=300).pdfocr_tobytes(language=lang))
        doc.insert_pdf(scan, start_at=i)
        doc.delete_page(i + 1)
        done += 1
    if not done:
        return 'Every page already has text'
    save(doc, path)
    return f'Recognized text on {done} page(s)'


def compress(doc, path, page, args):
    "Compress (downsample images)"
    dpi = int(ask(args, 'Downsample images above this DPI:', '150'))
    before = os.path.getsize(path)
    doc.rewrite_images(dpi_threshold=dpi + 1, dpi_target=dpi, quality=75)
    save(doc, path, garbage=4, clean=True)
    return f'{before // 1024} KB -> {os.path.getsize(path) // 1024} KB (Undo restores the original)'


def to_word(doc, path, page, args):
    "Convert to Word (needs pdf2docx)"
    from pdf2docx import Converter
    out = new_name(path, 'converted', '.docx')
    doc.close()
    cv = Converter(path)
    cv.convert(out)
    cv.close()
    return f'Saved {out}'


def undo(doc, path, page, args):
    "Undo last edit"
    bak = path + '.bak'
    if not os.path.exists(bak):
        return 'Nothing to undo'
    shutil.copyfile(bak, path)
    os.remove(bak)


COMMANDS = {f.__name__.replace('_', '-'): f for f in [
    delete_pages, extract_pages, split, rotate, move_pages, insert_blank, insert_files,
    replace_text, watermark, page_numbers, place_image,
    redact_text, encrypt, decrypt, sanitize,
    ocr, compress, to_word, undo]}


def settings():
    exe = sys.executable
    if os.name == 'nt' and os.path.exists(os.path.join(os.path.dirname(exe), 'pythonw.exe')):
        exe = os.path.join(os.path.dirname(exe), 'pythonw.exe')  # no console window flashing up
    script = os.path.abspath(__file__)
    return 'ExternalViewers [\n' + ''.join(
        f'\t[\n\t\tCommandLine = "{exe}" "{script}" {name} "%1" %p\n'
        f'\t\tName = PDF: {f.__doc__}\n\t\tFilter = *.pdf\n\t]\n' for name, f in COMMANDS.items()) + ']'


def main(argv):
    if argv[1:] == ['--settings']:
        return settings()
    if len(argv) < 4 or argv[1] not in COMMANDS:
        raise ValueError(__doc__ + '\nCommands: ' + ', '.join(COMMANDS))
    name, path, page, *args = argv[1:]
    func = COMMANDS[name]
    if func is undo:  # must work even if the PDF no longer opens
        return undo(None, path, 0, args)
    doc = pymupdf.open(path)
    if doc.needs_pass and not doc.authenticate(ask(args, 'Password:', secret=True)):
        raise ValueError('Wrong password')
    return func(doc, path, min(max(int(page) - 1, 0), len(doc) - 1), args)


if __name__ == '__main__':
    try:
        msg = main(sys.argv)
    except Exception as e:
        show(f'{type(e).__name__}: {e}', error=True)
        sys.exit(1)
    if sys.argv[1:] == ['--settings']:
        print(msg)
    elif msg:
        show(msg)
