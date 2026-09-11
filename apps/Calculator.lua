-- UP/DOWN changes the selected digit or operator; OK advances to the next field.
local a, b, op, field = 0, 0, 1, 1
local signs = {"+", "-", "x", "/"}
local digits = {100, 10, 1}
local answer = ""

local function calculate()
    if op == 1 then return a + b end
    if op == 2 then return a - b end
    if op == 3 then return a * b end
    if b == 0 then return "Cannot divide by zero" end
    return a / b
end

function on_event(key)
    if key == note4.OK then
        if field == 8 then answer = calculate(); field = 9
        elseif field == 9 then field = 1; answer = ""
        else field = field + 1 end
    elseif key == note4.UP or key == note4.DOWN then
        local delta = key == note4.UP and 1 or -1
        if field <= 3 then
            local p = digits[field]
            local digit = (a // p) % 10
            a = a + (((digit + delta) % 10) - digit) * p
        elseif field == 4 then op = (op - 1 + delta) % 4 + 1
        elseif field <= 7 then
            local p = digits[field - 4]
            local digit = (b // p) % 10
            b = b + (((digit + delta) % 10) - digit) * p
        else return false end
    else return false end
    return true
end

function on_render()
    note4.text(4, 4, "UP/DOWN: change   OK: next")
    local x = {12, 42, 72, 121, 174, 204, 234, 305}
    for i = 1, 3 do
        note4.text(x[i], 52, (a // digits[i]) % 10, 2)
        note4.text(x[i + 4], 52, (b // digits[i]) % 10, 2)
    end
    note4.text(x[4], 52, signs[op], 2)
    note4.text(x[8], 52, "=", 2)
    if field < 9 then
        note4.rect(x[field] - 5, 44, 32, 43)
        note4.text(4, 124, field == 8 and "OK: calculate" or "Choose digits and an operator")
    else
        note4.text(4, 112, "Result: " .. answer, 1, note4.BOLD)
        note4.text(4, 153, "OK: edit again", 1, note4.KEYCAP)
    end
end
