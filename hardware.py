# перепутаны провода на втором 74HC165 в диапазоне D4-D7
# сейчас кнопка №13 подключена к D7, а должна к D4
# решение (30.07.2026): оставить как есть

import board
import digitalio
import analogio

LATCH = digitalio.DigitalInOut(board.GP2)
LATCH.direction = digitalio.Direction.OUTPUT

CLK = digitalio.DigitalInOut(board.GP3)
CLK.direction = digitalio.Direction.OUTPUT

DATA = digitalio.DigitalInOut(board.GP4)
DATA.direction = digitalio.Direction.INPUT

joy_x = analogio.AnalogIn(board.GP26)
joy_y = analogio.AnalogIn(board.GP27)
joy_btn = digitalio.DigitalInOut(board.GP15)
joy_btn.direction = digitalio.Direction.INPUT
joy_btn.pull = digitalio.Pull.UP

JOY_CENTER = 32767
JOY_THRESHOLD = 15000


def read_all_chips():
    LATCH.value = False
    LATCH.value = True

    buttons = 0
    for _ in range(24):
        CLK.value = False
        buttons = (buttons << 1) | DATA.value
        CLK.value = True

    return buttons


def read_joystick():
    x = joy_x.value
    y = joy_y.value
    btn = not joy_btn.value

    direction = "CENTER"
    dx = ""
    dy = ""

    if x < JOY_CENTER - JOY_THRESHOLD:
        dx = "LEFT"
    elif x > JOY_CENTER + JOY_THRESHOLD:
        dx = "RIGHT"

    if y < JOY_CENTER - JOY_THRESHOLD:
        dy = "UP"
    elif y > JOY_CENTER + JOY_THRESHOLD:
        dy = "DOWN"

    if dx and dy:
        direction = f"{dy}-{dx}"
    elif dx:
        direction = dx
    elif dy:
        direction = dy

    return direction, btn
