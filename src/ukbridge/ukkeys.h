// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* Phan quyet dinh phim, dung chung cho moi front-end.
 *
 * GTK3, GTK4 va Qt co API su kien khac han nhau, nhung deu quy duoc ve
 * (keysym, cac co modifier, nhan hay tha). Tach ra day de bang phim tat chi
 * ton tai o MOT cho -- them phim tat moi khong phai sua 3 module.
 *
 * Ma keysym dung o day la ma X11/GDK (GDK_KEY_* trong GTK chinh la keysym X11,
 * Qt thi front-end phai tu doi sang truoc khi goi).
 */
#ifndef __UK_KEYS_H
#define __UK_KEYS_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum {
    UK_ACTION_NONE = 0,   /* khong phai phim tat */
    UK_ACTION_TOGGLE,     /* bat/tat tieng Viet */
    UK_ACTION_TELEX,
    UK_ACTION_VNI,
    UK_ACTION_VIQR,
    UK_ACTION_USER_IM,
    UK_ACTION_RESTORE,    /* tra lai phim da go */
    UK_ACTION_SINGLE_MODE /* tat spell-check cho tu ke tiep */
} UkKeyAction;

typedef struct {
    unsigned int keysym;
    int ctrl, shift, alt, super;
    int is_press;         /* 1 = nhan, 0 = tha */
} UkKeyInfo;

/* Nhan dien phim tat. Ctrl-Shift (nhan roi tha, khong co phim khac xen giua)
   la mot chuoi trang thai nen ham nay co bo nho trong -- phai goi cho MOI su
   kien phim, ke ca phim tha, dung loc bot. */
UkKeyAction uk_keys_shortcut(const UkKeyInfo *k);

/* Phim dieu huong/sua van ban: phai chot preedit roi tra lai cho ung dung. */
int uk_keys_is_editing(unsigned int keysym);

/* Phim modifier don thuan. */
int uk_keys_is_modifier(unsigned int keysym);

/* Phim so tren ban phim so (khong duoc hieu la dau thanh kieu VNI). */
int uk_keys_is_keypad_digit(unsigned int keysym);

#if defined(__cplusplus)
}
#endif

#endif
