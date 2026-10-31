package io.github.drmed92.notepad;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.net.Uri;
import android.os.Looper;
import android.text.Editable;
import android.text.style.StyleSpan;
import android.view.View;
import android.widget.EditText;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;

/** Drives the real Activity on the JVM (no device needed). */
@RunWith(RobolectricTestRunner.class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(qualifiers = "w411dp-h891dp-xxhdpi")
public class MainActivityTest {
    private final MainActivity a = Robolectric.buildActivity(MainActivity.class).setup().get();
    private final EditText edit = a.findViewById(R.id.edit);
    private final TextView title = a.findViewById(R.id.title);

    @Test
    public void boldSurvivesSaveAsHtmlAndReopen() {
        edit.setText("hello  world\n\tend");
        edit.setSelection(0, 5);
        a.toggle(TextCodec.BOLD);
        assertTrue(title.getText().toString().startsWith("*"));

        a.saveInApp("note.html", null);
        assertEquals("note.html - Notepad", title.getText().toString());
        a.blank();
        a.openUri(Uri.fromFile(new File(a.getFilesDir(), "notes/note.html")));

        Editable t = edit.getText();
        assertEquals("hello  world\n\tend", t.toString());
        StyleSpan[] bold = t.getSpans(0, t.length(), StyleSpan.class);
        assertEquals(1, bold.length);
        assertEquals(0, t.getSpanStart(bold[0]));
        assertEquals(5, t.getSpanEnd(bold[0]));

        edit.setSelection(0, 5);
        a.toggle(TextCodec.BOLD); // second press removes it
        assertEquals(0, t.getSpans(0, t.length(), StyleSpan.class).length);
    }

    @Test
    public void largeFilePagesReadOnlyAndFollowsAppends() throws Exception {
        File f = new File(a.getFilesDir(), "big.log");
        try (FileWriter w = new FileWriter(f)) {
            for (int i = 0; i < 200_000; i++) w.write("line " + i + "\n"); // ~2.3 MB
        }
        a.openUri(Uri.fromFile(f));
        assertTrue(title.getText().toString().contains("[Read-only]"));
        assertEquals(View.VISIBLE, a.findViewById(R.id.pager).getVisibility());
        String page1 = edit.getText().toString();
        assertTrue(page1.startsWith("line 0\n") && page1.endsWith("\n"));

        edit.getText().insert(0, "typed"); // read-only: rejected
        assertTrue(edit.getText().toString().startsWith("line 0\n"));

        a.findViewById(R.id.p_next).performClick();
        String[] lines1 = page1.split("\n");
        int lastOnPage1 = Integer.parseInt(lines1[lines1.length - 1].substring(5));
        assertTrue(edit.getText().toString().startsWith("line " + (lastOnPage1 + 1) + "\n"));

        a.toggleFollow();
        assertTrue(title.getText().toString().contains("[Following]"));
        assertTrue(edit.getText().toString().endsWith("line 199999\n"));
        try (FileOutputStream o = new FileOutputStream(f, true)) {
            o.write("NEW ENTRY\nhalf".getBytes(StandardCharsets.UTF_8));
        }
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMillis(1100));
        assertTrue(edit.getText().toString().endsWith("line 199999\nNEW ENTRY\n")); // half-written line waits
    }

    @Test
    public void unsavedTextIsRestoredAfterRestart() {
        edit.setText("draft text");
        a.onStop(); // app goes to background (or is swiped away)
        MainActivity b = Robolectric.buildActivity(MainActivity.class).setup().get();
        assertEquals("draft text", ((EditText) b.findViewById(R.id.edit)).getText().toString());
        assertFalse(((TextView) b.findViewById(R.id.title)).getText().toString().isEmpty());
    }

    @Test
    public void screenshot() throws Exception {
        edit.setText("Hello from Classic Notepad!\n\nSelect text to get B / I / U / A.\n");
        edit.setSelection(0, 5);
        a.toggle(TextCodec.BOLD);
        View root = a.findViewById(R.id.root);
        Bitmap bmp = Bitmap.createBitmap(root.getWidth(), root.getHeight(), Bitmap.Config.ARGB_8888);
        root.draw(new Canvas(bmp));
        try (FileOutputStream o = new FileOutputStream("build/screenshot.png")) {
            bmp.compress(Bitmap.CompressFormat.PNG, 100, o);
        }
    }
}
