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
 *     Firefox/GTK khong map inputmode=mozAwesomebar sang purpose=URL. Khong
 *     duoc dung rieng UPPERCASE_SENTENCES de nhan no: input/textarea web cung
 *     co hint nay. Chi chap nhan chuoi rieng da quan sat o awesomebar:
 *     sentence-only -> NONE trong cung mot focus;
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

typedef enum {
    UK_FIREFOX_ENTRY_NONE,
    UK_FIREFOX_ENTRY_SENTENCE_ONLY,
    UK_FIREFOX_ENTRY_CONFIRMED,
    UK_FIREFOX_ENTRY_REJECTED
} UkFirefoxEntryState;

/* Firefox dung inputmode rieng "mozAwesomebar". GTK khong hieu gia tri nay,
   nen IBus nhan FREE_FORM thay vi URL. Trace Firefox 153 Wayland co mot
   handshake on dinh: hints=UPPERCASE_SENTENCES (0x40), sau do hints=NONE.

   UPPERCASE_SENTENCES mot minh KHONG phai dau van tay: input/textarea web
   (nhat la autocapitalize/spellcheck) cung gui 0x40 hoac 0x41. Chi confirm sau
   canh chuyen sentence-only -> none. Trong luc moi thay nua dau, giu PREEDIT;
   khong nhay sang OFF/DIRECT giua am tiet. Hint khac va purpose tuong minh se
   xoa candidate. CONFIRMED/REJECTED duoc latch den focus-out vi Firefox co
   the lap lai set_content_type sau khi surrounding thay doi. */
static inline UkFirefoxEntryState
uk_ibus_policy_update_firefox_entry(UkFirefoxEntryState state,
                                    int is_free_form,
                                    int is_sentence_only,
                                    int has_no_hints)
{
    if (!is_free_form)
        return UK_FIREFOX_ENTRY_NONE;
    if (state == UK_FIREFOX_ENTRY_CONFIRMED ||
        state == UK_FIREFOX_ENTRY_REJECTED)
        return state;
    if (is_sentence_only)
        return UK_FIREFOX_ENTRY_SENTENCE_ONLY;
    if (state == UK_FIREFOX_ENTRY_SENTENCE_ONLY && has_no_hints)
        return UK_FIREFOX_ENTRY_CONFIRMED;
    if (state == UK_FIREFOX_ENTRY_SENTENCE_ONLY)
        return UK_FIREFOX_ENTRY_REJECTED;
    return UK_FIREFOX_ENTRY_NONE;
}

/* Handshake awesomebar phai hoan tat TRUOC phim nhap dau tien. Trace thuc te
   cua o chat Codex/Firefox cho thay no gui h0x40, nguoi dung go nhieu tu, roi
   moi gui h0. Neu confirm muon, bridge doi PREEDIT -> DIRECT giua context va
   co the replace theo van ban da commit. Latch REJECTED den focus-out. */
static inline UkFirefoxEntryState
uk_ibus_policy_note_input(UkFirefoxEntryState state, int direct_ready)
{
    return (state == UK_FIREFOX_ENTRY_SENTENCE_ONLY ||
            (state == UK_FIREFOX_ENTRY_CONFIRMED && !direct_ready))
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
