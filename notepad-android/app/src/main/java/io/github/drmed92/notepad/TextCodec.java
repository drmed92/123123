package io.github.drmed92.notepad;

import java.util.ArrayDeque;
import java.util.Arrays;

/** Pure-Java helpers (no Android types) so they can be unit tested on the JVM. */
final class TextCodec {
    static final int BOLD = 1, ITALIC = 2, UNDERLINE = 4; // BOLD/ITALIC match android.graphics.Typeface
    private static final String GENERATOR = "<meta name=\"generator\" content=\"ClassicNotepad\">";

    /** Text plus per-char style bits and colors (0 = default color). */
    static final class Rich {
        final String text;
        final int[] style, color;

        Rich(String text, int[] style, int[] color) {
            this.text = text;
            this.style = style;
            this.color = color;
        }
    }

    /** Writes an HTML file any browser shows with the formatting, and that fromHtml reads back exactly. */
    static String toHtml(Rich r) {
        StringBuilder o = new StringBuilder("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">")
                .append(GENERATOR)
                .append("</head>\n<body><pre style=\"white-space:pre-wrap;word-wrap:break-word;font-family:monospace\">\n");
        String t = r.text;
        for (int i = 0, n = t.length(); i < n; ) {
            int s = r.style[i], c = r.color[i], j = i;
            while (j < n && r.style[j] == s && r.color[j] == c) j++;
            if (c != 0) o.append(String.format("<span style=\"color:#%06x\">", c & 0xFFFFFF));
            if ((s & BOLD) != 0) o.append("<b>");
            if ((s & ITALIC) != 0) o.append("<i>");
            if ((s & UNDERLINE) != 0) o.append("<u>");
            for (int k = i; k < j; k++) {
                char ch = t.charAt(k);
                if (ch == '&') o.append("&amp;");
                else if (ch == '<') o.append("&lt;");
                else if (ch == '>') o.append("&gt;");
                else o.append(ch);
            }
            if ((s & UNDERLINE) != 0) o.append("</u>");
            if ((s & ITALIC) != 0) o.append("</i>");
            if ((s & BOLD) != 0) o.append("</b>");
            if (c != 0) o.append("</span>");
            i = j;
        }
        return o.append("</pre></body></html>\n").toString();
    }

    /** Reads HTML written by toHtml. Returns null for any other file, so it opens as plain source text. */
    static Rich fromHtml(String h) {
        if (!h.contains(GENERATOR)) return null;
        int p = h.indexOf("<pre"), end = h.lastIndexOf("</pre>");
        if (p < 0 || (p = h.indexOf('>', p) + 1) <= 0 || end < p) return null;
        if (h.charAt(p) == '\n') p++; // HTML drops the newline right after <pre>; toHtml always writes one
        StringBuilder t = new StringBuilder();
        int[] style = new int[Math.max(0, end - p)], color = new int[style.length];
        int b = 0, it = 0, u = 0;
        ArrayDeque<Integer> colors = new ArrayDeque<>();
        for (int i = p; i < end; i++) {
            char ch = h.charAt(i);
            if (ch == '<') {
                int q = h.indexOf('>', i);
                if (q < 0) break;
                String tag = h.substring(i + 1, q).trim().toLowerCase();
                i = q;
                boolean close = tag.startsWith("/");
                String name = (close ? tag.substring(1) : tag).split("\\s", 2)[0];
                int d = close ? -1 : 1;
                switch (name) {
                    case "b": case "strong": b += d; break;
                    case "i": case "em": it += d; break;
                    case "u": u += d; break;
                    case "span":
                        if (close) colors.poll();
                        else colors.push(parseColor(tag));
                        break;
                    default: break;
                }
                continue;
            }
            if (ch == '&') {
                int q = h.indexOf(';', i);
                Character e = q > i && q - i < 10 ? entity(h.substring(i + 1, q)) : null;
                if (e != null) {
                    ch = e;
                    i = q;
                }
            }
            style[t.length()] = (b > 0 ? BOLD : 0) | (it > 0 ? ITALIC : 0) | (u > 0 ? UNDERLINE : 0);
            color[t.length()] = colors.isEmpty() ? 0 : colors.peek();
            t.append(ch);
        }
        return new Rich(t.toString(), Arrays.copyOf(style, t.length()), Arrays.copyOf(color, t.length()));
    }

    private static int parseColor(String tag) {
        int k = tag.indexOf("color:#");
        if (k < 0 || k + 13 > tag.length()) return 0;
        try {
            return 0xFF000000 | Integer.parseInt(tag.substring(k + 7, k + 13), 16);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private static Character entity(String e) {
        switch (e) {
            case "amp": return '&';
            case "lt": return '<';
            case "gt": return '>';
            case "quot": return '"';
            case "apos": return '\'';
            case "nbsp": return ' ';
        }
        try {
            if (e.startsWith("#x")) return (char) Integer.parseInt(e.substring(2), 16);
            if (e.startsWith("#")) return (char) Integer.parseInt(e.substring(1));
        } catch (NumberFormatException ignored) {
        }
        return null;
    }

    // ---- page boundaries for large files (byte offsets; '\n' is never inside a UTF-8 multi-byte char) ----

    /** Index just after the last '\n' in b[0,len), or 0 if there is none. */
    static int lastLineEnd(byte[] b, int len) {
        for (int i = len - 1; i >= 0; i--) if (b[i] == '\n') return i + 1;
        return 0;
    }

    /** Where a forward page should end: after its last full line, else on a UTF-8 char boundary. Never 0 if len > 0. */
    static int pageEnd(byte[] b, int len) {
        int cut = lastLineEnd(b, len);
        if (cut > 0) return cut;
        int j = len - 1;
        while (j > 0 && (b[j] & 0xC0) == 0x80) j--; // start of the last char
        if (j < 0) return 0;
        int lead = b[j] & 0xFF;
        int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        return j + need <= len || j == 0 ? len : j;
    }

    /** Where a backward page should start: after its first (partial) line, else on a UTF-8 char boundary. Always < len. */
    static int pageStart(byte[] b, int len) {
        for (int i = 0; i < len - 1; i++) if (b[i] == '\n') return i + 1;
        int i = 0;
        while (i < len - 1 && (b[i] & 0xC0) == 0x80) i++;
        return i;
    }

    private TextCodec() {
    }
}
