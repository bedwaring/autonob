local autonob = require("autonob")

local sources = {"src/main.c"}
for _, file in ipairs(autonob.dir("thirdparty/lua")) do
  if file:match("%.c$") then
    table.insert(sources, "thirdparty/lua/" .. file)
  end
end

autonob.executable({
  name = "autonob",
  sources = sources,
  flags = {"-O2", "-std=c17", "-Ithirdparty/lua", "-lm"}
})
