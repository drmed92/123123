package app.wablink;

/** Run with: java -ea (see build.sh). */
public class BlinkStateCheck {
    public static void main(String[] args) {
        String sms = "com.samsung.android.messaging", dialer = "com.samsung.android.dialer";
        assert BlinkState.sourceOf("com.whatsapp", sms, dialer) == BlinkState.WHATSAPP;
        assert BlinkState.sourceOf("com.whatsapp.w4b", sms, dialer) == BlinkState.WHATSAPP;
        assert BlinkState.sourceOf("com.google.android.gm", sms, dialer) == BlinkState.GMAIL;
        assert BlinkState.sourceOf(sms, sms, dialer) == BlinkState.SMS;
        assert BlinkState.sourceOf(dialer, sms, dialer) == BlinkState.MISSED_CALL;
        assert BlinkState.sourceOf("com.android.server.telecom", sms, dialer) == BlinkState.MISSED_CALL;
        assert BlinkState.sourceOf("org.telegram.messenger", sms, dialer) == -1;
        assert BlinkState.sourceOf("org.telegram.messenger", null, null) == -1 : "no default SMS/dialer app";

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
