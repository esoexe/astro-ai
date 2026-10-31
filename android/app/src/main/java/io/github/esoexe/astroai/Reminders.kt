package io.github.esoexe.astroai

import android.app.AlarmManager
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.core.app.NotificationCompat
import io.github.esoexe.astroai.core.tr

/**
 * Reminders set in a chat or on the Tasks page ring as notifications at their time, even when the app is
 * closed or the phone restarted (the web version only worked while the page was open). Exact timing when
 * Android allows it, otherwise within a few minutes.
 */
class Reminders(private val app: AstroApp) {
    private val am = app.getSystemService(AlarmManager::class.java)

    /** Called by the activity, which shows the system prompt (notifications permission, Android 13+). */
    var askPermission: () -> Unit = {}

    private fun pending(id: String, text: String): PendingIntent = PendingIntent.getBroadcast(app, id.hashCode(),
        Intent(app, ReminderReceiver::class.java).putExtra("id", id).putExtra("text", text), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)

    fun scheduleAll() {
        val now = System.currentTimeMillis()
        for (r in app.store.reminders) {
            val pi = pending(r.id, r.text)
            if (r.fired) { am.cancel(pi); continue }
            val at = maxOf(r.at, now + 1000)
            val exact = Build.VERSION.SDK_INT < 31 || am.canScheduleExactAlarms()
            try { if (exact) am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi) else am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi) }
            catch (_: SecurityException) { am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi) }
        }
    }

    fun cancel(id: String, text: String) { am.cancel(pending(id, text)) }
}

class ReminderReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val app = context.applicationContext as AstroApp
        val id = intent.getStringExtra("id") ?: return
        val text = intent.getStringExtra("text") ?: tr("Reminder")
        val i = app.store.reminders.indexOfFirst { it.id == id }
        if (i >= 0) { app.store.reminders[i] = app.store.reminders[i].copy(fired = true); app.store.saveReminders() }
        if (!Work.canNotify(app)) return
        val n = NotificationCompat.Builder(app, Work.CH_REMIND).setSmallIcon(R.drawable.ic_stat_astro)
            .setContentTitle(tr("Reminder")).setContentText(text).setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setPriority(NotificationCompat.PRIORITY_HIGH).setCategory(NotificationCompat.CATEGORY_REMINDER)
            .setContentIntent(Work.openApp(app)).setAutoCancel(true).build()
        app.getSystemService(NotificationManager::class.java).notify(1000 + (id.hashCode() and 0xFFFF), n)
    }
}

/** Alarms are cleared when the phone restarts or the app is updated: set them again. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        // Exported for the system broadcasts below; anything else is ignored.
        if (intent.action != Intent.ACTION_BOOT_COMPLETED && intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        (context.applicationContext as AstroApp).reminders.scheduleAll()
    }
}
