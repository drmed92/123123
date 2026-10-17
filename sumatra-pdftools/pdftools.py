"""PDF editing commands for SumatraPDF, added to its File menu through the ExternalViewers setting.

Usage:
  pdftools.py --settings                 print the block to paste into SumatraPDF-settings.txt
  pdftools.py COMMAND FILE PAGE [ARG...] run a command; without ARGs it asks in a dialog
"""
import html
import json
import math
import os
import re
import shutil
import sys
import time

import pymupdf

TITLE = 'SumatraPDF tools'
MAX_UNDO = 20  # versions kept per PDF, in FILE.pdf.undo/
FONT_DIR = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts')
MATCH = 'Match original'
BASE14 = {'Helvetica': 'helv', 'Helvetica Bold': 'hebo', 'Helvetica Italic': 'heit', 'Helvetica Bold Italic': 'hebi',
          'Times': 'tiro', 'Times Bold': 'tibo', 'Times Italic': 'tiit', 'Times Bold Italic': 'tibi',
          'Courier': 'cour', 'Courier Bold': 'cobo', 'Courier Italic': 'coit', 'Courier Bold Italic': 'cobi'}
ALIGN = ['left', 'center', 'right', 'justify']
TEXT_ONLY = {'images': pymupdf.PDF_REDACT_IMAGE_NONE, 'graphics': pymupdf.PDF_REDACT_LINE_ART_NONE}
# a content stream holding nothing but a watermark artifact: ours, or another program's tagged the standard way
WATERMARK = re.compile(rb'\s*(q\s+)?/Artifact\s*<<[^>]*/Subtype\s*/Watermark\b.*EMC\s*(Q\s*)?', re.S)


# --- Dialogs (tkinter is imported only when a dialog is shown) --------------

def dialog(title, build):
    """Show a window with OK/Cancel; build(root, ok) adds the widgets and returns a function reading the result."""
    import tkinter as tk
    from tkinter import ttk
    root = tk.Tk()
    root.title(f'{title} - {TITLE}')
    root.attributes('-topmost', True)  # stay above SumatraPDF
    root.columnconfigure(1, weight=1)
    result = []

    def ok(*_):
        result.append(read())
        root.destroy()
    read = build(root, ok)
    bar = ttk.Frame(root)
    bar.grid(row=100, column=0, columnspan=3, sticky='e', padx=8, pady=8)
    ttk.Button(bar, text='OK', command=ok).pack(side='left', padx=4)
    ttk.Button(bar, text='Cancel', command=root.destroy).pack(side='left')
    root.bind('<Return>', lambda e: None if isinstance(e.widget, tk.Text) else ok())
    root.bind('<Escape>', lambda e: root.destroy())
    root.after(100, root.focus_force)  # take the keyboard from SumatraPDF
    root.mainloop()
    if not result:
        sys.exit(0)  # cancelled
    return result[0]


def form(args, title, *fields):
    """Ask for several values in one dialog. A field is (label, default) or (label, default, kind), kind being
    'secret', 'text' (multi-line), 'font', 'color' or a list of choices. Command-line and test runs pass the
    values as args instead, in field order."""
    if args:
        if len(args) < len(fields):
            raise ValueError(f'{title}: expected {len(fields)} values')
        values = args[:len(fields)]
        del args[:len(fields)]
        return values
    import tkinter as tk
    from tkinter import colorchooser, filedialog, ttk

    def build(root, ok):
        read = []
        for row, (label, default, *kind) in enumerate(fields):
            kind = kind[0] if kind else None
            ttk.Label(root, text=label).grid(row=row, column=0, sticky='ne', padx=8, pady=4)
            if kind == 'text':
                w = tk.Text(root, width=60, height=10, wrap='word', undo=True)
                w.insert('1.0', default)
                read.append(lambda w=w: w.get('1.0', 'end-1c'))
            else:
                var = tk.StringVar(root, default)
                if kind == 'font':
                    w = ttk.Combobox(root, textvariable=var, width=45,
                                     values=[MATCH] * (default == MATCH) + font_names())
                elif isinstance(kind, list):
                    w = ttk.Combobox(root, textvariable=var, width=45, values=kind, state='readonly')
                else:
                    w = ttk.Entry(root, textvariable=var, width=48, show='*' if kind == 'secret' else '')
                read.append(var.get)
                if kind in ('font', 'color'):
                    def browse(var=var, kind=kind):
                        value = (filedialog.askopenfilename(parent=root, filetypes=[('Fonts', '*.ttf *.otf *.ttc')])
                                 if kind == 'font' else colorchooser.askcolor(parent=root)[1])
                        if value:
                            var.set(value)
                    ttk.Button(root, text='...', width=3, command=browse).grid(row=row, column=2, padx=(0, 8))
            w.grid(row=row, column=1, sticky='we', pady=4)
            if row == 0:
                w.focus_set()
        return lambda: [r() for r in read]
    return dialog(title, build)


def choose(args, title, rows):
    """Pick one row from a list; returns its index. Command-line and test runs pass the row number (1 = first)."""
    if args:
        i = int(args.pop(0)) - 1
        if not 0 <= i < len(rows):
            raise ValueError(f'{title}: choose 1-{len(rows)}')
        return i
    import tkinter as tk
    from tkinter import ttk

    def build(root, ok):
        box = tk.Listbox(root, width=90, height=min(len(rows), 20))
        scroll = ttk.Scrollbar(root, command=box.yview)
        box.configure(yscrollcommand=scroll.set)
        box.insert('end', *rows)
        box.selection_set(0)
        box.grid(row=0, column=0, columnspan=2, padx=(8, 0), pady=8, sticky='nsew')
        scroll.grid(row=0, column=2, sticky='ns', pady=8, padx=(0, 8))
        box.bind('<Double-Button-1>', ok)
        box.focus_set()
        return lambda: (box.curselection() or (0,))[0]
    return dialog(title, build)


def arrange(title, items):
    """Let the user reorder or drop items; returns the new list."""
    import tkinter as tk
    from tkinter import ttk

    def build(root, ok):
        box = tk.Listbox(root, width=90, height=12)
        box.insert('end', *items)
        box.selection_set(0)
        box.grid(row=0, column=0, rowspan=3, columnspan=2, padx=8, pady=8, sticky='nsew')

        def move(step):
            for i in box.curselection():
                if 0 <= i + step < box.size():
                    value = box.get(i)
                    box.delete(i)
                    box.insert(i + step, value)
                    box.selection_set(i + step)
        ttk.Button(root, text='Up', command=lambda: move(-1)).grid(row=0, column=2, padx=8)
        ttk.Button(root, text='Down', command=lambda: move(1)).grid(row=1, column=2, padx=8)
        ttk.Button(root, text='Remove', command=lambda: [box.delete(i) for i in box.curselection()]).grid(
            row=2, column=2, padx=8)
        return lambda: list(box.get(0, 'end'))
    return dialog(title, build)


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


# --- Helpers ----------------------------------------------------------------

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


def page_box(page, spec):
    """'left,top,right,bottom' in % of the page as shown -> Rect in the page's unrotated coordinates."""
    x0, y0, x1, y1 = (float(v) / 100 for v in spec.split(','))
    r = page.rect
    box = pymupdf.Rect(x0 * r.width, y0 * r.height, x1 * r.width, y1 * r.height)
    if box.is_empty:
        raise ValueError(f'Empty box "{spec}"')
    return box * page.derotation_matrix


def font_names():
    files = sorted(os.listdir(FONT_DIR)) if os.path.isdir(FONT_DIR) else []
    return list(BASE14) + [f for f in files if f.lower().endswith(('.ttf', '.otf', '.ttc'))]


def similar_font(flags):
    """Closest standard font to extracted text (span flags: 2 italic, 4 serif, 8 monospace, 16 bold)."""
    family = 'Courier' if flags & 8 else 'Times' if flags & 4 else 'Helvetica'
    return ' '.join([family] + [style for style, bit in (('Bold', 16), ('Italic', 2)) if flags & bit])


def resolve_font(name, span=None):
    """Font chosen in a dialog -> a BASE14 name or a font file path."""
    if name == MATCH:
        return similar_font(span['flags']) if span else 'Helvetica'
    if name in BASE14:
        return name
    path = name if os.path.isfile(name) else os.path.join(FONT_DIR, name)
    if not os.path.isfile(path):
        raise ValueError(f'Font not found: {name}')
    return path


def html_style(font, size, color, align='left'):
    """CSS and font Archive for insert_htmlbox, for a font from resolve_font()."""
    if not re.fullmatch(r'#[0-9a-fA-F]{6}|[a-zA-Z]+', color):
        raise ValueError(f'Color must look like #ff0000 or red, not "{color}"')
    if float(size) <= 0:
        raise ValueError('Size must be more than 0')
    if font in BASE14:
        family, *style = font.split()
        rule, archive = '', None
        # MuPDF's built-in Times/Helvetica/Courier; the generic 'serif' would give Charis SIL instead
        face = (f'font-family: {family}; font-weight: {"bold" if "Bold" in style else "normal"}; '
                f'font-style: {"italic" if "Italic" in style else "normal"};')
    else:
        rule = f'@font-face {{font-family: chosen; src: url("{os.path.basename(font)}");}} '
        face, archive = 'font-family: chosen;', pymupdf.Archive(os.path.dirname(font))
    css = f'{rule}* {{{face} font-size: {size}pt; color: {color}; text-align: {align}; margin: 0; padding: 0}}'
    return css, archive


def html_text(text):
    return html.escape(text).replace('\n', '<br>')


def put_line(page, text, origin, width, font, size, color):
    """Write one line with its baseline at origin, shrunk to fit width (replaces text in place)."""
    measure = pymupdf.Font(BASE14[font]) if font in BASE14 else pymupdf.Font(fontfile=font)
    size = min(size, size * width / max(measure.text_length(text, fontsize=size), 0.01))
    css, archive = html_style(font, size, color)
    top = origin.y - 1 - 0.8 * size  # insert_htmlbox puts the first baseline 1pt + 0.8em below the box top
    box = pymupdf.Rect(origin.x - 1, top, origin.x + width + 2 + size, top + 2 + 1.5 * size)
    page.insert_htmlbox(box, html_text(text), css=css + ' * {white-space: nowrap; line-height: 1}', archive=archive)


def save(doc, path, subset=False, **kw):
    """Stage the edited PDF as FILE.tmp; main() swaps it in and keeps the previous version for Undo."""
    if subset:
        doc.subset_fonts()  # insert_htmlbox embeds whole font files
    doc.save(path + '.tmp', **{'garbage': 4, 'deflate': True, 'encryption': pymupdf.PDF_ENCRYPT_KEEP, **kw})


def history(path):
    """Saved earlier versions of path, newest first."""
    folder = path + '.undo'
    files = [f for f in os.listdir(folder) if re.fullmatch(r'\d+ [\w-]+\.pdf', f)] if os.path.isdir(folder) else []
    return [os.path.join(folder, f) for f in sorted(files, key=lambda f: -int(f.split()[0]))]


def commit(path, name):
    """Make FILE.tmp the new FILE, keeping the old one in FILE.undo/ as '<number> <command>.pdf'."""
    old = history(path)
    os.makedirs(path + '.undo', exist_ok=True)
    number = int(os.path.basename(old[0]).split()[0]) + 1 if old else 1
    shutil.copyfile(path, os.path.join(path + '.undo', f'{number:04d} {name}.pdf'))
    # copy over instead of os.replace: SumatraPDF keeps large files open without FILE_SHARE_DELETE
    shutil.copyfile(path + '.tmp', path)
    os.remove(path + '.tmp')
    for f in old[MAX_UNDO - 1:]:
        os.remove(f)


# --- Organize ---------------------------------------------------------------

def delete_pages(doc, path, page, args):
    "Delete pages"
    [spec] = form(args, 'Delete pages', ('Pages to delete (e.g. 2,4-6)', str(page + 1)))
    drop = set(parse_pages(spec, len(doc)))
    keep = [i for i in range(len(doc)) if i not in drop]
    if not keep:
        raise ValueError('Cannot delete every page')
    doc.select(keep)
    save(doc, path)


def extract_pages(doc, path, page, args):
    "Extract pages to new PDF"
    [spec] = form(args, 'Extract pages', ('Pages to extract (e.g. 1-3,7)', str(page + 1)))
    doc.select(parse_pages(spec, len(doc)))
    out = new_name(path, 'pages_' + spec.replace(' ', ''))
    doc.save(out, garbage=3, deflate=True)
    return f'Saved {out}'


def split(doc, path, page, args):
    "Split PDF"
    how, value = form(args, 'Split PDF',
                      ('Split by', 'every N pages', ['every N pages', 'page ranges', 'top-level bookmarks']),
                      ('Pages per file, or ranges like 1-3;4-9;10-', '1'))
    n = len(doc)
    if how == 'every N pages':
        step = int(value)
        if step < 1:
            raise ValueError('N must be at least 1')
        parts = [(f'part{k}', range(s, min(s + step, n))) for k, s in enumerate(range(0, n, step), 1)]
    elif how == 'page ranges':
        parts = [(f'pages_{r.strip()}', parse_pages(r, n)) for r in value.split(';') if r.strip()]
    else:
        starts = sorted((pno - 1, title) for level, title, pno, *_ in doc.get_toc() if level == 1 and pno > 0)
        if not starts:
            raise ValueError('This PDF has no bookmarks')
        if starts[0][0] > 0:
            starts.insert(0, (0, 'start'))
        ends = [s for s, _ in starts[1:]] + [n]
        parts = [(re.sub(r'[\\/:*?"<>|\s]+', '_', title).strip('_')[:60] or 'part', range(s, e))
                 for (s, title), e in zip(starts, ends) if e > s]
    for name, pages in parts:
        out = pymupdf.open()
        for i in pages:
            out.insert_pdf(doc, from_page=i, to_page=i)
        out.save(new_name(path, name), garbage=3, deflate=True)
    return f'Wrote {len(parts)} files next to {path}'


def merge(doc, path, page, args):
    "Merge PDFs into a new file"
    asked = not args
    files = [path] + pick(args, 'PDFs or images to merge with this one', many=True)
    if asked:
        files = arrange('Merge order (top = first)', files)
    out = pymupdf.open()
    for f in files:
        if f == path:
            out.insert_pdf(doc)  # the open document, already unlocked
        else:
            out.insert_file(f)
    if not len(out):
        raise ValueError('Nothing to merge')
    dest = new_name(path, 'merged')
    out.save(dest, garbage=3, deflate=True)
    return f'Saved {dest}'


def insert_files(doc, path, page, args):
    "Insert PDFs or images after this page"
    at = page + 1
    for f in pick(args, 'PDFs or images to insert', many=True):
        n = len(doc)
        doc.insert_file(f, start_at=at)
        at += len(doc) - n
    save(doc, path)


def rotate(doc, path, page, args):
    "Rotate pages (saved to file)"
    spec, angle = form(args, 'Rotate pages', ('Pages to rotate (1- = all)', str(page + 1)),
                       ('Degrees clockwise', '90', ['90', '180', '270']))
    for i in parse_pages(spec, len(doc)):
        doc[i].set_rotation((doc[i].rotation + int(angle)) % 360)
    save(doc, path)


def move_pages(doc, path, page, args):
    "Move pages"
    spec, before = form(args, 'Move pages', ('Pages to move', str(page + 1)),
                        (f'Put them before page ({len(doc) + 1} = at the end)', '1'))
    sel, before = parse_pages(spec, len(doc)), int(before)
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


# --- Edit -------------------------------------------------------------------

def replace_text(doc, path, page, args):
    "Find and replace text"
    old, new, font = form(args, 'Find and replace', ('Find (all pages)', ''), ('Replace with', ''),
                          ('Font', MATCH, 'font'))
    if not old:
        raise ValueError('Nothing to find')
    count = 0
    for p in doc:
        hits = p.search_for(old)
        if not hits:
            continue
        spans = [s for b in p.get_text('dict')['blocks'] for line in b.get('lines', []) for s in line['spans']]
        todo = [(r, max(spans, key=lambda s: abs(pymupdf.Rect(s['bbox']) & r))) for r in hits]  # span of each hit
        for r, _ in todo:
            p.add_redact_annot(r, fill=False)
        p.apply_redactions(**TEXT_ONLY)
        for r, s in todo:
            if new:
                put_line(p, new, pymupdf.Point(r.x0, s['origin'][1]), r.width, resolve_font(font, s), s['size'],
                         '#%06x' % s['color'])
        count += len(hits)
    if not count:
        return f'"{old}" not found'
    save(doc, path, subset=True)


def edit_paragraph(doc, path, page, args):
    "Edit paragraph (text, font, align, justify)"
    p = doc[page]
    found = [(b, ' '.join(''.join(s['text'] for s in line['spans']).strip() for line in b['lines']))
             for b in p.get_text('dict')['blocks'] if b.get('lines') and b['lines'][0]['dir'] == (1, 0)]
    found = [(b, t) for b, t in found if t.strip()]
    if not found:
        raise ValueError(f'No editable text on page {page + 1}')
    block, old = found[choose(args, f'Pick the paragraph to edit (page {page + 1})', [t[:150] for _, t in found])]
    span = block['lines'][0]['spans'][0]
    text, font, size, color, align, spacing = form(
        args, 'Edit paragraph', ('Text', old, 'text'), ('Font', MATCH, 'font'), ('Size', f"{span['size']:g}"),
        ('Color', '#%06x' % span['color'], 'color'), ('Align', 'left', ALIGN), ('Line spacing', '1.2'))
    css, archive = html_style(resolve_font(font, span), size, color, align)
    p.add_redact_annot(block['bbox'], fill=False)
    p.apply_redactions(**TEXT_ONLY)
    box = pymupdf.Rect(block['bbox']) + (-1, -1, 1, 1)  # insert_htmlbox indents 1pt
    _, scale = p.insert_htmlbox(box, html_text(text), css=css + f' * {{line-height: {float(spacing)}}}',
                                archive=archive)
    save(doc, path, subset=True)
    if scale < 1:
        return f'The text was shrunk to {scale:.0%} to fit the paragraph box'


def add_text(doc, path, page, args):
    "Add text box (font, align, justify)"
    text, font, size, color, align, box = form(
        args, 'Add text', ('Text', '', 'text'), ('Font', 'Helvetica', 'font'), ('Size', '12'),
        ('Color', '#000000', 'color'), ('Align', 'left', ALIGN),
        ('Box: left,top,right,bottom in % of the page', '10,10,90,30'))
    if not text.strip():
        raise ValueError('No text to add')
    p = doc[page]
    css, archive = html_style(resolve_font(font), size, color, align)
    _, scale = p.insert_htmlbox(page_box(p, box), html_text(text), css=css, archive=archive, rotate=p.rotation)
    save(doc, path, subset=True)
    if scale < 1:
        return f'The text was shrunk to {scale:.0%} to fit the box'


WATERMARK_FIELDS = [('Text', 'CONFIDENTIAL'), ('Font', 'Helvetica Bold', 'font'),
                    ('Size in pt (0 = fit the page)', '0'), ('Color', '#808080', 'color'), ('Opacity %', '30'),
                    ('Angle (degrees)', '45'),
                    ('Layer', 'on top', ['on top', 'behind page content']), ('Pages (1- = all)', '1-')]


def stamp_watermark(doc, values):
    """Draw the watermark as its own tagged content stream on each page, so it can be edited or removed later."""
    text, font, size, color, opacity, angle, layer, pages = values
    css, archive = html_style(resolve_font(font), 72, color, 'center')
    src = pymupdf.open()
    sp = src.new_page(width=5000, height=1000)
    # start lower than the page top: the font's ascender box reaches above the box, and the page would clip it
    sp.insert_htmlbox(pymupdf.Rect(0, 200, 5000, 1000), html_text(text), css=css + ' * {white-space: nowrap}',
                      archive=archive, opacity=float(opacity) / 100)
    box = pymupdf.Rect()
    for _, ink in sp.get_bboxlog():
        box |= ink  # centre on the visible glyphs, not on the font's line box
    if box.is_empty:
        raise ValueError('The watermark text is empty')
    box += (-2, -2, 2, 2)
    tag = b'/Artifact <</Subtype /Watermark /Type /Pagination /PdfTools <%s>>> BDC\n' % (
        json.dumps(values).encode().hex().encode())
    a = math.radians(float(angle))
    for i in parse_pages(pages, len(doc)):
        p = doc[i]
        full = p.rect * p.derotation_matrix  # show_pdf_page ignores the page's rotation
        if float(size) > 0:
            s = float(size) / 72
            w = s * (box.width * abs(math.cos(a)) + box.height * abs(math.sin(a)))
            h = s * (box.width * abs(math.sin(a)) + box.height * abs(math.cos(a)))
            c = (full.tl + full.br) / 2
            target = pymupdf.Rect(c.x - w / 2, c.y - h / 2, c.x + w / 2, c.y + h / 2)
        else:
            m = 0.05 * min(full.width, full.height)
            target = full + (m, m, -m, -m)
        before = set(p.get_contents())
        p.show_pdf_page(target, src, 0, clip=box, rotate=(float(angle) + p.rotation) % 360,
                        overlay=layer == 'on top')
        for x in p.get_contents():
            if x not in before:
                doc.update_stream(x, tag + doc.xref_stream(x) + b'\nEMC\n')


def drop_xobject(doc, page, name):
    """Delete /name from the page's /Resources /XObject. Indirect objects are followed by hand:
    xref_set_key paths that pass through one store a placeholder string instead of null."""
    holder, path = page.xref, ''
    for key in ('Resources', 'XObject'):
        kind, value = doc.xref_get_key(holder, path + key)
        if kind == 'xref':
            holder, path = int(value.split()[0]), ''
        elif kind == 'dict':
            path += key + '/'
        else:
            return  # inherited or missing: leave it alone
    doc.xref_set_key(holder, path + name, 'null')


def strip_watermarks(doc):
    """Remove watermark content streams and Watermark annotations.
    Returns (pages changed, settings of the first watermark this tool added, or None)."""
    pages, settings = 0, None
    for p in doc:
        contents, keep, names = p.get_contents(), [], set()
        for x in contents:
            data = doc.xref_stream(x)
            if WATERMARK.fullmatch(data):
                names.update(re.findall(rb'/([^\s/<>\[\]()%]+)\s+Do\b', data))
                m = re.search(rb'/PdfTools\s*<([0-9a-fA-F]+)>', data)
                settings = settings or (m and json.loads(bytes.fromhex(m.group(1).decode())))
            else:
                keep.append(x)
        annots = list(p.annots(types=[pymupdf.PDF_ANNOT_WATERMARK]))
        for a in annots:
            p.delete_annot(a)
        if len(keep) < len(contents):
            doc.xref_set_key(p.xref, 'Contents', '[%s]' % ' '.join(f'{x} 0 R' for x in keep))
            rest = b' '.join(doc.xref_stream(x) for x in keep)
            for name in names:
                if b'/' + name not in rest:
                    drop_xobject(doc, p, name.decode())
        pages += len(keep) < len(contents) or bool(annots)
    return pages, settings


def add_watermark(doc, path, page, args):
    "Add watermark"
    stamp_watermark(doc, form(args, 'Add watermark', *WATERMARK_FIELDS))
    save(doc, path, subset=True)


def edit_watermark(doc, path, page, args):
    "Edit watermark"
    pages, old = strip_watermarks(doc)
    if not pages:
        raise ValueError('No watermark found. Use Add watermark.')
    if not old or len(old) != len(WATERMARK_FIELDS):
        old = [f[1] for f in WATERMARK_FIELDS]  # watermark made by another program: start from the defaults
    fields = [(label, value, *kind) for (label, _, *kind), value in zip(WATERMARK_FIELDS, old)]
    stamp_watermark(doc, form(args, 'Edit watermark', *fields))
    save(doc, path, subset=True)


def remove_watermark(doc, path, page, args):
    "Remove watermark"
    pages, _ = strip_watermarks(doc)
    if not pages:
        return 'No watermark found'
    save(doc, path)
    return f'Removed the watermark from {pages} page(s)'


def page_numbers(doc, path, page, args):
    "Add page numbers / header / footer / Bates"
    text, where, align, font, size, color, pages = form(
        args, 'Page numbers, header, footer',
        ('Text: {n} = page, {total} = page count, {n:06} = Bates', 'Page {n} of {total}'),
        ('Position', 'bottom', ['top', 'bottom']), ('Align', 'center', ALIGN[:3]), ('Font', 'Helvetica', 'font'),
        ('Size', '10'), ('Color', '#000000', 'color'), ('Pages (1- = all)', '1-'))
    css, archive = html_style(resolve_font(font), size, color, align)
    height = 2 + 1.5 * float(size)
    for i in parse_pages(pages, len(doc)):
        p = doc[i]
        mx, my = min(36, p.rect.width / 10), min(24, p.rect.height / 20)  # tiny pages get smaller margins
        y = my if where == 'top' else p.rect.height - my - height
        band = pymupdf.Rect(mx, y, p.rect.width - mx, y + height) * p.derotation_matrix
        p.insert_htmlbox(band, html_text(text.format(n=i + 1, total=len(doc))), css=css, archive=archive,
                         rotate=p.rotation)
    save(doc, path, subset=True)


def place_image(doc, path, page, args):
    "Place image or signature on this page"
    img = pick(args, 'Image (PNG/JPG)')[0]
    [box] = form(args, 'Place image', ('Box: left,top,right,bottom in % of the page', '60,80,95,95'))
    p = doc[page]
    p.insert_image(page_box(p, box), filename=img, rotate=p.rotation)
    save(doc, path)


# --- Protect (redact, encrypt and sanitize write a new file, so no unprotected copy lands in the undo history) ---

def redact_text(doc, path, page, args):
    "Search and redact text"
    [text] = form(args, 'Redact', ('Text to black out on every page (removed, not just covered)', ''))
    if not text:
        raise ValueError('Nothing to redact')
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
    pw, again = form(args, 'Password-protect', ('New password', '', 'secret'), ('Repeat password', '', 'secret'))
    if not pw or pw != again:
        raise ValueError('Passwords are empty or differ')
    out = new_name(path, 'protected')
    doc.save(out, garbage=3, deflate=True, encryption=pymupdf.PDF_ENCRYPT_AES_256, owner_pw=pw, user_pw=pw)
    return f'Saved {out}'


def unlock(doc, path, page, args):
    "Unlock (remove password and restrictions)"
    if not doc.metadata.get('encryption'):
        return 'This PDF has no password or restrictions'
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
    [lang] = form(args, 'OCR', ('Tesseract language(s), e.g. eng or eng+fra', 'eng'))
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
    [dpi] = form(args, 'Compress', ('Downsample images above this DPI', '150'))
    doc.rewrite_images(dpi_threshold=int(dpi) + 1, dpi_target=int(dpi), quality=75)
    save(doc, path, clean=True)
    return f'{os.path.getsize(path) // 1024} KB -> {os.path.getsize(path + ".tmp") // 1024} KB (Undo restores it)'


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
    "Undo (choose how many steps)"
    versions = history(path)
    if not versions:
        return 'Nothing to undo'
    rows = []
    for k, f in enumerate(versions, 1):
        name = os.path.basename(f)[:-4].split(' ', 1)[1]
        what = 'the undo before it (redo)' if name == 'undo' else COMMANDS[name].__doc__ if name in COMMANDS else name
        when = time.strftime('%d %b %H:%M:%S', time.localtime(os.path.getmtime(f)))
        rows.append(f'{k} step{"s" if k > 1 else ""} back:  {what}  ({when})')
    k = choose(args, 'Undo: each row undoes that edit and every edit above it', rows)
    shutil.copyfile(versions[k], path + '.tmp')  # committed like any edit, so the undo can be undone too


COMMANDS = {f.__name__.replace('_', '-'): f for f in [
    delete_pages, extract_pages, split, merge, insert_files, rotate, move_pages, insert_blank,
    replace_text, edit_paragraph, add_text, add_watermark, edit_watermark, remove_watermark, page_numbers, place_image,
    redact_text, encrypt, unlock, sanitize,
    ocr, compress, to_word, undo]}


def settings():
    exe = sys.executable
    if os.name == 'nt' and os.path.exists(os.path.join(os.path.dirname(exe), 'pythonw.exe')):
        exe = os.path.join(os.path.dirname(exe), 'pythonw.exe')  # no console window flashing up
    # a PyInstaller exe is the whole program; otherwise run this script with Python
    run = f'"{exe}"' if getattr(sys, 'frozen', False) else f'"{exe}" "{os.path.abspath(__file__)}"'
    return 'ExternalViewers [\n' + ''.join(
        f'\t[\n\t\tCommandLine = {run} {name} "%1" %p\n'
        f'\t\tName = PDF: {f.__doc__}\n\t\tFilter = *.pdf\n\t]\n' for name, f in COMMANDS.items()) + ']'


def main(argv):
    if argv[1:] == ['--settings']:
        return settings()
    if len(argv) < 4 or argv[1] not in COMMANDS:
        raise ValueError(__doc__ + '\nCommands: ' + ', '.join(COMMANDS))
    name, path, page, *args = argv[1:]
    func, tmp = COMMANDS[name], path + '.tmp'
    if os.path.exists(tmp):
        os.remove(tmp)  # left over from a crash
    doc = None
    if func is not undo:  # undo must work even if the PDF no longer opens
        doc = pymupdf.open(path)
        if doc.needs_pass and not doc.authenticate(form(args, 'Password', ('Password', '', 'secret'))[0]):
            raise ValueError('Wrong password')
    try:
        msg = func(doc, path, min(max(int(page) - 1, 0), len(doc) - 1) if doc is not None else 0, args)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    finally:
        if doc is not None and not doc.is_closed:
            doc.close()
    if os.path.exists(tmp):
        commit(path, name)
    return msg


if __name__ == '__main__':
    if getattr(sys, 'frozen', False) and len(sys.argv) == 1:
        # double-clicked exe (no console): save the menu block next to it and say where
        out = os.path.join(os.path.dirname(sys.executable), 'SumatraPDF-menu.txt')
        with open(out, 'w', encoding='utf-8') as f:
            f.write(settings())
        show(f'Saved {out}\n\nIn SumatraPDF open Settings > Advanced Options, replace the line '
             f'"ExternalViewers [ ]" with the contents of that file, save, and restart SumatraPDF.')
        sys.exit(0)
    try:
        msg = main(sys.argv)
    except Exception as e:
        show(f'{type(e).__name__}: {e}', error=True)
        sys.exit(1)
    if sys.argv[1:] == ['--settings']:
        print(msg)
    elif msg:
        show(msg)
