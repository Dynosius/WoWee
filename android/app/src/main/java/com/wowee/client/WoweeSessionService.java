package com.wowee.client;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

/**
 * Keeps the game session open while the app is in the background.
 *
 * Android freezes a backgrounded app's process within seconds, and its socket
 * to the world server goes with it: the server sees the client vanish and the
 * player comes back to the login screen. A process holding a foreground
 * service is not frozen. This service does no work of its own - the session
 * lives in the native client as it always does - it only marks the process as
 * one the player is still using, with the notification Android requires for
 * that.
 *
 * WoweeActivity starts it while it is in the foreground, which is the only
 * time Android allows, and stops it when the activity goes away.
 */
public class WoweeSessionService extends Service {

    private static final String CHANNEL_ID = "session";
    private static final int NOTIFICATION_ID = 1;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null && manager.getNotificationChannel(CHANNEL_ID) == null) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "Game session", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Shown while WoWee keeps its connection open in the background");
            manager.createNotificationChannel(channel);
        }

        Notification notification = new Notification.Builder(this, CHANNEL_ID)
                .setSmallIcon(getApplicationInfo().icon)
                .setContentTitle("WoWee")
                .setContentText("Staying connected while in the background")
                .setOngoing(true)
                .build();

        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIFICATION_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
        return START_NOT_STICKY;
    }

    /** Swiping the app away ends the session, so it ends this too. */
    @Override
    public void onTaskRemoved(Intent rootIntent) {
        stopSelf();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
