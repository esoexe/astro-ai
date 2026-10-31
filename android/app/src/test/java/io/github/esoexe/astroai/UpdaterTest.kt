package io.github.esoexe.astroai

import io.github.esoexe.astroai.update.Updater
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class UpdaterTest {
    @Test fun versionOrder() {
        assertTrue(Updater.newer("1.8.1", "1.8.0"))
        assertTrue(Updater.newer("1.10.0", "1.9.9"))
        assertTrue(Updater.newer("2.0.0", "1.99.99"))
        assertFalse(Updater.newer("1.8.0", "1.8.0"))
        assertFalse(Updater.newer("1.8.0", "1.8.1"))
        assertFalse(Updater.newer("1.8.1", "1.8.1-debug"))   // the debug suffix is not a newer version
        assertFalse(Updater.newer("garbage", "1.8.1"))
    }
}
