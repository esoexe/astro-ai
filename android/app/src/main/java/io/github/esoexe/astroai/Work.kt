package io.github.esoexe.astroai

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import io.github.esoexe.astroai.core.tr

/**
 * Long work (One Brain answers can take minutes, Agent runs longer) keeps going when you switch apps: while
 * something is running and the app leaves the screen, a foreground service with a visible notification keeps
 * the process alive. When the work ends, the service stops and, if you are elsewhere, "Answer ready" is shown.
 */
class Work(private val app: AstroApp) {
    private var count = 0
    private var serviceOn = false
    val busy: Boolean get() = count > 0

    init { channels(app) }

    fun begin() { count++ }
    fun end() { count = maxOf(0, count - 1); if (count == 0) stopService() }

    /** Called when the activity pauses (still visible, so Android allows starting the service). */
    fun onLeave() {
        if (count == 0 || serviceOn) return
        try {
            ContextCompat.startForegroundService(app, Intent(app, WorkService::class.java))
            serviceOn = true
        } catch (_: Exception) { /* not allowed right now: the work continues while the process lives */ }
    }

    fun onReturn() { stopService() }

    private fun stopService() {
        if (!serviceOn) return
        serviceOn = false
        app.stopService(Intent(app, WorkService::class.java))
    }

    fun notifyDone(text: String) {
        if (!canNotify(app)) return
        val n = NotificationCompat.Builder(app, CH_DONE).setSmallIcon(R.drawable.ic_stat_astro)
            .setContentTitle(tr("Answer ready")).setContentText(text.replace(Regex("[#*`_>]+"), "").take(160))
            .setContentIntent(openApp(app)).setAutoCancel(true).build()
        app.getSystemService(NotificationManager::class.java).notify(2, n)
    }

    companion object {
        const val CH_WORK = "work"
        const val CH_DONE = "done"
        const val CH_REMIND = "reminders"

        fun channels(ctx: Context) {
            val nm = ctx.getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(NotificationChannel(CH_WORK, ctx.getString(R.string.channel_work), NotificationManager.IMPORTANCE_LOW))
            nm.createNotificationChannel(NotificationChannel(CH_DONE, tr("Answer ready"), NotificationManager.IMPORTANCE_DEFAULT))
            nm.createNotificationChannel(NotificationChannel(CH_REMIND, ctx.getString(R.string.channel_reminders), NotificationManager.IMPORTANCE_HIGH))
        }

        fun canNotify(ctx: Context) = Build.VERSION.SDK_INT < 33 || ContextCompat.checkSelfPermission(ctx, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

        fun openApp(ctx: Context): PendingIntent = PendingIntent.getActivity(ctx, 0,
            Intent(ctx, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
    }
}

class WorkService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val n: Notification = NotificationCompat.Builder(this, Work.CH_WORK).setSmallIcon(R.drawable.ic_stat_astro)
            .setContentTitle(tr("A.S.T.R.O AI is working…")).setContentText(tr("Your answer keeps going while you use other apps."))
            .setOngoing(true).setContentIntent(Work.openApp(this)).build()
        try {
            ServiceCompat.startForeground(this, 1, n, if (Build.VERSION.SDK_INT >= 34) ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE else 0)
        } catch (_: Exception) { stopSelf() }
        return START_NOT_STICKY
    }
}
