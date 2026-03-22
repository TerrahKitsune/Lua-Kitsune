-- Central configuration for all optional test suites.
-- This file is tracked in git as a safe template (all suites disabled, localhost defaults).
-- Edit your local copy to enable suites and add real credentials — git will ignore your changes.

return {

    -- ── Databases ────────────────────────────────────────────────────────
    mysql = {
        enabled = false,
        host = "127.0.0.1",
        port = 3306,
        user = "root",
        password = "",
        database = "kitsune"
    },
    postgres = {
        enabled = false,
        conninfo = "host=127.0.0.1 user=postgres password= dbname=postgres connect_timeout=5"
    },
    odbc = {
        enabled = false,
        connectionString = ""
    },

    -- ── Messaging / brokers ──────────────────────────────────────────────
    redis = {
        enabled = false,
        host = "127.0.0.1",
        port = 6379,
        useTls = false,
        timeout = 10,
        sslOptions = nil
    },
    kafka = {
        enabled = false,
        brokers = { "localhost:9092" }
    },

    -- ── Network ──────────────────────────────────────────────────────────
    ftp = {
        enabled = false,
        host = "127.0.0.1",
        port = 21,
        user = "",
        password = ""
    },
    http = {
        enabled = false,
        url = "https://example.com"
    },
    server = {
        enabled = false,
        port = 5050
    },
    client = {
        enabled = false,
        host = "127.0.0.1",
        port = 5050
    },
    pipe = {
        enabled = false,
        name = "KitsuneTestPipe"
    },

    -- ── Hardware / UI (require a display or audio device) ────────────────
    image    = { enabled = false },
    imgui    = { enabled = false },
    sound    = { enabled = false },
    tts      = { enabled = false },

    -- ── System interaction ───────────────────────────────────────────────
    macro    = { enabled = false },
    process  = { enabled = false },
    services = { enabled = false },
}
