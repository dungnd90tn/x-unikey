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

static inline UkIBusMode
uk_ibus_policy_choose(int is_secret,
                      int is_terminal,
                      int capabilities_known,
                      int can_surround,
                      int fallback_preedit)
{
    if (is_secret)
        return UK_IBUS_MODE_OFF;
    if (is_terminal)
        return fallback_preedit ? UK_IBUS_MODE_PREEDIT : UK_IBUS_MODE_OFF;
    (void)capabilities_known;
    (void)can_surround;
    return UK_IBUS_MODE_PREEDIT;
}

#endif
