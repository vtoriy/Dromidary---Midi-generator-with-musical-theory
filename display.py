import board
import busio
import displayio
from i2cdisplaybus import I2CDisplayBus
import adafruit_displayio_sh1106


class DisplayManager:
    def __init__(self):
        displayio.release_displays()
        i2c = busio.I2C(board.GP1, board.GP0, frequency=800000)
        display_bus = I2CDisplayBus(i2c, device_address=0x3C)
        self.display = adafruit_displayio_sh1106.SH1106(
            display_bus, width=132, height=64
        )
        self.splash = displayio.Group()
        self.display.root_group = self.splash
