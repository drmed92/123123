import glob
import importlib.util
import os
import shutil
import tempfile
import unittest
from unittest import mock

import pymupdf

import pdftools


def run(*argv):
    return pdftools.main(['pdftools.py', *argv])


def can_show_windows():
    try:
        import tkinter
        tkinter.Tk().destroy()
        return True
    except Exception:
        return False


HAS_TK = can_show_windows()
TTF = next(iter(glob.glob('/usr/share/fonts/**/DejaVuSans.ttf', recursive=True)
                + glob.glob(os.path.join(pdftools.FONT_DIR, 'arial.ttf'))), None)


def spans(page):
    return [s for b in page.get_text('dict')['blocks'] for line in b.get('lines', []) for s in line['spans']]


class PdfToolsTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.pdf = os.path.join(self.dir, 'doc.pdf')
        doc = pymupdf.open()
        for i in range(5):
            doc.new_page().insert_text((72, 100), f'Page {i + 1} invoice total 100 USD', fontsize=12)
        doc.save(self.pdf)

    def tearDown(self):
        shutil.rmtree(self.dir)

    def texts(self, path=None):
        with pymupdf.open(path or self.pdf) as doc:
            return [p.get_text() for p in doc]

    def page_count(self, path=None):
        with pymupdf.open(path or self.pdf) as doc:
            return len(doc)

    def test_parse_pages(self):
        self.assertEqual(pdftools.parse_pages('1,3-4,4-', 5), [0, 2, 3, 4])
        self.assertEqual(pdftools.parse_pages('3,1', 5), [2, 0])
        for bad in ['0', '6', '4-2', '']:
            self.assertRaises(ValueError, pdftools.parse_pages, bad, 5)

    def test_undo_several_steps_and_redo(self):
        self.assertEqual(run('undo', self.pdf, '1'), 'Nothing to undo')
        run('delete-pages', self.pdf, '1', '5')
        run('rotate', self.pdf, '1', '1', '90')
        run('insert-blank', self.pdf, '1')
        names = [os.path.basename(f) for f in pdftools.history(self.pdf)]
        self.assertEqual(names, ['0003 insert-blank.pdf', '0002 rotate.pdf', '0001 delete-pages.pdf'])
        run('undo', self.pdf, '1', '2')  # undo the blank page and the rotation
        with pymupdf.open(self.pdf) as doc:
            self.assertEqual((len(doc), doc[0].rotation), (4, 0))
        run('undo', self.pdf, '1', '1')  # undo the undo
        with pymupdf.open(self.pdf) as doc:
            self.assertEqual((len(doc), doc[0].rotation), (5, 90))
        self.assertFalse(os.path.exists(self.pdf + '.tmp'))

    def test_undo_keeps_max_versions_and_restores_broken_file(self):
        with mock.patch.object(pdftools, 'MAX_UNDO', 3):
            for _ in range(5):
                run('insert-blank', self.pdf, '1')
        self.assertEqual(len(pdftools.history(self.pdf)), 3)
        with open(self.pdf, 'wb') as f:
            f.write(b'not a pdf')
        run('undo', self.pdf, '1', '1')
        self.assertEqual(self.page_count(), 9)

    def test_failed_command_changes_nothing(self):
        self.assertRaises(ValueError, run, 'delete-pages', self.pdf, '1', '1-')
        self.assertEqual(self.page_count(), 5)
        self.assertEqual(pdftools.history(self.pdf), [])
        self.assertFalse(os.path.exists(self.pdf + '.tmp'))

    def test_extract_split_merge(self):
        run('extract-pages', self.pdf, '1', '4,2')
        self.assertEqual([t.split()[1] for t in self.texts(self.pdf[:-4] + '_pages_4,2.pdf')], ['4', '2'])
        run('split', self.pdf, '1', 'every N pages', '2')
        self.assertEqual([self.page_count(self.pdf[:-4] + f'_part{i}.pdf') for i in (1, 2, 3)], [2, 2, 1])
        run('split', self.pdf, '1', 'page ranges', '1-2; 5')
        self.assertEqual([self.page_count(self.pdf[:-4] + f'_pages_{r}.pdf') for r in ('1-2', '5')], [2, 1])
        with pymupdf.open(self.pdf) as doc:
            doc.set_toc([[1, 'Intro', 2], [2, 'Detail', 3], [1, 'End: part/2', 4]])
            doc.saveIncr()
        run('split', self.pdf, '1', 'top-level bookmarks', '')
        self.assertEqual([self.page_count(self.pdf[:-4] + f'_{n}.pdf') for n in ('start', 'Intro', 'End_part_2')],
                         [1, 2, 2])
        other = os.path.join(self.dir, 'other.pdf')
        shutil.copy(self.pdf, other)
        png = os.path.join(self.dir, 'img.png')
        pymupdf.Pixmap(pymupdf.csRGB, pymupdf.IRect(0, 0, 20, 20), False).save(png)
        self.assertIn('_merged.pdf', run('merge', self.pdf, '1', other, png))
        self.assertEqual(self.page_count(self.pdf[:-4] + '_merged.pdf'), 11)
        self.assertEqual(self.page_count(), 5)  # original untouched

    def test_rotate_move_blank_insert(self):
        run('rotate', self.pdf, '2', '2', '90')
        with pymupdf.open(self.pdf) as doc:
            self.assertEqual([p.rotation for p in doc], [0, 90, 0, 0, 0])
        run('move-pages', self.pdf, '1', '4-5', '1')
        self.assertEqual([t.split()[1] for t in self.texts()], ['4', '5', '1', '2', '3'])
        run('insert-blank', self.pdf, '1')
        self.assertEqual(self.texts()[1], '')
        png = os.path.join(self.dir, 'img.png')
        pymupdf.Pixmap(pymupdf.csRGB, pymupdf.IRect(0, 0, 20, 20), False).save(png)
        other = os.path.join(self.dir, 'other.pdf')
        shutil.copy(self.pdf, other)
        run('insert-files', self.pdf, '1', png, other)
        self.assertEqual(self.page_count(), 6 + 1 + 6)

    def test_replace_text_keeps_baseline_and_uses_chosen_font(self):
        with pymupdf.open(self.pdf) as doc:
            before = [s for s in spans(doc[0])][0]
        run('replace-text', self.pdf, '1', '100 USD', '250 EUR', pdftools.MATCH)
        with pymupdf.open(self.pdf) as doc:
            self.assertTrue(all('250 EUR' in p.get_text() and '100' not in p.get_text() for p in doc))
            new = next(s for s in spans(doc[0]) if s['text'].startswith('250'))
            self.assertAlmostEqual(new['origin'][1], before['origin'][1], delta=0.5)
            self.assertAlmostEqual(new['size'], 12, delta=0.1)
        run('replace-text', self.pdf, '1', 'invoice', 'receipt', 'Times Bold')
        with pymupdf.open(self.pdf) as doc:
            new = next(s for s in spans(doc[0]) if s['text'].startswith('receipt'))
            self.assertRegex(new['font'], r'Nimbus ?Roman.Bold')  # MuPDF's Times Bold, not Charis SIL
        self.assertEqual(run('replace-text', self.pdf, '1', 'nope', 'x', pdftools.MATCH), '"nope" not found')

    @unittest.skipUnless(TTF, 'no TrueType font found')
    def test_replace_text_with_font_file_any_script(self):
        run('replace-text', self.pdf, '1', 'invoice', 'фактура فاتورة', TTF)
        with pymupdf.open(self.pdf) as doc:
            new = next(s for s in spans(doc[0]) if s['text'].startswith('фактура'))
            self.assertEqual(new['font'], pymupdf.Font(fontfile=TTF).name)
        self.assertLess(os.path.getsize(self.pdf), 200_000)  # font was subset, not embedded whole

    def test_edit_paragraph_justify(self):
        doc = pymupdf.open()
        p = doc.new_page()
        p.insert_textbox(pymupdf.Rect(72, 72, 300, 200), 'Old paragraph text. ' * 8, fontsize=11)
        p.insert_text((72, 400), 'Other block stays', fontsize=11)
        doc.save(self.pdf)
        new = 'Fresh words that are long enough to wrap over several lines of this box. ' * 2
        run('edit-paragraph', self.pdf, '1', '1', new, 'Times', '11', '#ff0000', 'justify', '1.2')
        with pymupdf.open(self.pdf) as doc:
            text = doc[0].get_text()
            self.assertNotIn('Old paragraph', text)
            self.assertIn('Other block stays', text)
            lines = [line for b in doc[0].get_text('dict')['blocks'] for line in b.get('lines', [])
                     if line['spans'][0]['color'] == 0xff0000]
            right = [line['bbox'][2] for line in lines]
            self.assertGreater(len(lines), 2)
            self.assertTrue(all(abs(r - right[0]) < 2 for r in right[:-1]), right)  # justified edges

    def test_add_text_on_rotated_page_is_upright_inside_box(self):
        with pymupdf.open(self.pdf) as doc:
            doc[0].set_rotation(90)
            doc.saveIncr()
        run('add-text', self.pdf, '1', 'Hello box', 'Courier', '14', '#000000', 'center', '10,10,90,30')
        with pymupdf.open(self.pdf) as doc:
            p = doc[0]
            s = next(s for s in spans(p) if s['text'] == 'Hello box')
            shown = pymupdf.Rect(s['bbox']) * p.rotation_matrix
            box = pymupdf.Rect(0.1 * p.rect.width, 0.1 * p.rect.height, 0.9 * p.rect.width, 0.3 * p.rect.height)
            self.assertTrue(box.contains(shown) and shown.width > shown.height, (shown, box))
            self.assertAlmostEqual((shown.x0 + shown.x1) / 2, p.rect.width / 2, delta=2)  # centered

    def test_watermark_add_edit_remove(self):
        with pymupdf.open(self.pdf) as doc:
            doc[1].set_rotation(90)
            doc.saveIncr()
            streams = [len(p.get_contents()) for p in doc]
        run('add-watermark', self.pdf, '1', 'DRAFT', 'Helvetica Bold', '0', '#ff0000', '30', '45', 'on top', '1-')
        with pymupdf.open(self.pdf) as doc:
            self.assertTrue(all('DRAFT' in p.get_text() for p in doc))
            for p in doc[:2]:  # upright and rotated page: the visible ink sits in the middle
                ink = pymupdf.Rect(p.get_bboxlog()[-1][1]) * p.rotation_matrix
                self.assertAlmostEqual((ink.x0 + ink.x1) / 2, p.rect.width / 2, delta=1)
                self.assertAlmostEqual((ink.y0 + ink.y1) / 2, p.rect.height / 2, delta=1)
        run('edit-watermark', self.pdf, '1',
            'SECRET', 'Times', '40', '#0000ff', '50', '30', 'behind page content', '2-3')
        self.assertEqual(['DRAFT' in t or 'SECRET' in t for t in self.texts()], [False, True, True, False, False])
        run('remove-watermark', self.pdf, '1')
        with pymupdf.open(self.pdf) as doc:
            self.assertFalse(any('SECRET' in p.get_text() for p in doc))
            self.assertEqual([len(p.get_contents()) for p in doc], streams)
            self.assertEqual([p.get_xobjects() for p in doc], [[]] * 5)  # nothing hidden left behind
        self.assertEqual(run('remove-watermark', self.pdf, '1'), 'No watermark found')
        self.assertRaises(ValueError, run, 'edit-watermark', self.pdf, '1')

    def test_page_numbers_align_and_font(self):
        run('page-numbers', self.pdf, '1', 'BATES-{n:04} / {total}', 'bottom', 'right', 'Courier', '9', '#000000', '1-')
        with pymupdf.open(self.pdf) as doc:
            p = doc[4]
            s = next(s for s in spans(p) if s['text'].startswith('BATES'))
            self.assertEqual(s['text'], 'BATES-0005 / 5')
            self.assertAlmostEqual(s['bbox'][2], p.rect.width - 37, delta=2)
            self.assertGreater(s['bbox'][1], p.rect.height - 60)
            self.assertTrue(s['flags'] & 8)  # monospaced
        tiny = os.path.join(self.dir, 'tiny.pdf')
        with pymupdf.open() as doc:
            doc.new_page(width=72, height=72)
            doc.save(tiny)
        run('page-numbers', tiny, '1', '{n}', 'top', 'center', 'Helvetica', '8', '#000000', '1-')
        self.assertEqual(self.texts(tiny), ['1\n'])

    def test_place_image_upright_on_rotated_page(self):
        with pymupdf.open(self.pdf) as doc:
            doc[2].set_rotation(270)
            doc.saveIncr()
        png = os.path.join(self.dir, 'sig.png')
        pymupdf.Pixmap(pymupdf.csRGB, pymupdf.IRect(0, 0, 40, 20), False).save(png)
        run('place-image', self.pdf, '3', png, '60,80,95,95')
        with pymupdf.open(self.pdf) as doc:
            self.assertEqual([len(p.get_image_info()) for p in doc], [0, 0, 1, 0, 0])
            p = doc[2]
            shown = pymupdf.Rect(p.get_image_info()[0]['bbox']) * p.rotation_matrix
            self.assertGreater(shown.width, shown.height)  # 2:1 image stays landscape
            self.assertGreater(shown.y0, p.rect.height * 0.79)

    def test_redact(self):
        msg = run('redact-text', self.pdf, '1', 'invoice')
        out = self.pdf[:-4] + '_redacted.pdf'
        self.assertIn(out, msg)
        self.assertTrue(all('invoice' not in t and 'total' in t for t in self.texts(out)))
        self.assertIn('invoice', self.texts()[0])  # original untouched

    def test_encrypt_and_unlock(self):
        self.assertEqual(run('unlock', self.pdf, '1'), 'This PDF has no password or restrictions')
        self.assertRaises(ValueError, run, 'encrypt', self.pdf, '1', 'pw', 'typo')
        run('encrypt', self.pdf, '1', 'pw', 'pw')
        locked = self.pdf[:-4] + '_protected.pdf'
        self.assertTrue(pymupdf.open(locked).needs_pass)
        run('delete-pages', locked, '1', 'pw', '1')  # edits keep the password
        with pymupdf.open(locked) as doc:
            self.assertTrue(doc.needs_pass and doc.authenticate('pw') and len(doc) == 4)
        self.assertRaises(ValueError, run, 'unlock', locked, '1', 'wrong')
        run('unlock', locked, '1', 'pw')
        self.assertFalse(pymupdf.open(locked).needs_pass)

    def test_unlock_removes_restrictions(self):
        restricted = os.path.join(self.dir, 'restricted.pdf')
        with pymupdf.open(self.pdf) as doc:
            doc.save(restricted, encryption=pymupdf.PDF_ENCRYPT_AES_256, owner_pw='owner',
                     permissions=pymupdf.PDF_PERM_ACCESSIBILITY)
        with pymupdf.open(restricted) as doc:
            self.assertFalse(doc.permissions & pymupdf.PDF_PERM_PRINT)
        run('unlock', restricted, '1')
        with pymupdf.open(restricted) as doc:
            self.assertIsNone(doc.metadata['encryption'])
            self.assertTrue(doc.permissions & pymupdf.PDF_PERM_PRINT and doc.permissions & pymupdf.PDF_PERM_COPY)

    def test_sanitize_compress(self):
        with pymupdf.open(self.pdf) as doc:
            doc.set_metadata({'author': 'Secret Person'})
            doc.saveIncr()
        run('sanitize', self.pdf, '1')
        self.assertEqual(pymupdf.open(self.pdf[:-4] + '_sanitized.pdf').metadata['author'], '')
        with pymupdf.open(self.pdf) as doc:  # big image -> compressible
            pix = pymupdf.Pixmap(pymupdf.csRGB, pymupdf.IRect(0, 0, 1500, 1500), False)
            pix.set_rect(pix.irect, (200, 30, 30))
            doc[0].insert_image(pymupdf.Rect(0, 0, 100, 100), pixmap=pix)
            doc.saveIncr()
        self.assertIn('KB ->', run('compress', self.pdf, '1', '150'))
        with pymupdf.open(self.pdf) as doc:
            self.assertLess(doc.extract_image(doc[0].get_images()[0][0])['width'], 1500)

    @unittest.skipUnless(shutil.which('tesseract'), 'Tesseract not installed')
    def test_ocr(self):
        scan = os.path.join(self.dir, 'scan.pdf')
        with pymupdf.open(self.pdf) as src, pymupdf.open() as doc:
            doc.insert_file(pymupdf.open('png', src[0].get_pixmap(dpi=200).tobytes('png')))
            doc.save(scan)
        self.assertEqual(self.texts(scan), [''])
        run('ocr', scan, '1', 'eng')
        self.assertIn('invoice', self.texts(scan)[0])
        self.assertEqual(run('ocr', scan, '1', 'eng'), 'Every page already has text')

    @unittest.skipUnless(importlib.util.find_spec('pdf2docx'), 'pdf2docx not installed')
    def test_to_word(self):
        self.assertIn('doc_converted.docx', run('to-word', self.pdf, '1'))

    def test_settings_lists_every_command(self):
        out = run('--settings')
        for name in pdftools.COMMANDS:
            self.assertIn(f' {name} "%1" %p', out)


@unittest.skipUnless(HAS_TK, 'no tkinter or no display')
class DialogTest(unittest.TestCase):
    """Drives the real Tk dialogs: script(root) runs inside the dialog's event loop instead of a user."""

    def drive(self, script):
        import tkinter
        real = tkinter.Tk.mainloop

        def mainloop(root, n=0):
            root.after(50, lambda: script(root))
            real(root, n)
        return mock.patch.object(tkinter.Tk, 'mainloop', mainloop)

    def widgets(self, root, cls):
        found = []
        for w in root.winfo_children():
            found += [w] * isinstance(w, cls) + self.widgets(w, cls)
        return found

    def press(self, root, text):
        from tkinter import ttk
        next(b for b in self.widgets(root, ttk.Button) if b.cget('text') == text).invoke()

    def test_form(self):
        import tkinter
        from tkinter import ttk

        def script(root):
            entry, font, align, secret, color = self.widgets(root, ttk.Entry)  # Combobox is an Entry too
            entry.delete(0, 'end')
            entry.insert(0, 'typed')
            font.set('Times Bold')
            align.set('justify')
            secret.insert(0, 'pw')
            [text] = self.widgets(root, tkinter.Text)
            text.delete('1.0', 'end')
            text.insert('1.0', 'two\nlines')
            self.assertIn(pdftools.MATCH, font.cget('values'))
            self.press(root, 'OK')
        with self.drive(script):
            values = pdftools.form([], 'Test', ('A', 'x'), ('Font', pdftools.MATCH, 'font'),
                                   ('Align', 'left', pdftools.ALIGN), ('Text', 'old', 'text'), ('Pw', '', 'secret'),
                                   ('Color', '#000000', 'color'))
        self.assertEqual(values, ['typed', 'Times Bold', 'justify', 'two\nlines', 'pw', '#000000'])

    def test_cancel_exits_without_changes(self):
        with self.drive(lambda root: self.press(root, 'Cancel')), self.assertRaises(SystemExit):
            pdftools.form([], 'Test', ('A', 'x'))

    def test_choose_and_arrange(self):
        import tkinter

        def pick_third(root):
            [box] = self.widgets(root, tkinter.Listbox)
            box.selection_clear(0, 'end')
            box.selection_set(2)
            self.press(root, 'OK')
        with self.drive(pick_third):
            self.assertEqual(pdftools.choose([], 'Test', ['a', 'b', 'c']), 2)

        def reorder(root):
            [box] = self.widgets(root, tkinter.Listbox)
            box.selection_clear(0, 'end')
            box.selection_set(2)
            self.press(root, 'Up')
            self.press(root, 'Up')
            box.selection_clear(0, 'end')
            box.selection_set(2)
            self.press(root, 'Remove')
            self.press(root, 'OK')
        with self.drive(reorder):
            self.assertEqual(pdftools.arrange('Test', ['a', 'b', 'c']), ['c', 'a'])

    def test_commands_open_their_dialogs(self):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        pdf = os.path.join(tmp, 'doc.pdf')
        with pymupdf.open() as doc:
            doc.new_page().insert_text((72, 100), 'Hello paragraph', fontsize=12)
            doc.save(pdf)
        with self.drive(lambda root: self.press(root, 'OK')):  # accept the defaults
            run('add-watermark', pdf, '1')
            run('edit-watermark', pdf, '1')
            run('page-numbers', pdf, '1')
            run('edit-paragraph', pdf, '1')  # pick the paragraph
        self.assertEqual(len(pdftools.history(pdf)), 4)


if __name__ == '__main__':
    unittest.main()
