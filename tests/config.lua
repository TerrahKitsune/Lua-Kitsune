-- Central configuration for all optional test suites.
-- This file is tracked in git as a safe template (all suites disabled, localhost defaults).
-- Edit your local copy to enable suites and add real credentials — git will ignore your changes.

return {

    -- ── Databases ────────────────────────────────────────────────────────
    mysql = {
        enabled = true,
        host = "10.9.23.252",
        port = 3306,
        user = "kitsune",
        password = "Pizza123!",
        database = "kitsune"
    },
    postgres = {
        enabled = true,
        conninfo = "host=10.9.23.252 user=postgres password=hej123 dbname=postgres connect_timeout=5"
    },
    odbc = {
        enabled = false,
        connectionString = ""
    },

    -- ── Messaging / brokers ──────────────────────────────────────────────
    redis = {
        enabled = true,
        host = "10.9.23.252",
        port = 6379,
        useTls = false,
        timeout = 10,
        password = "hej123",
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

    -- ── Always-on (no external dependencies) ─────────────────────────────
    utils      = { enabled = true },
    aes        = { enabled = true },
    archive    = { enabled = true },
    base64     = { enabled = true },
    console    = { enabled = true },
    csv        = { enabled = true },
    env        = { enabled = true },
    erf        = { enabled = true },
    fileasync  = { enabled = true },
    filesystem = { enabled = true },
    gff        = { enabled = true },
    hashing    = { enabled = true },
    json       = { enabled = true },
    mutex      = { enabled = true },
    sqlite     = { enabled = true },
    stream     = { enabled = true },
    timer      = { enabled = true },
    tlk        = { enabled = true },
    twoda      = { enabled = true },
    wchar      = { enabled = true },
    zip        = { enabled = true },
}
