package app.wablink;

/** Run with: java -ea (see build.sh). */
public class BlinkStateCheck {
    public static void main(String[] args) {
        assert BlinkState.isWhatsApp("com.whatsapp");
        assert BlinkState.isWhatsApp("com.whatsapp.w4b");
        assert !BlinkState.isWhatsApp("org.telegram.messenger");

        BlinkState s = new BlinkState();
        assert s.shouldShow(true, false, false) : "unread + screen off -> blink";
        assert !s.shouldShow(false, false, false) : "nothing unread";
        assert !s.shouldShow(true, true, false) : "screen in use";
        assert !s.shouldShow(true, false, true) : "already blinking";

        assert s.onPosted(100);
        s.dismissed = true;
        assert !s.shouldShow(true, false, false) : "dismissed stays quiet";
        assert !s.onPosted(100) : "silent re-post is not a new message";
        assert s.dismissed : "re-post must not undo dismiss";
        assert s.onPosted(200) : "newer message";
        assert s.shouldShow(true, false, false) : "new message re-arms";

        System.out.println("BlinkStateCheck OK");
    }
}
