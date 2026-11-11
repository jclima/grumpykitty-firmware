local last_second = -1
return {
  open = function()
    mk.ui.clear()
    mk.ui.header("Personal Utility", "Ready", "normal")
    mk.ui.footer("A Redraw", "Hold B Exit")
  end,
  update = function(now_ms, input)
    local second = math.floor(now_ms / 1000)
    if second ~= last_second or input.a then
      mk.ui.tiles({{label="Device uptime", value=tostring(second), unit="s", tone="normal"}})
      last_second = second
    end
  end,
  close = function() end
}
