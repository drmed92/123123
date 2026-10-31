package io.github.drmed92.notepad;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.text.Editable;
import android.text.InputFilter;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextWatcher;
import android.text.format.DateFormat;
import android.text.format.Formatter;
import android.text.style.CharacterStyle;
import android.text.style.ForegroundColorSpan;
import android.text.style.StyleSpan;
import android.text.style.UnderlineSpan;
import android.util.TypedValue;
import android.view.ActionMode;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.InputMethodManager;
import android.webkit.MimeTypeMap;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.PopupMenu;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;
import java.util.function.Predicate;

import static java.nio.charset.StandardCharsets.UTF_8;

// shortcut: file I/O runs on the UI thread; fine for local files (<=1 MB, or 256 KB pages), move to a worker thread if slow cloud providers cause ANRs.
public class MainActivity extends Activity {
    private static final int REQ_OPEN = 1, REQ_SAVE = 2;
    private static final int LARGE = 1 << 20, PAGE = 256 << 10;
    private static final int ID_BOLD = 1001, ID_ITALIC = 1002, ID_UNDERLINE = 1003, ID_COLOR = 1004;
    private static final int SPAN = Spanned.SPAN_EXCLUSIVE_INCLUSIVE; // typing at the end of bold text stays bold, like Word

    private static final String[] TYPES = {"Text Document (*.txt)", "Formatted Text, keeps bold/colors (*.html)", "Log File (*.log)",
            "Markdown (*.md)", "CSV (*.csv)", "JSON (*.json)", "XML (*.xml)", "All Files (type any extension)"};
    private static final String[] EXTS = {".txt", ".html", ".log", ".md", ".csv", ".json", ".xml", ""};
    private static final int[] COLORS = {0, 0xFF000000, 0xFF7F7F7F, 0xFFC00000, 0xFFFF0000, 0xFFFF6600, 0xFFE0A800,
            0xFF00B050, 0xFF008080, 0xFF00B0F0, 0xFF0070C0, 0xFF002060, 0xFF7030A0, 0xFFFF33CC};
    private static final String[] COLOR_NAMES = {"Automatic", "Black", "Gray", "Dark Red", "Red", "Orange", "Gold",
            "Green", "Teal", "Light Blue", "Blue", "Dark Blue", "Purple", "Pink"};

    private EditText edit;
    private TextView title, pageInfo;
    private View pager;
    private SharedPreferences prefs;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private Uri uri; // file:// in app storage, content:// anywhere else, null if never saved
    private String name = "Untitled", findText = "";
    private boolean modified, loading, readOnly, crlf, paged, following, closing;
    private long winStart, winEnd, fileSize; // byte window on screen in paged / follow mode
    private Runnable afterSave;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        if (Build.VERSION.SDK_INT >= 30) { // edge-to-edge (forced on Android 15+): pad for system bars and keyboard
            getWindow().setDecorFitsSystemWindows(false);
            findViewById(R.id.root).setOnApplyWindowInsetsListener((v, in) -> {
                Insets i = in.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
                v.setPadding(i.left, i.top, i.right, i.bottom);
                return WindowInsets.CONSUMED;
            });
        }
        prefs = getPreferences(MODE_PRIVATE);
        edit = findViewById(R.id.edit);
        title = findViewById(R.id.title);
        pager = findViewById(R.id.pager);
        pageInfo = findViewById(R.id.p_info);

        edit.setFilters(new InputFilter[]{(src, s, e, dest, ds, de) -> readOnly && !loading ? dest.subSequence(ds, de) : null});
        edit.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void afterTextChanged(Editable s) {
                if (!loading) setModified(true);
            }
        });
        edit.setCustomSelectionActionModeCallback(formatBar);
        applyView();

        findViewById(R.id.close).setOnClickListener(v -> confirmDiscard(this::exit));
        findViewById(R.id.m_file).setOnClickListener(this::fileMenu);
        findViewById(R.id.m_edit).setOnClickListener(this::editMenu);
        findViewById(R.id.m_view).setOnClickListener(this::viewMenu);
        findViewById(R.id.m_help).setOnClickListener(this::helpMenu);
        findViewById(R.id.p_first).setOnClickListener(v -> page(0, true));
        findViewById(R.id.p_prev).setOnClickListener(v -> page(winStart, false));
        findViewById(R.id.p_next).setOnClickListener(v -> page(winEnd, true));
        findViewById(R.id.p_last).setOnClickListener(v -> page(-1, false));
        restore();
    }

    // ---------------------------------------------------------------- menus

    private void fileMenu(View v) {
        PopupMenu p = new PopupMenu(this, v);
        Menu m = p.getMenu();
        item(m, "New", () -> confirmDiscard(this::blank));
        item(m, "Open…", () -> confirmDiscard(() -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"), REQ_OPEN)));
        item(m, "Open from App Storage…", () -> confirmDiscard(this::openSavedDialog));
        item(m, "Save", () -> save(null)).setEnabled(!readOnly);
        item(m, "Save As…", () -> saveAs(null)).setEnabled(!readOnly);
        item(m, "Exit", () -> confirmDiscard(this::exit));
        p.show();
    }

    private void editMenu(View v) {
        PopupMenu p = new PopupMenu(this, v);
        Menu m = p.getMenu();
        item(m, "Undo", () -> edit.onTextContextMenuItem(android.R.id.undo));
        item(m, "Redo", () -> edit.onTextContextMenuItem(android.R.id.redo));
        item(m, "Cut", () -> edit.onTextContextMenuItem(android.R.id.cut));
        item(m, "Copy", () -> edit.onTextContextMenuItem(android.R.id.copy));
        item(m, "Paste", () -> edit.onTextContextMenuItem(android.R.id.paste));
        item(m, "Delete", () -> edit.getText().delete(selStart(), selEnd()));
        item(m, "Select All", () -> {
            edit.requestFocus();
            edit.selectAll();
        });
        item(m, "Find…", this::findDialog);
        item(m, "Find Next", this::findNext);
        item(m, "Time/Date", () -> {
            Date now = new Date();
            edit.getText().replace(selStart(), selEnd(), DateFormat.getTimeFormat(this).format(now) + " " + DateFormat.getDateFormat(this).format(now));
        });
        p.show();
    }

    private void viewMenu(View v) {
        PopupMenu p = new PopupMenu(this, v);
        Menu m = p.getMenu();
        item(m, "Word Wrap", () -> {
            prefs.edit().putBoolean("wrap", !prefs.getBoolean("wrap", true)).apply();
            applyView();
        }).setCheckable(true).setChecked(prefs.getBoolean("wrap", true));
        item(m, "Follow Log (live updates)", this::toggleFollow).setCheckable(true).setChecked(following);
        item(m, "Zoom In", () -> zoom(2));
        item(m, "Zoom Out", () -> zoom(-2));
        item(m, "Restore Default Zoom", () -> zoom(0));
        p.show();
    }

    private void helpMenu(View v) {
        PopupMenu p = new PopupMenu(this, v);
        item(p.getMenu(), "About Classic Notepad", () -> new AlertDialog.Builder(this)
                .setTitle("About Classic Notepad")
                .setMessage("Version " + version() + "\n\nFreeware. No ads, no tracking, no internet access.\n\n"
                        + "File > Save keeps notes in the app's private storage:\n" + notesDir() + "\n\n"
                        + "Save As… > Browse… saves anywhere, with any extension. Save as .html to keep bold, italic, "
                        + "underline and colors; other types are plain text.\n\n"
                        + "Files over 1 MB open read-only, one page at a time. View > Follow Log shows new lines "
                        + "as another app writes them.\n\nSelect text to get the B / I / U / A formatting bar.")
                .setPositiveButton("OK", null).show());
        p.show();
    }

    private static MenuItem item(Menu m, String title, Runnable r) {
        return m.add(title).setOnMenuItemClickListener(i -> {
            r.run();
            return true;
        });
    }

    private String version() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "";
        }
    }

    // ---------------------------------------------------------------- document state

    void blank() {
        stopFollow();
        uri = null;
        name = "Untitled";
        crlf = paged = false;
        setReadOnly(false);
        setDoc("");
        setModified(false);
    }

    private void setDoc(CharSequence text) {
        loading = true;
        edit.setText(text);
        loading = false;
        edit.setSelection(0);
        edit.scrollTo(0, 0);
    }

    private void setModified(boolean m) {
        if (modified == m) return;
        modified = m;
        updateTitle();
    }

    private void setReadOnly(boolean ro) {
        readOnly = ro;
        edit.setShowSoftInputOnFocus(!ro);
        if (ro) ((InputMethodManager) getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(edit.getWindowToken(), 0);
        updatePager();
        updateTitle();
    }

    private void updateTitle() {
        title.setText((modified ? "*" : "") + name + " - Notepad" + (following ? " [Following]" : readOnly ? " [Read-only]" : ""));
    }

    private void updatePager() {
        pager.setVisibility(paged ? View.VISIBLE : View.GONE);
        long max = Math.max(1, fileSize);
        pageInfo.setText(following ? "Following… " + Formatter.formatShortFileSize(this, fileSize)
                : (winStart * 100 / max) + "% – " + (winEnd * 100 / max) + "% of " + Formatter.formatShortFileSize(this, fileSize));
    }

    private void applyView() {
        edit.setHorizontallyScrolling(!prefs.getBoolean("wrap", true));
        edit.setTextSize(TypedValue.COMPLEX_UNIT_SP, prefs.getInt("size", 16));
    }

    private void zoom(int d) {
        prefs.edit().putInt("size", d == 0 ? 16 : Math.max(8, Math.min(48, prefs.getInt("size", 16) + d))).apply();
        applyView();
    }

    /** Runs next, first offering to save unsaved changes (the classic "Do you want to save changes?"). */
    private void confirmDiscard(Runnable next) {
        if (!modified) {
            next.run();
            return;
        }
        new AlertDialog.Builder(this).setTitle("Notepad")
                .setMessage("Do you want to save changes to " + name + "?")
                .setPositiveButton("Save", (d, w) -> save(next))
                .setNegativeButton("Don't Save", (d, w) -> next.run())
                .setNeutralButton("Cancel", null).show();
    }

    private void exit() {
        closing = true;
        draftFile().delete();
        prefs.edit().remove("uri").remove("name").apply();
        finishAndRemoveTask();
    }

    // ---------------------------------------------------------------- open

    void openUri(Uri u) {
        stopFollow();
        try {
            long size = sizeOf(u);
            String n = displayName(u);
            if (size > LARGE) {
                uri = u;
                name = n;
                fileSize = size;
                paged = true;
                setReadOnly(true);
                showWindow(0, true);
            } else { // shortcut: unknown size (-1) is read whole; fine for local storage providers
                String s = readAll(u);
                if (s.startsWith("﻿")) s = s.substring(1);
                TextCodec.Rich r = isRich(n) ? TextCodec.fromHtml(s) : null;
                crlf = r == null && s.contains("\r\n");
                uri = u;
                name = n;
                paged = false;
                setReadOnly(false);
                setDoc(r != null ? toSpans(r) : s.replace("\r\n", "\n"));
            }
            setModified(false);
            updateTitle();
        } catch (Exception e) {
            toast("Cannot open: " + e.getMessage());
            blank(); // never leave the old text attached to the new file's uri
        }
    }

    private void openSavedDialog() {
        File[] files = notesDir().listFiles(File::isFile);
        if (files == null || files.length == 0) {
            toast("No notes saved in app storage yet.");
            return;
        }
        Arrays.sort(files, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
        String[] names = new String[files.length];
        for (int i = 0; i < files.length; i++) names[i] = files[i].getName();
        ListView list = new ListView(this);
        list.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_list_item_1, names));
        AlertDialog dlg = new AlertDialog.Builder(this).setTitle("Open (long-press to delete)").setView(list)
                .setNegativeButton("Cancel", null).show();
        list.setOnItemClickListener((p, v, i, id) -> {
            dlg.dismiss();
            openUri(Uri.fromFile(files[i]));
        });
        list.setOnItemLongClickListener((p, v, i, id) -> {
            dlg.dismiss();
            confirm("Delete " + files[i].getName() + "?", () -> {
                if (!files[i].delete()) toast("Could not delete " + files[i].getName());
            });
            return true;
        });
    }

    // ---------------------------------------------------------------- save

    private void save(Runnable after) {
        if (readOnly) return;
        if (uri == null) saveAs(after);
        else if (write(uri, name) && after != null) after.run();
    }

    private void saveAs(Runnable after) {
        int pad = dp(20);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, dp(8), pad, 0);
        EditText field = new EditText(this);
        field.setSingleLine();
        field.setText(name.contains(".") ? name : name + ".txt");
        Spinner type = new Spinner(this);
        type.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, TYPES));
        int t = Arrays.asList(EXTS).indexOf(ext(field.getText().toString()));
        type.setSelection(t < 0 ? EXTS.length - 1 : t);
        type.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> p, View v, int pos, long id) {
                if (EXTS[pos].isEmpty()) return;
                String n = field.getText().toString();
                int dot = n.lastIndexOf('.');
                field.setText((dot > 0 ? n.substring(0, dot) : n) + EXTS[pos]);
            }

            @Override public void onNothingSelected(AdapterView<?> p) {}
        });
        TextView hint = new TextView(this);
        hint.setText("Save in App keeps it in Notepad's private storage. Browse… lets you pick any folder.");
        hint.setPadding(0, dp(12), 0, 0);
        box.addView(label("File name:"));
        box.addView(field);
        box.addView(label("Save as type:"));
        box.addView(type);
        box.addView(hint);
        new AlertDialog.Builder(this).setTitle("Save As").setView(box)
                .setPositiveButton("Save in App", (d, w) -> saveInApp(cleanName(field), after))
                .setNeutralButton("Browse…", (d, w) -> {
                    String n = cleanName(field);
                    if (n == null) return;
                    afterSave = after;
                    startActivityForResult(new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                            .setType(mimeOf(n)).putExtra(Intent.EXTRA_TITLE, n), REQ_SAVE);
                })
                .setNegativeButton("Cancel", null).show();
    }

    void saveInApp(String n, Runnable after) {
        if (n == null) return;
        Uri u = Uri.fromFile(new File(notesDir(), n));
        Runnable go = () -> {
            if (write(u, n) && after != null) after.run();
        };
        if (new File(notesDir(), n).exists() && !u.equals(uri)) confirm(n + " already exists. Replace it?", go);
        else go.run();
    }

    private boolean write(Uri u, String n) {
        try (OutputStream os = getContentResolver().openOutputStream(u, "wt")) {
            if (os == null) throw new IOException("no output stream");
            TextCodec.Rich r = toRich(edit.getText());
            String out;
            if (isRich(n)) {
                out = TextCodec.toHtml(r);
            } else {
                for (int i = 0; i < r.style.length; i++) {
                    if (r.style[i] != 0 || r.color[i] != 0) {
                        toast("Saved as plain text. Use .html to keep formatting.");
                        break;
                    }
                }
                out = crlf ? r.text.replace("\n", "\r\n") : r.text;
            }
            os.write(out.getBytes(UTF_8));
        } catch (Exception e) {
            toast("Could not save: " + e.getMessage());
            return false;
        }
        uri = u;
        name = n;
        setModified(false);
        updateTitle();
        return true;
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        Runnable after = afterSave;
        afterSave = null;
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri u = data.getData();
        try { // lets the file be reopened after a restart
            getContentResolver().takePersistableUriPermission(u, data.getFlags()
                    & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION));
        } catch (Exception ignored) {
        }
        if (req == REQ_OPEN) openUri(u);
        else if (req == REQ_SAVE && write(u, displayName(u)) && after != null) after.run();
    }

    // ---------------------------------------------------------------- unsaved-work draft

    @Override
    protected void onStop() {
        super.onStop();
        if (closing) return;
        prefs.edit().putString("uri", uri == null ? null : uri.toString()).putString("name", name).putBoolean("crlf", crlf).apply();
        File d = draftFile(), tmp = new File(getFilesDir(), "draft.tmp");
        if (!modified || readOnly) {
            d.delete();
            return;
        }
        try (FileOutputStream os = new FileOutputStream(tmp)) {
            os.write(TextCodec.toHtml(toRich(edit.getText())).getBytes(UTF_8));
        } catch (IOException e) {
            return; // keep the previous draft rather than a half-written one
        }
        tmp.renameTo(d);
    }

    private void restore() {
        String u = prefs.getString("uri", null);
        File d = draftFile();
        if (d.exists()) {
            try {
                Spannable text = toSpans(TextCodec.fromHtml(readAll(Uri.fromFile(d))));
                uri = u == null ? null : Uri.parse(u);
                name = prefs.getString("name", "Untitled");
                crlf = prefs.getBoolean("crlf", false);
                setDoc(text);
                setModified(true);
                updateTitle();
                return;
            } catch (Exception ignored) { // unreadable draft: fall through
            }
        }
        if (u != null) openUri(Uri.parse(u));
        else updateTitle();
    }

    private File draftFile() {
        return new File(getFilesDir(), "draft.html");
    }

    private File notesDir() {
        File d = new File(getFilesDir(), "notes");
        d.mkdirs();
        return d;
    }

    // ---------------------------------------------------------------- large files and log following

    /** forward: show the page starting at pos; backward: the page ending at pos (-1 = end of file). */
    private void page(long pos, boolean forward) {
        stopFollow();
        try {
            fileSize = sizeOf(uri);
            if (pos < 0) pos = fileSize;
            if (forward ? pos >= fileSize && pos > 0 : pos <= 0) return;
            showWindow(pos, forward);
        } catch (Exception e) {
            toast("Cannot read: " + e.getMessage());
        }
    }

    private void showWindow(long pos, boolean forward) throws IOException {
        long a = forward ? pos : Math.max(0, pos - PAGE);
        long b = forward ? Math.min(fileSize, pos + PAGE) : pos;
        byte[] buf = readRange(uri, a, (int) (b - a));
        int from = 0, to = buf.length;
        if (forward && a + to < fileSize) to = TextCodec.pageEnd(buf, to);
        if (!forward && a > 0) from = TextCodec.pageStart(buf, to);
        winStart = a + from;
        winEnd = a + to;
        setDoc(decode(buf, from, to));
        updatePager();
    }

    void toggleFollow() {
        if (following) {
            stopFollow();
            openUri(uri); // back to normal (editable) view
            return;
        }
        if (uri == null) {
            toast("Open a log file first (File > Open…), then choose Follow Log.");
            return;
        }
        confirmDiscard(() -> {
            try {
                fileSize = sizeOf(uri);
                paged = following = true;
                setReadOnly(true);
                showWindow(fileSize, false);
                scrollToEnd();
                handler.removeCallbacks(poll);
                handler.postDelayed(poll, 1000);
            } catch (Exception e) {
                toast("Cannot follow: " + e.getMessage());
                stopFollow();
            }
        });
    }

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            try {
                followTick();
            } catch (Exception e) {
                toast("Stopped following: " + e.getMessage());
                stopFollow();
                return;
            }
            handler.postDelayed(this, 1000);
        }
    };

    private void followTick() throws IOException {
        long size = sizeOf(uri);
        if (size == winEnd) return;
        fileSize = size;
        if (size < winEnd || size - winEnd > PAGE) { // truncated / rotated, or a big burst: jump to the tail
            showWindow(size, false);
            scrollToEnd();
            return;
        }
        byte[] buf = readRange(uri, winEnd, (int) (size - winEnd));
        int cut = TextCodec.lastLineEnd(buf, buf.length); // whole lines only; a half-written line waits for the next tick
        if (cut == 0) return;
        winEnd += cut;
        loading = true;
        Editable t = edit.getText();
        t.append(decode(buf, 0, cut));
        if (t.length() > 2 * PAGE) { // keep the editor small: drop old lines from the top
            int drop = t.toString().indexOf('\n', t.length() - PAGE) + 1;
            if (drop <= 0) drop = t.length() - PAGE;
            // shortcut: ignores '\r' removed by decode, so Prev may repeat a few lines of a CRLF log
            winStart += t.subSequence(0, drop).toString().getBytes(UTF_8).length;
            t.delete(0, drop);
        }
        loading = false;
        updatePager();
        scrollToEnd();
    }

    private void stopFollow() {
        handler.removeCallbacks(poll);
        if (!following) return;
        following = false;
        updateTitle();
        updatePager();
    }

    private void scrollToEnd() {
        edit.post(() -> edit.bringPointIntoView(edit.length()));
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (following) handler.post(poll);
    }

    @Override
    protected void onPause() {
        super.onPause();
        handler.removeCallbacks(poll);
    }

    // ---------------------------------------------------------------- selection formatting bar (B I U A)

    private final ActionMode.Callback formatBar = new ActionMode.Callback() {
        @Override
        public boolean onCreateActionMode(ActionMode mode, Menu menu) {
            if (readOnly) return true;
            menu.add(0, ID_BOLD, 1, styled("B", 1, new StyleSpan(Typeface.BOLD))).setContentDescription("Bold");
            menu.add(0, ID_ITALIC, 1, styled("I", 1, new StyleSpan(Typeface.ITALIC))).setContentDescription("Italic");
            menu.add(0, ID_UNDERLINE, 1, styled("U", 1, new UnderlineSpan())).setContentDescription("Underline");
            menu.add(0, ID_COLOR, 1, styled("A", 1, new ForegroundColorSpan(0xFFE00000))).setContentDescription("Font color");
            return true;
        }

        @Override
        public boolean onPrepareActionMode(ActionMode mode, Menu menu) {
            return false;
        }

        @Override
        public boolean onActionItemClicked(ActionMode mode, MenuItem item) {
            int id = item.getItemId();
            if (id == ID_BOLD) toggle(TextCodec.BOLD);
            else if (id == ID_ITALIC) toggle(TextCodec.ITALIC);
            else if (id == ID_UNDERLINE) toggle(TextCodec.UNDERLINE);
            else if (id == ID_COLOR) colorDialog();
            else return false;
            return true;
        }

        @Override
        public void onDestroyActionMode(ActionMode mode) {
        }
    };

    /** Bold/italic/underline on the selection; if the whole selection already has it, removes it. */
    void toggle(int bit) {
        Editable t = edit.getText();
        int s = selStart(), e = selEnd();
        if (s == e) return;
        boolean on = false;
        for (CharacterStyle sp : t.getSpans(s, e, CharacterStyle.class))
            if (bitOf(t, sp) == bit && t.getSpanStart(sp) <= s && t.getSpanEnd(sp) >= e) on = true;
        clear(t, s, e, sp -> bitOf(t, sp) == bit);
        if (!on) t.setSpan(bit == TextCodec.UNDERLINE ? new UnderlineSpan() : new StyleSpan(bit), s, e, SPAN);
        setModified(true);
    }

    private void colorDialog() {
        int s = selStart(), e = selEnd();
        if (s == e) return;
        CharSequence[] items = new CharSequence[COLORS.length];
        for (int i = 0; i < items.length; i++)
            items[i] = styled((COLORS[i] == 0 ? "□□□  " : "■■■  ") + COLOR_NAMES[i], 3, new ForegroundColorSpan(COLORS[i] == 0 ? 0xFF000000 : COLORS[i]));
        new AlertDialog.Builder(this).setTitle("Font Color").setItems(items, (d, i) -> {
            Editable t = edit.getText();
            int end = Math.min(e, t.length());
            if (s >= end) return;
            clear(t, s, end, sp -> sp instanceof ForegroundColorSpan);
            if (COLORS[i] != 0) t.setSpan(new ForegroundColorSpan(COLORS[i]), s, end, SPAN);
            setModified(true);
        }).show();
    }

    /** Style bit of a span we manage, or 0 (also 0 for the keyboard's temporary composing underline). */
    private static int bitOf(Spanned t, Object sp) {
        if ((t.getSpanFlags(sp) & Spanned.SPAN_COMPOSING) != 0) return 0;
        if (sp instanceof StyleSpan) return ((StyleSpan) sp).getStyle() & (TextCodec.BOLD | TextCodec.ITALIC);
        if (sp.getClass() == UnderlineSpan.class) return TextCodec.UNDERLINE;
        return 0;
    }

    /** Removes matching spans from [s,e), keeping the parts of them outside that range. */
    private static void clear(Spannable t, int s, int e, Predicate<Object> match) {
        for (CharacterStyle sp : t.getSpans(s, e, CharacterStyle.class)) {
            if (!match.test(sp)) continue;
            int a = t.getSpanStart(sp), b = t.getSpanEnd(sp), f = t.getSpanFlags(sp);
            t.removeSpan(sp);
            if (a < s) t.setSpan(sp, a, s, f);
            if (b > e) t.setSpan(copy(sp), e, b, f);
        }
    }

    private static CharacterStyle copy(CharacterStyle sp) {
        if (sp instanceof StyleSpan) return new StyleSpan(((StyleSpan) sp).getStyle());
        if (sp instanceof ForegroundColorSpan) return new ForegroundColorSpan(((ForegroundColorSpan) sp).getForegroundColor());
        return new UnderlineSpan();
    }

    private static TextCodec.Rich toRich(Spanned t) {
        int n = t.length();
        int[] style = new int[n], color = new int[n];
        for (CharacterStyle sp : t.getSpans(0, n, CharacterStyle.class)) {
            int bit = bitOf(t, sp);
            int c = sp instanceof ForegroundColorSpan ? ((ForegroundColorSpan) sp).getForegroundColor() | 0xFF000000 : 0;
            if (bit == 0 && c == 0) continue;
            for (int i = t.getSpanStart(sp), end = t.getSpanEnd(sp); i < end; i++) {
                style[i] |= bit;
                if (c != 0) color[i] = c;
            }
        }
        return new TextCodec.Rich(t.toString(), style, color);
    }

    private static Spannable toSpans(TextCodec.Rich r) {
        SpannableStringBuilder b = new SpannableStringBuilder(r.text);
        int n = r.text.length();
        for (int bit : new int[]{TextCodec.BOLD, TextCodec.ITALIC, TextCodec.UNDERLINE}) {
            for (int i = 0, j; i < n; i = j) {
                boolean on = (r.style[i] & bit) != 0;
                j = i + 1;
                while (j < n && ((r.style[j] & bit) != 0) == on) j++;
                if (on) b.setSpan(bit == TextCodec.UNDERLINE ? new UnderlineSpan() : new StyleSpan(bit), i, j, SPAN);
            }
        }
        for (int i = 0, j; i < n; i = j) {
            j = i + 1;
            while (j < n && r.color[j] == r.color[i]) j++;
            if (r.color[i] != 0) b.setSpan(new ForegroundColorSpan(r.color[i]), i, j, SPAN);
        }
        return b;
    }

    // ---------------------------------------------------------------- find

    private void findDialog() {
        EditText q = new EditText(this);
        q.setSingleLine();
        q.setText(findText);
        q.selectAll();
        LinearLayout box = new LinearLayout(this);
        box.setPadding(dp(20), dp(8), dp(20), 0);
        box.addView(q, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        new AlertDialog.Builder(this).setTitle("Find").setView(box)
                .setPositiveButton("Find Next", (d, w) -> {
                    findText = q.getText().toString();
                    findNext();
                })
                .setNegativeButton("Cancel", null).show();
    }

    private void findNext() {
        if (findText.isEmpty()) {
            findDialog();
            return;
        }
        String t = edit.getText().toString().toLowerCase(Locale.ROOT), f = findText.toLowerCase(Locale.ROOT);
        int i = t.indexOf(f, selEnd());
        if (i < 0) i = t.indexOf(f); // wrap around
        if (i < 0) {
            toast("Cannot find \"" + findText + "\"" + (paged ? " on this page" : ""));
            return;
        }
        edit.requestFocus();
        edit.setSelection(i, i + f.length());
    }

    // ---------------------------------------------------------------- I/O helpers

    private String readAll(Uri u) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(u)) {
            if (in == null) throw new IOException("no input stream");
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            for (int r; (r = in.read(buf)) > 0; ) out.write(buf, 0, r);
            return out.toString("UTF-8");
        }
    }

    private byte[] readRange(Uri u, long off, int len) throws IOException {
        try (FileInputStream in = new ParcelFileDescriptor.AutoCloseInputStream(getContentResolver().openFileDescriptor(u, "r"))) {
            FileChannel ch = in.getChannel();
            ByteBuffer bb = ByteBuffer.allocate(len);
            while (bb.hasRemaining() && ch.read(bb, off + bb.position()) > 0) {
                // read until full or end of file
            }
            return Arrays.copyOf(bb.array(), bb.position());
        }
    }

    private long sizeOf(Uri u) throws IOException {
        try (ParcelFileDescriptor p = getContentResolver().openFileDescriptor(u, "r")) {
            if (p == null) throw new IOException("cannot open");
            return p.getStatSize();
        }
    }

    private static String decode(byte[] b, int from, int to) {
        return new String(b, from, to - from, UTF_8).replace("\r\n", "\n");
    }

    private String displayName(Uri u) {
        if ("file".equals(u.getScheme())) return new File(u.getPath()).getName();
        try (Cursor c = getContentResolver().query(u, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) return c.getString(0);
        } catch (Exception ignored) {
        }
        String s = u.getLastPathSegment();
        return s == null ? "Untitled" : s;
    }

    private static String ext(String n) {
        int dot = n.lastIndexOf('.');
        return dot > 0 ? n.substring(dot).toLowerCase(Locale.ROOT) : "";
    }

    private static boolean isRich(String n) {
        String e = ext(n);
        return e.equals(".html") || e.equals(".htm");
    }

    private static String mimeOf(String n) {
        String e = ext(n);
        String m = e.isEmpty() ? null : MimeTypeMap.getSingleton().getMimeTypeFromExtension(e.substring(1));
        return m != null ? m : "application/octet-stream"; // octet-stream stops pickers adding ".txt" to unknown extensions
    }

    private String cleanName(EditText field) {
        String n = field.getText().toString().trim();
        if (n.isEmpty() || n.contains("/") || n.equals(".") || n.equals("..")) {
            toast("Please enter a valid file name.");
            return null;
        }
        return n;
    }

    // ---------------------------------------------------------------- small UI helpers

    private int selStart() {
        return Math.max(0, Math.min(edit.getSelectionStart(), edit.getSelectionEnd()));
    }

    private int selEnd() {
        return Math.max(0, Math.max(edit.getSelectionStart(), edit.getSelectionEnd()));
    }

    private static CharSequence styled(String text, int end, Object span) {
        SpannableString s = new SpannableString(text);
        s.setSpan(span, 0, end, 0);
        if (span instanceof ForegroundColorSpan && text.length() == 1) s.setSpan(new StyleSpan(Typeface.BOLD), 0, 1, 0);
        return s;
    }

    private TextView label(String text) {
        TextView v = new TextView(this);
        v.setText(text);
        v.setPadding(0, dp(8), 0, 0);
        return v;
    }

    private void confirm(String msg, Runnable yes) {
        new AlertDialog.Builder(this).setMessage(msg)
                .setPositiveButton("Yes", (d, w) -> yes.run())
                .setNegativeButton("No", null).show();
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_LONG).show();
    }
}
