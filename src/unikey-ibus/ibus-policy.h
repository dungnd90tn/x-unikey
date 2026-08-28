// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
#ifndef UK_IBUS_POLICY_H
#define UK_IBUS_POLICY_H

#include <string.h>

/*
 * Quy tac chon cach dua ket qua vao client duoc tach khoi engine.c de co the
 * test ma khong can khoi dong ibus-daemon.
 *
 * Thu tu la quan trong:
 *   - o bi mat khong bao gio duoc bo go can thiep;
 *   - terminal khai bao purpose=TERMINAL hoac duoc shell-integration xac
 *     nhan luon OFF;
 *   - address bar Chromium khai purpose=URL. Firefox/GTK khong map
 *     inputmode=mozAwesomebar sang URL ma gui FREE_FORM+hints=0x40. Chi hai
 *     dau van tay nay dung DIRECT sau khi client xac nhan surrounding-text;
 *   - moi context IBus con lai dung PREEDIT (predict), ke ca ALPHA, EMAIL,
 *     NAME, Gmail, form web va VS Code chat. Capability surrounding-text chi
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

/* Policy duoc chia thanh profile co thu tu uu tien, thay vi de engine.c
   chong cac special-case len nhau:

     ZSH_TERMINAL    terminal tu shell-integration bao focus ro rang;
     BROWSER_ADDRESS Chromium URL hoac Firefox awesomebar;
     GENERAL         moi editable context con lai dung PREEDIT.

   Ten ZSH_TERMINAL noi cach profile duoc kich hoat, khong co nghia IBus co
   the tu doc ten process zsh. Tren Wayland, shell-integration phai phat mot
   focus event rieng vi xterm.js va input=text co metadata IBus giong nhau. */
typedef enum {
    UK_IBUS_PROFILE_GENERAL,
    UK_IBUS_PROFILE_BROWSER_ADDRESS,
    UK_IBUS_PROFILE_ZSH_TERMINAL
} UkIBusProfile;

static inline UkIBusProfile
uk_ibus_policy_select_profile(int zsh_terminal_active,
                              int browser_address_entry)
{
    if (zsh_terminal_active)
        return UK_IBUS_PROFILE_ZSH_TERMINAL;
    if (browser_address_entry)
        return UK_IBUS_PROFILE_BROWSER_ADDRESS;
    return UK_IBUS_PROFILE_GENERAL;
}

/* Noi dung ~/.unikey/profile-event do helper viet. Parser co y khong chap
   nhan profile la/de them de mot file rac khong the ep engine sang OFF. */
static inline int
uk_ibus_policy_parse_profile_event(const char *text, int *zsh_active)
{
    if (!text || !zsh_active)
        return 0;
    if (strcmp(text, "profile=zsh\nstate=on\n") == 0) {
        *zsh_active = 1;
        return 1;
    }
    if (strcmp(text, "profile=zsh\nstate=off\n") == 0) {
        *zsh_active = 0;
        return 1;
    }
    if (strcmp(text, "profile=zsh\nstate=probe\n") == 0) {
        *zsh_active = 2;
        return 1;
    }
    return 0;
}

/* Chromium co the gui purpose=URL roi tra ve FREE_FORM trong cung mot focus.
   Giu latch rieng cho URL; ALPHA/EMAIL/NAME thuoc general/PREEDIT. */
static inline int
uk_ibus_policy_update_address_purpose_latch(int saw_address_purpose,
                                            int is_url_purpose,
                                            int is_free_form)
{
    if (is_url_purpose)
        return 1;
    if (!is_free_form)
        return 0;
    return saw_address_purpose;
}

/* IBus/VTE co the khai purpose=TERMINAL dung, sau do gui lai FREE_FORM trong
   cung mot focus (da quan sat tren Ubuntu 26.04 / IBus 1.5.34). Khong duoc de
   lan reset metadata nay bat PREEDIT tro lai va ve composition tren dong moi.
   Purpose cu the khac FREE_FORM se ket thuc latch de tranh mang nham sang mot
   editable context neu client tai su dung input context ma khong focus-out. */
static inline int
uk_ibus_policy_update_terminal_purpose_latch(int saw_terminal_purpose,
                                             int is_terminal_purpose,
                                             int is_free_form)
{
    if (is_terminal_purpose)
        return 1;
    if (!is_free_form)
        return 0;
    return saw_terminal_purpose;
}

typedef enum {
    UK_BROWSER_ADDRESS_NONE,
    UK_BROWSER_ADDRESS_CONFIRMED,
    UK_BROWSER_ADDRESS_REJECTED
} UkBrowserAddressState;

/* Firefox GTK gui awesomebar la UPPERCASE_SENTENCES (1<<6), tuc dung 0x40.
   0x41/0x45 xuat hien o chat, form va Gmail nen phai thuoc general/PREEDIT.
   Tach helper de regression test khong can link libibus. */
static inline int
uk_ibus_policy_is_firefox_address_hints(unsigned int hints)
{
    return hints == (1u << 6);
}

static inline int
uk_ibus_policy_is_general_text_hints(unsigned int hints)
{
    return hints != 0 && !uk_ibus_policy_is_firefox_address_hints(hints);
}

/* Awesomebar candidate duoc confirm ngay khi co h0x40 de phim dau tien khong
   vao PREEDIT. CONFIRMED/REJECTED duoc latch den focus-out vi Firefox co the
   lap lai set_content_type sau khi caps thay doi. */
static inline UkBrowserAddressState
uk_ibus_policy_update_browser_address(UkBrowserAddressState state,
                                      int is_free_form,
                                      int has_firefox_address_hints,
                                      int has_general_text_hints)
{
    if (!is_free_form)
        return UK_BROWSER_ADDRESS_NONE;
    if (state == UK_BROWSER_ADDRESS_CONFIRMED ||
        state == UK_BROWSER_ADDRESS_REJECTED)
        return state;
    if (has_firefox_address_hints)
        return UK_BROWSER_ADDRESS_CONFIRMED;
    if (has_general_text_hints)
        return UK_BROWSER_ADDRESS_REJECTED;
    return UK_BROWSER_ADDRESS_NONE;
}

/* Editing/BackSpace cua DIRECT candidate phai co surrounding truoc. Printable
   key den som duoc engine ghi raw vao bridge va handoff rieng; cac thao tac
   caret/range khong handoff an toan nen reject den focus-out. */
static inline UkBrowserAddressState
uk_ibus_policy_note_input(UkBrowserAddressState state, int direct_ready)
{
    return (state == UK_BROWSER_ADDRESS_CONFIRMED && !direct_ready)
        ? UK_BROWSER_ADDRESS_REJECTED : state;
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
    if (is_terminal) {
        (void)fallback_preedit;
        return UK_IBUS_MODE_OFF;
    }
    if (is_direct_entry)
        return capabilities_known && can_surround
            ? UK_IBUS_MODE_DIRECT : UK_IBUS_MODE_OFF;
    (void)capabilities_known;
    (void)can_surround;
    return UK_IBUS_MODE_PREEDIT;
}

#endif
