#ifndef PS2KEYBOARD_H
#define PS2KEYBOARD_H 1

void InitKeyboardDriver();

void KeyboardDriverMain(uint8_t sc);

int kbd_getc();
int kbd_gets(char *out, int maxlen);

#endif /* PS2KEYBOARD_H */