import importlib.util
import os
import shutil
import tempfile
import unittest

import pymupdf

import pdftools


def run(*argv):
    return pdftools.main(['pdftools.py', *argv])


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

    def test_parse_pages(self):
        self.assertEqual(pdftools.parse_pages('1,3-4,4-', 5), [0, 2, 3, 4])
        self.assertEqual(pdftools.parse_pages('3,1', 5), [2, 0])
        for bad in ['0', '6', '4-2', '']:
            self.assertRaises(ValueError, pdftools.parse_pages, bad, 5)

    def test_delete_then_undo(self):
        run('delete-pages', self.pdf, '1', '2,4-5')
        self.assertEqual([t.split()[1] for t in self.texts()], ['1', '3'])
        run('undo', self.pdf, '1')
        self.assertEqual(len(self.texts()), 5)
        self.assertEqual(run('undo', self.pdf, '1'), 'Nothing to undo')
        self.assertRaises(ValueError, run, 'delete-pages', self.pdf, '1', '1-')

    def test_extract_and_split(self):
        run('extract-pages', self.pdf, '1', '4,2')
        self.assertEqual([t.split()[1] for t in self.texts(self.pdf[:-4] + '_pages_4,2.pdf')], ['4', '2'])
        run('split', self.pdf, '1', '2')
        self.assertEqual([len(self.texts(self.pdf[:-4] + f'_part{i}.pdf')) for i in (1, 2, 3)], [2, 2, 1])
        self.assertEqual(len(self.texts()), 5)  # original untouched

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
        self.assertEqual(len(self.texts()), 6 + 1 + 6)

    def test_replace_text(self):
        run('replace-text', self.pdf, '1', '100 USD', '250 EUR')
        texts = self.texts()
        self.assertTrue(all('250 EUR' in t and '100' not in t and 'invoice total' in t for t in texts), texts)
        self.assertEqual(run('replace-text', self.pdf, '1', 'nope', 'x'), '"nope" not found')

    def test_watermark_numbers_image(self):
        run('watermark', self.pdf, '1', 'DRAFT')
        run('page-numbers', self.pdf, '1', 'BATES-{n:04} / {total}', 'bottom')
        png = os.path.join(self.dir, 'sig.png')
        pymupdf.Pixmap(pymupdf.csRGB, pymupdf.IRect(0, 0, 40, 20), False).save(png)
        run('place-image', self.pdf, '3', png, '60,80,95,95')
        texts = self.texts()
        self.assertIn('DRAFT', texts[0])
        self.assertIn('BATES-0005 / 5', texts[4])
        with pymupdf.open(self.pdf) as doc:
            self.assertEqual([len(p.get_image_info()) for p in doc], [0, 0, 1, 0, 0])

    def test_redact(self):
        msg = run('redact-text', self.pdf, '1', 'invoice')
        out = self.pdf[:-4] + '_redacted.pdf'
        self.assertIn(out, msg)
        self.assertTrue(all('invoice' not in t and 'total' in t for t in self.texts(out)))
        self.assertIn('invoice', self.texts()[0])  # original untouched

    def test_encrypt_decrypt(self):
        self.assertRaises(ValueError, run, 'encrypt', self.pdf, '1', 'pw', 'typo')
        run('encrypt', self.pdf, '1', 'pw', 'pw')
        locked = self.pdf[:-4] + '_protected.pdf'
        self.assertTrue(pymupdf.open(locked).needs_pass)
        # edits keep the password
        run('delete-pages', locked, '1', 'pw', '1')
        with pymupdf.open(locked) as doc:
            self.assertTrue(doc.needs_pass and doc.authenticate('pw') and len(doc) == 4)
        self.assertRaises(ValueError, run, 'decrypt', locked, '1', 'wrong')
        run('decrypt', locked, '1', 'pw')
        self.assertFalse(pymupdf.open(locked).needs_pass)

    def test_sanitize_compress(self):
        with pymupdf.open(self.pdf) as doc:
            doc.set_metadata({'author': 'Secret Person'})
            doc.saveIncr()
        run('sanitize', self.pdf, '1')
        self.assertEqual(pymupdf.open(self.pdf[:-4] + '_sanitized.pdf').metadata['author'], '')
        with pymupdf.open(self.pdf) as doc:  # big noisy image -> compressible
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


if __name__ == '__main__':
    unittest.main()
