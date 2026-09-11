-- Edit this independent file to carry a personal review deck.
local cards = {
    {"What does EPD stand for?", "Electrophoretic display"},
    {"Binary 1010 in decimal?", "10"},
    {"How many bits in a byte?", "8"},
    {"What persists without power?", "Flash memory"},
    {"Why use a pull-up resistor?", "To define the idle logic level"},
    {"I2C uses which two signals?", "SDA and SCL"},
}
local index, revealed = 1, false

function on_event(key)
    if key == note4.OK then revealed = not revealed
    elseif key == note4.UP then index = (index - 2) % #cards + 1; revealed = false
    elseif key == note4.DOWN then index = index % #cards + 1; revealed = false
    else return false end
    return true
end

function on_render()
    note4.text(4, 4, "CARD " .. index .. " / " .. #cards)
    note4.rect(0, 32, note4.width, 107)
    note4.text(12, 59, cards[index][1])
    if revealed then note4.text(12, 102, cards[index][2])
    else note4.text(12, 102, "OK: reveal answer") end
    note4.text(4, 163, "UP/DOWN: card   OK: flip")
end
