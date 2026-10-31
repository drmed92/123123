package io.github.drmed92.notepad;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import java.nio.charset.StandardCharsets;
import org.junit.Test;

public class TextCodecTest {
    @Test
    public void htmlRoundTripKeepsTextAndFormatting() {
        String text = "\n  a<b> & \"c\"\ttab\n\né😀 end  ";
        int n = text.length();
        int[] style = new int[n], color = new int[n];
        for (int i = 2; i < 8; i++) style[i] = TextCodec.BOLD | TextCodec.UNDERLINE;
        for (int i = 5; i < 12; i++) color[i] = 0xFFFF0000;
        style[n - 1] = TextCodec.ITALIC;

        TextCodec.Rich back = TextCodec.fromHtml(TextCodec.toHtml(new TextCodec.Rich(text, style, color)));

        assertEquals(text, back.text);
        assertArrayEquals(style, back.style);
        assertArrayEquals(color, back.color);
    }

    @Test
    public void foreignHtmlOpensAsPlainText() {
        assertNull(TextCodec.fromHtml("<html><body><pre>hi</pre></body></html>"));
    }

    @Test
    public void pageBoundariesFollowLinesAndUtf8() {
        byte[] b = "ab\ncd\nef".getBytes(StandardCharsets.UTF_8);
        assertEquals(6, TextCodec.pageEnd(b, b.length));    // stop after "cd\n"
        assertEquals(3, TextCodec.pageStart(b, b.length));  // skip partial "ab\n"
        assertEquals(0, TextCodec.lastLineEnd(b, 2));

        byte[] e = "aé".getBytes(StandardCharsets.UTF_8); // 'é' is 2 bytes
        assertEquals(1, TextCodec.pageEnd(e, 2));           // don't split 'é'
        assertEquals(3, TextCodec.pageEnd(e, 3));
        assertEquals(1, TextCodec.pageStart(new byte[] {(byte) 0xA9, 'x'}, 2)); // skip continuation byte
    }
}
