// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* Phan quyet dinh phim dung chung -- xem ukkeys.h */

#include "ukkeys.h"

/* Keysym X11. Trung voi GDK_KEY_* cua ca GTK3 lan GTK4. */
#define XK_BackSpace   0xff08
#define XK_Tab         0xff09
#define XK_ISO_Left_Tab 0xfe20
#define XK_Return      0xff0d
#define XK_KP_Enter    0xff8d
#define XK_ISO_Enter   0xfe34
#define XK_Escape      0xff1b
#define XK_Delete      0xffff
#define XK_KP_Delete   0xff9f
#define XK_Home        0xff50
#define XK_Left        0xff51
#define XK_Up          0xff52
#define XK_Right       0xff53
#define XK_Down        0xff54
#define XK_Page_Up     0xff55
#define XK_Page_Down   0xff56
#define XK_End         0xff57
#define XK_Insert      0xff63
#define XK_Shift_L     0xffe1
#define XK_Shift_R     0xffe2
#define XK_Control_L   0xffe3
#define XK_Control_R   0xffe4
#define XK_Caps_Lock   0xffe5
#define XK_Meta_L      0xffe7
#define XK_Meta_R      0xffe8
#define XK_Alt_L       0xffe9
#define XK_Alt_R       0xffea
#define XK_Super_L     0xffeb
#define XK_Super_R     0xffec
#define XK_Num_Lock    0xff7f
#define XK_ISO_Level3_Shift 0xfe03
#define XK_KP_0        0xffb0
#define XK_KP_9        0xffb9
#define XK_F5          0xffc2
#define XK_F6          0xffc3
#define XK_F7          0xffc4
#define XK_F8          0xffc5
#define XK_F9          0xffc6
#define XK_Z           0x005a
#define XK_z           0x007a

/* Ctrl-Shift dang cho nha phim. Trang thai nay theo ban phim, khong theo o
   nhap, nen de o pham vi tap tin la dung. */
static int PendingSwitch = 0;

static int is_shift(unsigned int k)   { return k == XK_Shift_L   || k == XK_Shift_R; }
static int is_control(unsigned int k) { return k == XK_Control_L || k == XK_Control_R; }

UkKeyAction uk_keys_shortcut(const UkKeyInfo *k)
{
    /* Ctrl-Shift: chi doi trang thai khi NHA, va chi khi khong co phim nao
       khac bam xen vao giua -- giong ukxim (xim.c:isSwitchKey), de Ctrl-Shift-C
       hay Ctrl-Shift-V khong vo tinh bat/tat bo go. */
    if (is_shift(k->keysym) || is_control(k->keysym)) {
        if (k->is_press) {
            if ((is_shift(k->keysym) && k->ctrl) || (is_control(k->keysym) && k->shift))
                PendingSwitch = 1;
        } else if (PendingSwitch) {
            PendingSwitch = 0;
            return UK_ACTION_TOGGLE;
        }
        return UK_ACTION_NONE;
    }

    PendingSwitch = 0;

    if (!k->is_press || !k->ctrl || !k->shift)
        return UK_ACTION_NONE;

    switch (k->keysym) {
    case XK_F9:     return UK_ACTION_TOGGLE;
    case XK_F5:     return UK_ACTION_TELEX;
    case XK_F6:     return UK_ACTION_VNI;
    case XK_F7:     return UK_ACTION_VIQR;
    case XK_F8:     return UK_ACTION_USER_IM;
    case XK_Escape: return UK_ACTION_RESTORE;
    case XK_Z:
    case XK_z:      return UK_ACTION_SINGLE_MODE;
    default:        return UK_ACTION_NONE;
    }
}

int uk_keys_is_editing(unsigned int keysym)
{
    switch (keysym) {
    case XK_Return: case XK_KP_Enter: case XK_ISO_Enter:
    case XK_Tab:    case XK_ISO_Left_Tab:
    case XK_Escape: case XK_Delete:   case XK_KP_Delete:
    case XK_Left:   case XK_Right:    case XK_Up:      case XK_Down:
    case XK_Home:   case XK_End:      case XK_Page_Up: case XK_Page_Down:
    case XK_Insert:
        return 1;
    default:
        return 0;
    }
}

int uk_keys_is_modifier(unsigned int keysym)
{
    switch (keysym) {
    case XK_Shift_L:   case XK_Shift_R:
    case XK_Control_L: case XK_Control_R:
    case XK_Alt_L:     case XK_Alt_R:
    case XK_Super_L:   case XK_Super_R:
    case XK_Meta_L:    case XK_Meta_R:
    case XK_Caps_Lock: case XK_Num_Lock:
    case XK_ISO_Level3_Shift:
        return 1;
    default:
        return 0;
    }
}

int uk_keys_is_keypad_digit(unsigned int keysym)
{
    return keysym >= XK_KP_0 && keysym <= XK_KP_9;
}
