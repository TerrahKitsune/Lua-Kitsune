-- Connection configuration for database integration tests.
-- Copy this file, fill in your credentials, and set enabled = true.
-- This file is tracked in git as a safe template (enabled = false, localhost defaults).
-- Once you add your real credentials it will be ignored by git.

return {
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
    }
}
