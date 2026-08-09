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
 *   - input ALPHA/URL/EMAIL/NAME chi dung DIRECT khi client da xac nhan
 *     surrounding-text; nhu vay go tieng Viet ma khong composition/predict;
 *     Firefox/GTK khong map inputmode=mozAwesomebar sang purpose=URL. Cac o
 *     nhap web/Electron co spellcheck/autocapitalize gui 0x40 hoac 0x41;
 *     chon DIRECT ngay khi co surrounding de khong lo composition/predict.
 *     FREE_FORM+hints=NONE giu PREEDIT vi IBus khong phan biet duoc
 *     xterm.js voi input text;
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

/* Client co the gui purpose editable tuong minh roi tra ve FREE_FORM trong
   cung mot focus. Giu latch cho ALPHA/URL/EMAIL/NAME; purpose khac xoa no. */
static inline int
uk_ibus_policy_update_direct_purpose_latch(int saw_direct_purpose,
                                           int is_direct_purpose,
                                           int is_free_form)
{
    if (is_direct_purpose)
        return 1;
    if (!is_free_form)
        return 0;
    return saw_direct_purpose;
}

typedef enum {
    UK_FIREFOX_ENTRY_NONE,
    UK_FIREFOX_ENTRY_CONFIRMED,
    UK_FIREFOX_ENTRY_REJECTED
} UkFirefoxEntryState;

/* Gia tri ABI cua IBusInputHints: SPELLCHECK=1<<0,
   UPPERCASE_SENTENCES=1<<6. Tach helper de regression test khong can link
   libibus. Chi chap nhan dung 0x40/0x41, khong gom tuy tien cac hint khac. */
static inline int
uk_ibus_policy_is_direct_text_hints(unsigned int hints)
{
    return hints == (1u << 6) || hints == ((1u << 6) | (1u << 0));
}

/* Firefox awesomebar va input/chat web/Electron deu co the gui 0x40/0x41.
   Neu doi h0 moi confirm, phim dau tien van chay PREEDIT va tao predict; neu
   chuyen mode muon thi control co the thay range giua mot tu. Vi vay hint
   text-entry confirm candidate ngay. CONFIRMED/REJECTED duoc latch den
   focus-out vi client co the lap lai set_content_type sau khi caps thay doi. */
static inline UkFirefoxEntryState
uk_ibus_policy_update_firefox_entry(UkFirefoxEntryState state,
                                    int is_free_form,
                                    int has_direct_text_hints)
{
    if (!is_free_form)
        return UK_FIREFOX_ENTRY_NONE;
    if (state == UK_FIREFOX_ENTRY_CONFIRMED ||
        state == UK_FIREFOX_ENTRY_REJECTED)
        return state;
    if (has_direct_text_hints)
        return UK_FIREFOX_ENTRY_CONFIRMED;
    return UK_FIREFOX_ENTRY_NONE;
}

/* Editing/BackSpace cua DIRECT candidate phai co surrounding truoc. Printable
   key den som duoc engine ghi raw vao bridge va handoff rieng; cac thao tac
   caret/range khong handoff an toan nen reject den focus-out. */
static inline UkFirefoxEntryState
uk_ibus_policy_note_input(UkFirefoxEntryState state, int direct_ready)
{
    return (state == UK_FIREFOX_ENTRY_CONFIRMED && !direct_ready)
        ? UK_FIREFOX_ENTRY_REJECTED : state;
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
