/*
 * MT62XX Keypad Driver with T2Input State Machine
 *
 * Implements a two-click positional input system for efficient text entry
 * on numeric keypads. The state machine runs in kernel space for zero-latency
 * key processing and direct fbcon integration.
 *
 * Hardware: MT62xx Keypad Scanner at 0xA00D0000, IRQ 16
 *
 * T2Input mechanism (from TelnetVXP/T2Input.cpp):
 *   - First numpad press: stores row index
 *   - Second numpad press: outputs character at [row][column]
 *   - No timeout needed, deterministic 2-press per character
 *   - Modes: SMALL (lowercase), FIRST_BIG (one uppercase), BIG (all caps), NUM (numbers)
 *
 * Physical keypad layout:
 *   Row 0: 1, 2, 3, LEFT_SOFTKEY, UP, RIGHT_SOFTKEY
 *   Row 1: 4, 5, 6, LEFT, CENTER, RIGHT
 *   Row 2: 7, 8, 9, SEND, DOWN, END
 *   Row 3: STAR, 0, HASH, FUNCTION, VOL_UP, VOL_DOWN
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>

#define DRIVER_NAME "mt62xx-keypad"

/* Hardware registers */
#define KP_STS       0x00
#define KP_LOW_KEY   0x04
#define KP_MID_KEY   0x08
#define KP_HI_KEY    0x0C
#define KP_DEBOUNCE  0x18

/* CIRQ (Interrupt Controller) registers */
#define CIRQ_BASE        0xA0060000
#define IRQ_SENS_SET0    (CIRQ_BASE + 0x0080)  /* Set edge-sensitive for IRQ 0-31 */

/* T2Input state machine */
enum input_mode { SMALL, FIRST_BIG, BIG, NUM };
enum state { MAIN, SECOND_CLICK, SET_MENU, F_NUM, CTRL, CTRL_SECOND_CLICK };

/* Physical key definitions - maximum bit position we support */
#define PHY_KEY_MAX 48

/* VM_KEY equivalents for T2Input logic */
enum vm_key {
    VM_KEY_NUM0 = 0, VM_KEY_NUM1, VM_KEY_NUM2, VM_KEY_NUM3,
    VM_KEY_NUM4, VM_KEY_NUM5, VM_KEY_NUM6, VM_KEY_NUM7,
    VM_KEY_NUM8, VM_KEY_NUM9,
    VM_KEY_UP, VM_KEY_DOWN, VM_KEY_LEFT, VM_KEY_RIGHT,
    VM_KEY_LEFT_SOFTKEY, VM_KEY_RIGHT_SOFTKEY,
    VM_KEY_OK, VM_KEY_STAR, VM_KEY_POUND, VM_KEY_SEND, VM_KEY_END,
    VM_KEY_MAX
};

struct mt62xx_keypad {
    void __iomem *base;
    struct input_dev *input;
    int irq;

    /* T2Input state machine */
    enum state state;
    enum input_mode cur_input_mode;
    int last_input_key;

    /* Key state tracking for press/release detection */
    u16 prev_keymap[3];
};

/* Two-click keyboard layouts (from T2Input.cpp) */
static const char *normal_keyboard[10][10] = {
    {"0", " ", "_", "\\", "/", "=", ":", ";", "<", ">"},
    {"1", ".", ",", "\'", "?", "!", "\"", "(", ")", "+"},
    {"2", "a", "b", "c", "-", "$", "%", "^", "[", "]"},
    {"3", "d", "e", "f", "{", "}", "*", "&", "#", "@"},
    {"4", "g", "h", "i", "", "", "", "", "", ""},
    {"5", "j", "k", "l", "", "", "", "", "", ""},
    {"6", "m", "n", "o", "", "", "", "", "", ""},
    {"7", "p", "q", "r", "s", "", "", "", "", ""},
    {"8", "t", "u", "v", "", "", "", "", "", ""},
    {"9", "w", "x", "y", "z", "", "", "", "", ""},
};

static const char *big_keyboard[10][10] = {
    {"0", " ", "_", "\\", "/", "=", ":", ";", "<", ">"},
    {"1", ".", ",", "\'", "?", "!", "\"", "(", ")", "+"},
    {"2", "A", "B", "C", "-", "$", "%", "^", "[", "]"},
    {"3", "D", "E", "F", "{", "}", "*", "&", "#", "@"},
    {"4", "G", "H", "I", "", "", "", "", "", ""},
    {"5", "J", "K", "L", "", "", "", "", "", ""},
    {"6", "M", "N", "O", "", "", "", "", "", ""},
    {"7", "P", "Q", "R", "S", "", "", "", "", ""},
    {"8", "T", "U", "V", "", "", "", "", "", ""},
    {"9", "W", "X", "Y", "Z", "", "", "", "", ""},
};

static const char *num_keyboard[10] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
};

/* Control character codes */
struct ctrl_code {
    const char *name;
    unsigned char code;
};

static const struct ctrl_code ctrl_keyboard[10][10] = {
    {{"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
    {{"", 0}, {"^[", 27}, {"^]", 29}, {"^\\", 28}, {"^^", 30}, {"^_", 31}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
    {{"", 0}, {"^A", 1}, {"^B", 2}, {"^C", 3}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
    {{"", 0}, {"^D", 4}, {"^E", 5}, {"^F", 6}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
    {{"", 0}, {"^G", 7}, {"^H", 8}, {"^I", 9}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
    {{"", 0}, {"^J", 10}, {"^K", 11}, {"^L", 12}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
    {{"", 0}, {"^M", 13}, {"^N", 14}, {"^O", 15}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
    {{"", 0}, {"^P", 16}, {"^Q", 17}, {"^R", 18}, {"^S", 19}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
    {{"", 0}, {"^T", 20}, {"^U", 21}, {"^V", 22}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
    {{"", 0}, {"^W", 23}, {"^X", 24}, {"^Y", 25}, {"^Z", 26}, {"", 0}, {"", 0}, {"", 0}, {"", 0}, {"", 0}},
};

/* Physical key to VM_KEY mapping - based on actual Nokia 225 hardware testing */
static int physical_to_vm_key(int phy_key)
{
    /* Hardware uses active-low: bit=1 means no key, bit=0 means key pressed
     * Actual mapping from device testing:
     * Bits 0-15: KP_LOW_KEY register
     * Bits 16-31: KP_MID_KEY register
     * Bits 32-47: KP_HI_KEY register
     */
    switch (phy_key) {
        /* Navigation cluster */
        case 2: return VM_KEY_LEFT;
        case 4: return VM_KEY_RIGHT;
        case 11: return VM_KEY_UP;
        case 12: return VM_KEY_OK;
        case 13: return VM_KEY_DOWN;

        /* Soft keys and call keys (from actual device testing) */
        case 1: return VM_KEY_LEFT_SOFTKEY;
        case 3: return VM_KEY_RIGHT_SOFTKEY;
        case 10: return VM_KEY_SEND;
        case 17: return VM_KEY_END;

        /* Number keys */
        case 19: return VM_KEY_NUM1;
        case 28: return VM_KEY_NUM2;
        case 37: return VM_KEY_NUM3;
        case 20: return VM_KEY_NUM4;
        case 29: return VM_KEY_NUM5;
        case 38: return VM_KEY_NUM6;
        case 21: return VM_KEY_NUM7;
        case 30: return VM_KEY_NUM8;
        case 39: return VM_KEY_NUM9;
        case 31: return VM_KEY_NUM0;

        /* Special keys */
        case 22: return VM_KEY_STAR;
        case 40: return VM_KEY_POUND;

        default: return -1;
    }
}

/* ASCII character to Linux keycode mapping */
static int char_to_keycode(char c)
{
    switch (c) {
        case 'a': case 'A': return KEY_A;
        case 'b': case 'B': return KEY_B;
        case 'c': case 'C': return KEY_C;
        case 'd': case 'D': return KEY_D;
        case 'e': case 'E': return KEY_E;
        case 'f': case 'F': return KEY_F;
        case 'g': case 'G': return KEY_G;
        case 'h': case 'H': return KEY_H;
        case 'i': case 'I': return KEY_I;
        case 'j': case 'J': return KEY_J;
        case 'k': case 'K': return KEY_K;
        case 'l': case 'L': return KEY_L;
        case 'm': case 'M': return KEY_M;
        case 'n': case 'N': return KEY_N;
        case 'o': case 'O': return KEY_O;
        case 'p': case 'P': return KEY_P;
        case 'q': case 'Q': return KEY_Q;
        case 'r': case 'R': return KEY_R;
        case 's': case 'S': return KEY_S;
        case 't': case 'T': return KEY_T;
        case 'u': case 'U': return KEY_U;
        case 'v': case 'V': return KEY_V;
        case 'w': case 'W': return KEY_W;
        case 'x': case 'X': return KEY_X;
        case 'y': case 'Y': return KEY_Y;
        case 'z': case 'Z': return KEY_Z;
        case '0': return KEY_0;
        case '1': return KEY_1;
        case '2': return KEY_2;
        case '3': return KEY_3;
        case '4': return KEY_4;
        case '5': return KEY_5;
        case '6': return KEY_6;
        case '7': return KEY_7;
        case '8': return KEY_8;
        case '9': return KEY_9;
        case ' ': return KEY_SPACE;
        case '.': return KEY_DOT;
        case ',': return KEY_COMMA;
        case '\'': return KEY_APOSTROPHE;
        case '?': return KEY_SLASH;
        case '!': return KEY_1; /* Shift+1 */
        case '"': return KEY_APOSTROPHE; /* Shift+' */
        case '(': return KEY_9; /* Shift+9 */
        case ')': return KEY_0; /* Shift+0 */
        case '+': return KEY_EQUAL; /* Shift+= */
        case '-': return KEY_MINUS;
        case '$': return KEY_4; /* Shift+4 */
        case '%': return KEY_5; /* Shift+5 */
        case '^': return KEY_6; /* Shift+6 */
        case '&': return KEY_7; /* Shift+7 */
        case '*': return KEY_8; /* Shift+8 */
        case '#': return KEY_3; /* Shift+3 */
        case '@': return KEY_2; /* Shift+2 */
        case '=': return KEY_EQUAL;
        case ':': return KEY_SEMICOLON; /* Shift+; */
        case ';': return KEY_SEMICOLON;
        case '<': return KEY_COMMA; /* Shift+, */
        case '>': return KEY_DOT; /* Shift+. */
        case '/': return KEY_SLASH;
        case '\\': return KEY_BACKSLASH;
        case '_': return KEY_MINUS; /* Shift+- */
        case '[': return KEY_LEFTBRACE;
        case ']': return KEY_RIGHTBRACE;
        case '{': return KEY_LEFTBRACE; /* Shift+[ */
        case '}': return KEY_RIGHTBRACE; /* Shift+] */
        case '\n': case '\r': return KEY_ENTER;
        case '\t': return KEY_TAB;
        case 127: return KEY_BACKSPACE; /* DEL */
        default: return KEY_RESERVED;
    }
}

/* Emit a character as key press/release events */
static void emit_char(struct mt62xx_keypad *kp, char c)
{
    int keycode = char_to_keycode(c);
    bool need_shift = false;

    if (keycode == KEY_RESERVED)
        return;

    /* Handle shift for uppercase letters and special characters */
    if (c >= 'A' && c <= 'Z') {
        need_shift = true;
    } else {
        switch (c) {
            case '!': case '"': case '(': case ')': case '+':
            case '$': case '%': case '^': case '&': case '*':
            case '#': case '@': case ':': case '<': case '>':
            case '_': case '{': case '}':
                need_shift = true;
                break;
        }
    }

    if (need_shift) {
        input_report_key(kp->input, KEY_LEFTSHIFT, 1);
        input_sync(kp->input);
    }

    input_report_key(kp->input, keycode, 1);
    input_sync(kp->input);
    input_report_key(kp->input, keycode, 0);
    input_sync(kp->input);

    if (need_shift) {
        input_report_key(kp->input, KEY_LEFTSHIFT, 0);
        input_sync(kp->input);
    }
}

/* Emit escape sequence for arrow keys */
static void emit_escape_seq(struct mt62xx_keypad *kp, int keycode)
{
    input_report_key(kp->input, keycode, 1);
    input_sync(kp->input);
    input_report_key(kp->input, keycode, 0);
    input_sync(kp->input);
}

/* Handle special keys (arrows, enter, backspace, mode switching) */
static void handle_special_keys(struct mt62xx_keypad *kp, int vm_key)
{
    switch (vm_key) {
        case VM_KEY_UP:
            emit_escape_seq(kp, KEY_UP);
            break;
        case VM_KEY_DOWN:
            emit_escape_seq(kp, KEY_DOWN);
            break;
        case VM_KEY_RIGHT:
            emit_escape_seq(kp, KEY_RIGHT);
            break;
        case VM_KEY_LEFT:
            emit_escape_seq(kp, KEY_LEFT);
            break;

        case VM_KEY_OK:
            emit_char(kp, '\n');
            break;

        case VM_KEY_LEFT_SOFTKEY:
            /* Left softkey = cancel/reset state */
            if (kp->state == CTRL_SECOND_CLICK) {
                kp->state = CTRL;
            } else {
                kp->state = MAIN;
            }
            break;

        case VM_KEY_RIGHT_SOFTKEY:
            /* Right softkey = backspace */
            emit_char(kp, 127);
            break;

        case VM_KEY_POUND:
            if (kp->state == MAIN) {
                /* Cycle input mode: SMALL -> FIRST_BIG -> BIG -> NUM -> SMALL */
                kp->cur_input_mode = (kp->cur_input_mode + 1) % 4;
            }
            break;

        case VM_KEY_STAR:
            if (kp->state == MAIN) {
                kp->state = SET_MENU;
            } else {
                kp->state = MAIN;
            }
            break;

        case VM_KEY_SEND:
            emit_char(kp, '\t');  /* Call key = Tab for shell convenience */
            break;

        case VM_KEY_END:
            /* Could be used for special function */
            break;
    }
}

/* Process a key event through the T2Input state machine */
static void process_key_event(struct mt62xx_keypad *kp, int phy_key, int pressed)
{
    int vm_key = physical_to_vm_key(phy_key);

    if (vm_key < 0)
        return;

    /* Only process on key release (like T2Input does) */
    if (pressed)
        return;

    /* Handle numpad input through state machine */
    if (vm_key >= VM_KEY_NUM0 && vm_key <= VM_KEY_NUM9) {
        int num = vm_key - VM_KEY_NUM0;

        switch (kp->state) {
            case MAIN:
            case SECOND_CLICK:
                if (kp->cur_input_mode == NUM) {
                    /* Direct number output */
                    emit_char(kp, num_keyboard[num][0]);
                    kp->state = MAIN;
                } else if (kp->state == MAIN) {
                    /* First click: store row index */
                    kp->last_input_key = num;
                    kp->state = SECOND_CLICK;
                } else {
                    /* Second click: output character at [row][column] */
                    const char *c;
                    if (kp->cur_input_mode == SMALL || kp->cur_input_mode == FIRST_BIG) {
                        c = normal_keyboard[kp->last_input_key][num];
                    } else {
                        c = big_keyboard[kp->last_input_key][num];
                    }

                    if (c[0] != '\0') {
                        emit_char(kp, c[0]);
                    }

                    /* Auto-revert FIRST_BIG to SMALL after one character */
                    if (kp->cur_input_mode == FIRST_BIG) {
                        kp->cur_input_mode = SMALL;
                    }

                    kp->state = MAIN;
                }
                break;

            case SET_MENU:
                /* SET_MENU options: 1=F_NUM, 2=CTRL, 3=TAB */
                switch (num) {
                    case 1:
                        kp->state = F_NUM;
                        break;
                    case 2:
                        kp->state = CTRL;
                        break;
                    case 3:
                        emit_char(kp, '\t');
                        kp->state = MAIN;
                        break;
                }
                break;

            case F_NUM:
                /* Function keys F1-F12 via numpad */
                /* For now, just emit F1-F9 */
                if (num >= 1 && num <= 9) {
                    emit_escape_seq(kp, KEY_F1 + num - 1);
                }
                kp->state = MAIN;
                break;

            case CTRL:
            case CTRL_SECOND_CLICK:
                if (kp->state == CTRL) {
                    /* First click: store row */
                    kp->last_input_key = num;
                    kp->state = CTRL_SECOND_CLICK;
                } else {
                    /* Second click: output control character */
                    if (ctrl_keyboard[kp->last_input_key][num].code != 0) {
                        emit_char(kp, ctrl_keyboard[kp->last_input_key][num].code);
                    }
                    kp->state = CTRL;
                }
                break;
        }
        return;
    }

    /* Handle special keys */
    handle_special_keys(kp, vm_key);
}

/* Interrupt handler - reads key state and detects press/release */
static irqreturn_t mt62xx_keypad_irq(int irq, void *dev_id)
{
    struct mt62xx_keypad *kp = dev_id;
    u16 keymap[3];
    int i, bit;

    /* Read current key state from hardware registers */
    keymap[0] = readw_relaxed(kp->base + KP_LOW_KEY);
    keymap[1] = readw_relaxed(kp->base + KP_MID_KEY);
    keymap[2] = readw_relaxed(kp->base + KP_HI_KEY);

    /* Compare with previous state to detect press/release
     * Hardware uses active-low: bit=1 means no key, bit=0 means key pressed
     * So when bit goes 1→0, key was pressed; when bit goes 0→1, key was released */
    for (i = 0; i < 3; i++) {
        u16 pressed = ~keymap[i] & kp->prev_keymap[i];  /* 1→0 transitions */
        u16 released = keymap[i] & ~kp->prev_keymap[i]; /* 0→1 transitions */

        /* Process each bit */
        for (bit = 0; bit < 16; bit++) {
            int phy_key = i * 16 + bit;

            if (phy_key >= PHY_KEY_MAX)
                break;

            if (pressed & (1 << bit)) {
                process_key_event(kp, phy_key, 1);
            }
            if (released & (1 << bit)) {
                process_key_event(kp, phy_key, 0);
            }
        }
    }

    /* Save state for next interrupt */
    kp->prev_keymap[0] = keymap[0];
    kp->prev_keymap[1] = keymap[1];
    kp->prev_keymap[2] = keymap[2];

    return IRQ_HANDLED;
}

static int mt62xx_keypad_probe(struct platform_device *pdev)
{
    struct mt62xx_keypad *kp;
    struct input_dev *input;
    struct resource *res;
    int ret;

    kp = devm_kzalloc(&pdev->dev, sizeof(*kp), GFP_KERNEL);
    if (!kp)
        return -ENOMEM;

    /* Map keypad registers */
    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "keypad-regs");
    if (!res) {
        dev_err(&pdev->dev, "no keypad-regs resource\n");
        return -ENODEV;
    }

    kp->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(kp->base)) {
        ret = PTR_ERR(kp->base);
        dev_err(&pdev->dev, "failed to map keypad registers: %d\n", ret);
        return ret;
    }

    pr_info("mt62xx-keypad: registers mapped at %p\n", kp->base);

    /* Allocate input device */
    input = devm_input_allocate_device(&pdev->dev);
    if (!input) {
        dev_err(&pdev->dev, "failed to allocate input device\n");
        return -ENOMEM;
    }

    input->name = "mt62xx-keypad";
    input->phys = "mt62xx-keypad/input0";
    input->id.bustype = BUS_HOST;
    input->id.vendor = 0x0001;
    input->id.product = 0x0001;
    input->id.version = 0x0100;

    /* Set supported event types */
    set_bit(EV_KEY, input->evbit);
    set_bit(EV_REP, input->evbit); /* Enable auto-repeat */

    /* Set supported keys */
    set_bit(KEY_A, input->keybit);
    set_bit(KEY_B, input->keybit);
    set_bit(KEY_C, input->keybit);
    set_bit(KEY_D, input->keybit);
    set_bit(KEY_E, input->keybit);
    set_bit(KEY_F, input->keybit);
    set_bit(KEY_G, input->keybit);
    set_bit(KEY_H, input->keybit);
    set_bit(KEY_I, input->keybit);
    set_bit(KEY_J, input->keybit);
    set_bit(KEY_K, input->keybit);
    set_bit(KEY_L, input->keybit);
    set_bit(KEY_M, input->keybit);
    set_bit(KEY_N, input->keybit);
    set_bit(KEY_O, input->keybit);
    set_bit(KEY_P, input->keybit);
    set_bit(KEY_Q, input->keybit);
    set_bit(KEY_R, input->keybit);
    set_bit(KEY_S, input->keybit);
    set_bit(KEY_T, input->keybit);
    set_bit(KEY_U, input->keybit);
    set_bit(KEY_V, input->keybit);
    set_bit(KEY_W, input->keybit);
    set_bit(KEY_X, input->keybit);
    set_bit(KEY_Y, input->keybit);
    set_bit(KEY_Z, input->keybit);

    set_bit(KEY_0, input->keybit);
    set_bit(KEY_1, input->keybit);
    set_bit(KEY_2, input->keybit);
    set_bit(KEY_3, input->keybit);
    set_bit(KEY_4, input->keybit);
    set_bit(KEY_5, input->keybit);
    set_bit(KEY_6, input->keybit);
    set_bit(KEY_7, input->keybit);
    set_bit(KEY_8, input->keybit);
    set_bit(KEY_9, input->keybit);

    set_bit(KEY_SPACE, input->keybit);
    set_bit(KEY_ENTER, input->keybit);
    set_bit(KEY_BACKSPACE, input->keybit);
    set_bit(KEY_TAB, input->keybit);

    set_bit(KEY_UP, input->keybit);
    set_bit(KEY_DOWN, input->keybit);
    set_bit(KEY_LEFT, input->keybit);
    set_bit(KEY_RIGHT, input->keybit);

    set_bit(KEY_LEFTSHIFT, input->keybit);

    set_bit(KEY_F1, input->keybit);
    set_bit(KEY_F2, input->keybit);
    set_bit(KEY_F3, input->keybit);
    set_bit(KEY_F4, input->keybit);
    set_bit(KEY_F5, input->keybit);
    set_bit(KEY_F6, input->keybit);
    set_bit(KEY_F7, input->keybit);
    set_bit(KEY_F8, input->keybit);
    set_bit(KEY_F9, input->keybit);

    set_bit(KEY_DOT, input->keybit);
    set_bit(KEY_COMMA, input->keybit);
    set_bit(KEY_APOSTROPHE, input->keybit);
    set_bit(KEY_SLASH, input->keybit);
    set_bit(KEY_MINUS, input->keybit);
    set_bit(KEY_EQUAL, input->keybit);
    set_bit(KEY_SEMICOLON, input->keybit);
    set_bit(KEY_BACKSLASH, input->keybit);
    set_bit(KEY_LEFTBRACE, input->keybit);
    set_bit(KEY_RIGHTBRACE, input->keybit);

    /* Register input device */
    ret = input_register_device(input);
    if (ret) {
        dev_err(&pdev->dev, "failed to register input device: %d\n", ret);
        return ret;
    }

    kp->input = input;

    /* Initialize T2Input state machine */
    kp->state = MAIN;
    kp->cur_input_mode = SMALL;
    kp->last_input_key = 0;
    /* Hardware reads 0xFFFF when no keys pressed (active-low).
     * Initialize prev_keymap to match idle state so the first IRQ
     * doesn't generate 48 false key events. */
    kp->prev_keymap[0] = 0xFFFF;
    kp->prev_keymap[1] = 0xFFFF;
    kp->prev_keymap[2] = 0xFFFF;

    /* Configure debounce time (16ms at 32kHz = 512) */
    writew_relaxed(0x200, kp->base + KP_DEBOUNCE);

    /* Get IRQ number */
    res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "keypad-irq");
    if (!res) {
        dev_err(&pdev->dev, "no keypad-irq resource\n");
        return -ENODEV;
    }
    kp->irq = res->start;

    /* Configure IRQ as edge-sensitive (matches MAUI's IRQSensitivity call) */
    {
        void __iomem *cirq_sens_set = ioremap(IRQ_SENS_SET0, 4);
        if (cirq_sens_set) {
            writel_relaxed(1 << kp->irq, cirq_sens_set);
            iounmap(cirq_sens_set);
        }
    }

    /* Request IRQ */
    ret = devm_request_irq(&pdev->dev, kp->irq, mt62xx_keypad_irq,
                           IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                           "mt62xx-keypad", kp);
    if (ret) {
        dev_err(&pdev->dev, "failed to request IRQ %d: %d\n", kp->irq, ret);
        return ret;
    }

    platform_set_drvdata(pdev, kp);

    dev_info(&pdev->dev, "probe SUCCESS (IRQ %d)\n", kp->irq);

    return 0;
}

static int mt62xx_keypad_remove(struct platform_device *pdev)
{
    pr_info("mt62xx-keypad: removed\n");
    return 0;
}

static struct platform_driver mt62xx_keypad_driver = {
    .probe  = mt62xx_keypad_probe,
    .remove = mt62xx_keypad_remove,
    .driver = {
        .name = DRIVER_NAME,
    },
};

module_platform_driver(mt62xx_keypad_driver);

MODULE_AUTHOR("gvl610");
MODULE_DESCRIPTION("MT62XX Keypad Driver with T2Input State Machine");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
