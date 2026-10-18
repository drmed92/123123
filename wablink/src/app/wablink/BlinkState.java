package app.wablink;

/** Pure decision logic, kept free of Android types so test/BlinkStateCheck.java can run it on a plain JVM. */
final class BlinkState {
    /** Notification sources; each is a bit in WaListener.unread and an index into BlinkActivity's icon table. */
    static final int WHATSAPP = 0, SMS = 1, MISSED_CALL = 2, GMAIL = 3;

    /** User has seen the blink (tapped it or pressed power); stays quiet until a new message or an unlock. */
    boolean dismissed;
    private long newestWhen;

    /** Source of a non-ongoing notification from {@code pkg}, or -1 if it is not one we blink for. */
    static int sourceOf(String pkg, String smsPkg, String dialerPkg) {
        if ("com.whatsapp".equals(pkg) || "com.whatsapp.w4b".equals(pkg)) return WHATSAPP;
        if ("com.google.android.gm".equals(pkg)) return GMAIL;
        if (pkg.equals(smsPkg)) return SMS;
        if (pkg.equals(dialerPkg) || "com.android.server.telecom".equals(pkg)) return MISSED_CALL;
        return -1;
    }

    /**
     * Called for every watched notification post. Returns true if it carries a newer message.
     * shortcut: uses Notification.when to tell new messages from silent re-posts; upgrade to message-count parsing if an app ever re-stamps old notifications.
     */
    boolean onPosted(long when) {
        if (when <= newestWhen) return false;
        newestWhen = when;
        dismissed = false;
        return true;
    }

    boolean shouldShow(boolean unread, boolean screenOn, boolean showing) {
        return unread && !dismissed && !screenOn && !showing;
    }
}
