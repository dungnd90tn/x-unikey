// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
#ifndef UK_IBUS_POLICY_H
#define UK_IBUS_POLICY_H

/*
 * Quy tac chon cach dua ket qua vao client duoc tach khoi engine.c de co the
 * test ma khong can khoi dong ibus-daemon.
 *
 * Thu tu la quan trong:
 *   - o bi mat khong bao gio duoc bo go can thiep;
 *   - terminal khai bao dung purpose phai theo TerminalMode;
 *   - thanh URL chi dung DIRECT khi client da xac nhan surrounding-text; nhu
 *     vay van go duoc tieng Viet de search ma khong vao composition/predict;
 *     Firefox/GTK khong map inputmode=mozAwesomebar sang purpose=URL, nen dung
 *     them heuristic FREE_FORM + UPPERCASE_SENTENCES ma no gui;
 *   - moi context IBus con lai dung PREEDIT. Capability surrounding-text chi
 *     noi rang protocol co lenh xoa, khong dam bao no tro vao van ban that cua
 *     ung dung. VS Code/xterm.js la phan vi du: textarea an bao surrounding
 *     nhung lenh xoa/commit lai duoc chuyen thanh byte gui vao PTY;
 *   - DIRECT van ton tai trong ukbridge de test va cho frontend nao thuc su
 *     so huu editable buffer, nhung khong duoc tu dong chon tu IBus caps.
 */
typedef enum {
    UK_IBUS_MODE_OFF,
    UK_IBUS_MODE_PREEDIT,
    UK_IBUS_MODE_DIRECT
} UkIBusMode;

/* Chromium co the gui URL roi tra ve FREE_FORM trong cung mot lan focus.
   Purpose tuong minh khac thi bat dau context moi va bo latch. */
static inline int
uk_ibus_policy_update_url_latch(int saw_url, int is_url, int is_free_form)
{
    if (is_url)
        return 1;
    if (!is_free_form)
        return 0;
    return saw_url;
}

/* Firefox dung inputmode rieng "mozAwesomebar". GTK khong hieu gia tri nay,
   nen IBus nhan FREE_FORM thay vi URL. Tuy nhien Firefox van gui hint
   UPPERCASE_SENTENCES (0x40), sau do co the tra hints ve 0 khi surrounding
   text duoc bat. Giu latch den het focus de khong roi lai PREEDIT giua luc go.

   Khong duoc latch chi tu SURROUNDING_TEXT: VS Code/xterm.js cung co bit do.
   Purpose tuong minh khac FREE_FORM xoa latch, de password/terminal khong ke
   thua heuristic cua context truoc. */
static inline int
uk_ibus_policy_update_sentence_latch(int saw_sentence,
                                     int is_free_form,
                                     int has_sentence_hint)
{
    if (!is_free_form)
        return 0;
    if (has_sentence_hint)
        return 1;
    return saw_sentence;
}

static inline UkIBusMode
uk_ibus_policy_choose(int is_secret,
                      int is_terminal,
                      int is_direct_entry,
                      int capabilities_known,
                      int can_surround,
                      int fallback_preedit)
{
    if (is_secret)
        return UK_IBUS_MODE_OFF;
    if (is_terminal)
        return fallback_preedit ? UK_IBUS_MODE_PREEDIT : UK_IBUS_MODE_OFF;
    if (is_direct_entry)
        return capabilities_known && can_surround
            ? UK_IBUS_MODE_DIRECT : UK_IBUS_MODE_OFF;
    (void)capabilities_known;
    (void)can_surround;
    return UK_IBUS_MODE_PREEDIT;
}

#endif
