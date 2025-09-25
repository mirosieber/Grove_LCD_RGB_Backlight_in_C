#include "rgb_lcd.h"
#include <string.h>

// Private function prototypes
static void i2c_send_byte(rgb_lcd_t* lcd, uint8_t dta);
static void i2c_send_bytes(rgb_lcd_t* lcd, uint8_t* dta, uint8_t len);
static void lcd_command(rgb_lcd_t* lcd, uint8_t value);
static void set_register(rgb_lcd_t* lcd, uint8_t reg, uint8_t data);

// Color definitions
static const uint8_t color_define[4][3] = {
    {255, 255, 255},  // white
    {255, 0, 0},      // red
    {0, 255, 0},      // green
    {0, 0, 255},      // blue
};

void rgb_lcd_init(rgb_lcd_t* lcd, uint8_t cols, uint8_t lines, uint8_t dotsize, I2C_HandleTypeDef* hi2c) {
    // Initialize LCD structure
    memset(lcd, 0, sizeof(rgb_lcd_t));
    lcd->hi2c = hi2c;
    lcd->initialized = 1;

    if (lines > 1) {
        lcd->displayfunction |= LCD_2LINE;
    }
    lcd->numlines = lines;
    lcd->currline = 0;

    // For some 1 line displays you can select a 10 pixel high font
    if ((dotsize != 0) && (lines == 1)) {
        lcd->displayfunction |= LCD_5x10DOTS;
    }

    // Wait for LCD to power up
    HAL_Delay(50);

    // Send function set command sequence
    lcd_command(lcd, LCD_FUNCTIONSET | lcd->displayfunction);
    HAL_Delay(5);  // wait more than 4.1ms

    // Second try
    lcd_command(lcd, LCD_FUNCTIONSET | lcd->displayfunction);
    HAL_Delay(1);

    // Third try
    lcd_command(lcd, LCD_FUNCTIONSET | lcd->displayfunction);

    // Finally, set # lines, font size, etc.
    lcd_command(lcd, LCD_FUNCTIONSET | lcd->displayfunction);

    // Turn the display on with no cursor or blinking default
    lcd->displaycontrol = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
    rgb_lcd_display(lcd);

    // Clear display
    rgb_lcd_clear(lcd);

    // Initialize to default text direction
    lcd->displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
    lcd_command(lcd, LCD_ENTRYMODESET | lcd->displaymode);

    // Check RGB chip model by trying to communicate with V5 address
    uint8_t tx_data = 0x00;
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(lcd->hi2c, RGB_ADDRESS_V5 << 1, &tx_data, 1, 100);

    if (status == HAL_OK) {
        lcd->rgb_chip_addr = RGB_ADDRESS_V5;
        set_register(lcd, 0x00, 0x07); // reset the chip
        HAL_Delay(1); // wait 1ms to complete
        set_register(lcd, 0x04, 0x15); // set all led always on
    } else {
        lcd->rgb_chip_addr = RGB_ADDRESS;
        // Backlight init
        set_register(lcd, REG_MODE1, 0);
        // Set LEDs controllable by both PWM and GRPPWM registers
        set_register(lcd, REG_OUTPUT, 0xFF);
        // Set MODE2 values
        set_register(lcd, REG_MODE2, 0x20); // 0010 0000 (DMBLNK to 1, ie blinky mode)
    }

    rgb_lcd_set_color_white(lcd);
}

// High level commands
void rgb_lcd_clear(rgb_lcd_t* lcd) {
    lcd_command(lcd, LCD_CLEARDISPLAY);
    HAL_Delay(2); // This command takes a long time!
}

void rgb_lcd_home(rgb_lcd_t* lcd) {
    lcd_command(lcd, LCD_RETURNHOME);
    HAL_Delay(2); // This command takes a long time!
}

void rgb_lcd_set_cursor(rgb_lcd_t* lcd, uint8_t col, uint8_t row) {
    col = (row == 0 ? col | 0x80 : col | 0xc0);
    uint8_t dta[2] = {0x80, col};
    i2c_send_bytes(lcd, dta, 2);
}

void rgb_lcd_no_display(rgb_lcd_t* lcd) {
    lcd->displaycontrol &= ~LCD_DISPLAYON;
    lcd_command(lcd, LCD_DISPLAYCONTROL | lcd->displaycontrol);
}

void rgb_lcd_display(rgb_lcd_t* lcd) {
    lcd->displaycontrol |= LCD_DISPLAYON;
    lcd_command(lcd, LCD_DISPLAYCONTROL | lcd->displaycontrol);
}

void rgb_lcd_no_cursor(rgb_lcd_t* lcd) {
    lcd->displaycontrol &= ~LCD_CURSORON;
    lcd_command(lcd, LCD_DISPLAYCONTROL | lcd->displaycontrol);
}

void rgb_lcd_cursor(rgb_lcd_t* lcd) {
    lcd->displaycontrol |= LCD_CURSORON;
    lcd_command(lcd, LCD_DISPLAYCONTROL | lcd->displaycontrol);
}

void rgb_lcd_no_blink(rgb_lcd_t* lcd) {
    lcd->displaycontrol &= ~LCD_BLINKON;
    lcd_command(lcd, LCD_DISPLAYCONTROL | lcd->displaycontrol);
}

void rgb_lcd_blink(rgb_lcd_t* lcd) {
    lcd->displaycontrol |= LCD_BLINKON;
    lcd_command(lcd, LCD_DISPLAYCONTROL | lcd->displaycontrol);
}

void rgb_lcd_scroll_display_left(rgb_lcd_t* lcd) {
    lcd_command(lcd, LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
}

void rgb_lcd_scroll_display_right(rgb_lcd_t* lcd) {
    lcd_command(lcd, LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
}

void rgb_lcd_left_to_right(rgb_lcd_t* lcd) {
    lcd->displaymode |= LCD_ENTRYLEFT;
    lcd_command(lcd, LCD_ENTRYMODESET | lcd->displaymode);
}

void rgb_lcd_right_to_left(rgb_lcd_t* lcd) {
    lcd->displaymode &= ~LCD_ENTRYLEFT;
    lcd_command(lcd, LCD_ENTRYMODESET | lcd->displaymode);
}

void rgb_lcd_autoscroll(rgb_lcd_t* lcd) {
    lcd->displaymode |= LCD_ENTRYSHIFTINCREMENT;
    lcd_command(lcd, LCD_ENTRYMODESET | lcd->displaymode);
}

void rgb_lcd_no_autoscroll(rgb_lcd_t* lcd) {
    lcd->displaymode &= ~LCD_ENTRYSHIFTINCREMENT;
    lcd_command(lcd, LCD_ENTRYMODESET | lcd->displaymode);
}

void rgb_lcd_create_char(rgb_lcd_t* lcd, uint8_t location, uint8_t charmap[]) {
    location &= 0x7; // we only have 8 locations 0-7
    lcd_command(lcd, LCD_SETCGRAMADDR | (location << 3));

    uint8_t dta[9];
    dta[0] = 0x40;
    for (int i = 0; i < 8; i++) {
        dta[i + 1] = charmap[i];
    }
    i2c_send_bytes(lcd, dta, 9);
}

void rgb_lcd_write_char(rgb_lcd_t* lcd, uint8_t value) {
    uint8_t dta[2] = {0x40, value};
    i2c_send_bytes(lcd, dta, 2);
}

void rgb_lcd_write_string(rgb_lcd_t* lcd, const char* str) {
    while (*str) {
        rgb_lcd_write_char(lcd, *str++);
    }
}

void rgb_lcd_set_rgb(rgb_lcd_t* lcd, uint8_t r, uint8_t g, uint8_t b) {
    if (lcd->rgb_chip_addr == RGB_ADDRESS_V5) {
        set_register(lcd, 0x06, r);
        set_register(lcd, 0x07, g);
        set_register(lcd, 0x08, b);
    } else {
        set_register(lcd, 0x04, r);
        set_register(lcd, 0x03, g);
        set_register(lcd, 0x02, b);
    }
}

void rgb_lcd_set_pwm(rgb_lcd_t* lcd, uint8_t color, uint8_t pwm) {
    switch (color) {
        case WHITE:
            rgb_lcd_set_rgb(lcd, pwm, pwm, pwm);
            break;
        case RED:
            rgb_lcd_set_rgb(lcd, pwm, 0, 0);
            break;
        case GREEN:
            rgb_lcd_set_rgb(lcd, 0, pwm, 0);
            break;
        case BLUE:
            rgb_lcd_set_rgb(lcd, 0, 0, pwm);
            break;
        default:
            break;
    }
}

void rgb_lcd_set_color(rgb_lcd_t* lcd, uint8_t color) {
    if (color > 3) return;
    rgb_lcd_set_rgb(lcd, color_define[color][0], color_define[color][1], color_define[color][2]);
}

void rgb_lcd_set_color_white(rgb_lcd_t* lcd) {
    rgb_lcd_set_rgb(lcd, 255, 255, 255);
}

void rgb_lcd_blink_led(rgb_lcd_t* lcd) {
    if (lcd->rgb_chip_addr == RGB_ADDRESS_V5) {
        // Attach all LED to pwm1
        set_register(lcd, 0x04, 0x2a);  // 0010 1010
        set_register(lcd, 0x01, 0x06);  // blink every second
        set_register(lcd, 0x02, 0x7f);  // half on, half off
    } else {
        set_register(lcd, 0x07, 0x17);  // blink every second
        set_register(lcd, 0x06, 0x7f);  // half on, half off
    }
}

void rgb_lcd_no_blink_led(rgb_lcd_t* lcd) {
    if (lcd->rgb_chip_addr == RGB_ADDRESS_V5) {
        set_register(lcd, 0x04, 0x15);  // 0001 0101
    } else {
        set_register(lcd, 0x07, 0x00);
        set_register(lcd, 0x06, 0xff);
    }
}

// Private functions
static void i2c_send_byte(rgb_lcd_t* lcd, uint8_t dta) {
    if (!lcd->initialized) return;
    HAL_I2C_Master_Transmit(lcd->hi2c, LCD_ADDRESS << 1, &dta, 1, 100);
}

static void i2c_send_bytes(rgb_lcd_t* lcd, uint8_t* dta, uint8_t len) {
    if (!lcd->initialized) return;
    HAL_I2C_Master_Transmit(lcd->hi2c, LCD_ADDRESS << 1, dta, len, 100);
}

static void lcd_command(rgb_lcd_t* lcd, uint8_t value) {
    uint8_t dta[2] = {0x80, value};
    i2c_send_bytes(lcd, dta, 2);
}

static void set_register(rgb_lcd_t* lcd, uint8_t reg, uint8_t data) {
    if (!lcd->initialized) return;
    uint8_t tx_data[2] = {reg, data};
    HAL_I2C_Master_Transmit(lcd->hi2c, lcd->rgb_chip_addr << 1, tx_data, 2, 100);
}
