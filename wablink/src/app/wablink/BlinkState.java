package app.wablink;

/** Pure decision logic, kept free of Android types so test/BlinkStateCheck.java can run it on a plain JVM. */
final class BlinkState {
    /** User has seen the blink (tapped it or pressed power); stays quiet until a new message or an unlock. */
    boolean dismissed;
    private long newestWhen;

    static boolean isWhatsApp(String pkg) {
        return "com.whatsapp".equals(pkg) || "com.whatsapp.w4b".equals(pkg);
    }

    /**
     * Called for every WhatsApp notification post. Returns true if it carries a newer message.
     * shortcut: uses Notification.when to tell new messages from silent re-posts; upgrade to message-count parsing if WhatsApp ever re-stamps old notifications.
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
