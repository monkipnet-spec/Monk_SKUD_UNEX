package by.monk.skud.monitor;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;

/** Opens the kiosk monitor automatically after the Android TV box boots. */
public class BootReceiver extends BroadcastReceiver {
    private static final long START_DELAY_MS = 8000L;

    @Override public void onReceive(final Context context, Intent intent) {
        if (intent == null) return;
        final String action = intent.getAction();
        if (!Intent.ACTION_BOOT_COMPLETED.equals(action)
                && !Intent.ACTION_LOCKED_BOOT_COMPLETED.equals(action)
                && !Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)
                && !"android.intent.action.QUICKBOOT_POWERON".equals(action)) return;

        // Keep the broadcast alive while Ethernet/Wi-Fi is coming up.  Eight
        // seconds still fits inside BroadcastReceiver's short execution window.
        final PendingResult pending = goAsync();
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override public void run() {
                try {
                    Intent launch = new Intent(context, MainActivity.class);
                    launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
                    context.startActivity(launch);
                } catch (Throwable ignored) {
                    // Some vendor firmwares have a separate Autostart permission.
                    // The user can allow it in the TV box settings; normal manual
                    // launching continues to work even when the firmware blocks us.
                } finally {
                    pending.finish();
                }
            }
        }, START_DELAY_MS);
    }
}
