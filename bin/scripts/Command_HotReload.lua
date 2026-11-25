-- Reload every Lua file from the configured scripts folder without restarting the proxy.

registerCommand("/hotreload", "Reload Lua scripts", function()
  if requestScriptReload() then
    log("[script] hot reload requested")
  else
    warn("[script] hot reload request failed")
  end
end)
