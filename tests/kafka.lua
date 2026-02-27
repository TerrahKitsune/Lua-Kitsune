local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local kafkaConfig = {
    enabled = false,
    brokers = { "localhost:9092" }
}

run("Kafka table exists", function()
    assert_table(Kafka, "Kafka")
end)

if not kafkaConfig.enabled then
    skip("Kafka suite", "set kafkaConfig.enabled = true to run Kafka tests")
    return
end

run("Kafka.NewProducer creates instance", function()
    local kafka = Kafka.NewProducer()
    assert(kafka, "Kafka.NewProducer failed")
end)
