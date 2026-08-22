// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* Test cho ukbridge: go mot chuoi phim, kiem tra van ban thu duoc.
 *
 * Chay khong can X/Wayland/toolkit nao -- day la toan bo logic go tieng Viet
 * cua cac module GTK3/GTK4/Qt, nen test o day phu duoc phan rui ro nhat.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "keycons.h"
#include "optparse.h"
#include "ukbridge.h"
#include "ukopt.h"
#include "../unikey-ibus/ibus-policy.h"

/*----------------------------------------------------------------
  Front-end gia: gom commit + preedit lai thanh van ban cuoi cung,
  dung nhu mot o nhap that se hien thi.
 ----------------------------------------------------------------*/
typedef struct {
    char committed[512];
    char preedit[256];
    char events[64];
} FakeEntry;

static void fake_commit(void *user, const char *utf8)
{
    FakeEntry *e = (FakeEntry *)user;
    strncat(e->committed, utf8, sizeof(e->committed) - strlen(e->committed) - 1);
    strncat(e->events, "C", sizeof(e->events) - strlen(e->events) - 1);
}

static void fake_preedit(void *user, const char *utf8)
{
    FakeEntry *e = (FakeEntry *)user;
    snprintf(e->preedit, sizeof(e->preedit), "%s", utf8 ? utf8 : "");
    strncat(e->events, (utf8 && *utf8) ? "P" : "E",
            sizeof(e->events) - strlen(e->events) - 1);
}

static const UkBridgeVTable FakeVTable = {
    fake_commit, fake_preedit, NULL, NULL
};

/* Van ban ma nguoi dung nhin thay = da commit + phan preedit dang go */
static const char *visible(FakeEntry *e, char *out, size_t n)
{
    snprintf(out, n, "%s%s", e->committed, e->preedit);
    return out;
}

/*----------------------------------------------------------------*/
static int Failures = 0;

static void type_ascii(UkBridge *b, const char *keys)
{
    const unsigned char *p = (const unsigned char *)keys;
    for (; *p; p++) {
        int shift = (*p >= 'A' && *p <= 'Z');
        uk_bridge_key(b, *p, shift, 0);
    }
}

static void check(int im, const char *im_name, const char *keys, const char *expect)
{
    FakeEntry e;
    char got[768];
    UkBridge *b;

    memset(&e, 0, sizeof(e));
    b = uk_bridge_new(&FakeVTable, &e);
    uk_bridge_set_enabled(b, 1);
    uk_bridge_set_input_method(b, im);

    type_ascii(b, keys);
    visible(&e, got, sizeof(got));

    if (strcmp(got, expect) == 0) {
        printf("  ok   %-6s %-18s -> %s\n", im_name, keys, got);
    } else {
        printf("  FAIL %-6s %-18s -> \"%s\" (mong doi \"%s\")\n",
               im_name, keys, got, expect);
        Failures++;
    }
    uk_bridge_free(b);
}

/* Go chuoi phim roi bam BackSpace n lan */
static void check_backspace(const char *keys, int n, const char *expect)
{
    FakeEntry e;
    char got[768];
    UkBridge *b;
    int i;

    memset(&e, 0, sizeof(e));
    b = uk_bridge_new(&FakeVTable, &e);
    uk_bridge_set_enabled(b, 1);
    uk_bridge_set_input_method(b, UkTelex);

    type_ascii(b, keys);
    for (i = 0; i < n; i++)
        uk_bridge_backspace(b);
    visible(&e, got, sizeof(got));

    if (strcmp(got, expect) == 0) {
        printf("  ok   telex  %-12s +%dBS -> %s\n", keys, n, got);
    } else {
        printf("  FAIL telex  %-12s +%dBS -> \"%s\" (mong doi \"%s\")\n",
               keys, n, got, expect);
        Failures++;
    }
    uk_bridge_free(b);
}

/*----------------------------------------------------------------
  Che do DIRECT: khong co preedit. Front-end gia o day mo phong mot o
  nhap that -- engine xoa bang erase_before_cursor roi commit chu moi,
  con ky tu nao bridge tra ve PASS thi chinh ung dung tu chen.
 ----------------------------------------------------------------*/
typedef struct {
    char text[512];
} FakeField;

static void field_chop_utf8(FakeField *f, int nchars)
{
    int i = (int)strlen(f->text);
    while (nchars > 0 && i > 0) {
        i--;
        while (i > 0 && ((unsigned char)f->text[i] & 0xC0) == 0x80)
            i--;
        nchars--;
    }
    f->text[i] = '\0';
}

static void direct_commit(void *user, const char *utf8)
{
    FakeField *f = (FakeField *)user;
    strncat(f->text, utf8, sizeof(f->text) - strlen(f->text) - 1);
}

static void direct_preedit(void *user, const char *utf8)
{
    (void)user; (void)utf8;   /* che do DIRECT khong dung preedit */
}

/* Mo phong client THUC SU chap nhan delete-surrounding-text. VTE khong lam
   dieu nay, nen policy IBus ben duoi phai giu VTE khoi che do DIRECT. */
static void direct_erase(void *user, int nchars)
{
    field_chop_utf8((FakeField *)user, nchars);
}

static const UkBridgeVTable DirectVTable = {
    direct_commit,
    direct_preedit,
    direct_erase,
    NULL
};

static void check_direct(int im, const char *im_name,
                         const char *keys, const char *expect)
{
    FakeField f;
    UkBridge *b;
    const unsigned char *p;

    memset(&f, 0, sizeof(f));
    b = uk_bridge_new(&DirectVTable, &f);

    /* DIRECT khong con la mac dinh -- phai bat tuong minh. */
    uk_bridge_set_direct_mode(b, 1);
    uk_bridge_set_enabled(b, 1);
    uk_bridge_set_input_method(b, im);

    for (p = (const unsigned char *)keys; *p; p++) {
        int shift = (*p >= 'A' && *p <= 'Z');
        if (uk_bridge_key(b, *p, shift, 0) == UK_BRIDGE_PASS) {
            /* Ung dung tu chen ky tu, dung nhu o nhap that lam. */
            char one[2] = { (char)*p, '\0' };
            strncat(f.text, one, sizeof(f.text) - strlen(f.text) - 1);
        }
    }

    if (strcmp(f.text, expect) == 0)
        printf("  ok   direct %-6s %-14s -> %s\n", im_name, keys, f.text);
    else {
        printf("  FAIL direct %-6s %-14s -> \"%s\" (mong doi \"%s\")\n",
               im_name, keys, f.text, expect);
        Failures++;
    }
    uk_bridge_free(b);
}

/* User Tab/go nhanh: raw key dau tien vao app truoc caps surrounding. Bridge
   phai nho no, handoff sang DIRECT khong reset state, roi bien doi ca am tiet. */
static void check_pending_direct_handoff(void)
{
    FakeField f;
    UkBridge *b;

    memset(&f, 0, sizeof(f));
    strcpy(f.text, "d"); /* app da tu chen raw key vi engine tra PASS */
    b = uk_bridge_new(&DirectVTable, &f);
    uk_bridge_set_enabled(b, 1);
    uk_bridge_set_input_method(b, UkTelex);
    uk_bridge_note_passthrough_key(b, 'd', 0, 0);
    uk_bridge_set_direct_mode(b, 1); /* caps surrounding den */
    uk_bridge_key(b, 'd', 0, 0);
    uk_bridge_key(b, 'a', 0, 0);
    uk_bridge_key(b, 'j', 0, 0);

    if (strcmp(f.text, "đạ") == 0)
        printf("  ok   pending DIRECT handoff: raw d + daj -> %s\n", f.text);
    else {
        printf("  FAIL pending DIRECT handoff: got=%s expect=đạ\n", f.text);
        Failures++;
    }
    uk_bridge_free(b);
}

/*----------------------------------------------------------------
  Doi che do giua chung.

  IBus goi set_capabilities moi khi doi o nhap, nen bridge co the phai
  nhay tu DIRECT (web) sang PREEDIT (terminal) ngay giua mot am tiet
  dang go. Phan dang go khong duoc mat, cung khong duoc nhan doi.
 ----------------------------------------------------------------*/
static void check_mode_switch(void)
{
    FakeField f;          /* dong vai o nhap that: giu ca chu da commit */
    UkBridge *b;
    UkBridgeVTable vt;

    memset(&f, 0, sizeof(f));

    /* vtable co ca 3 callback: commit ghi vao f.text, preedit thi bo qua
       (o day chi quan tam van ban that su di vao o nhap). */
    vt = DirectVTable;
    b = uk_bridge_new(&vt, &f);
    uk_bridge_set_enabled(b, 1);
    uk_bridge_set_input_method(b, UkTelex);
    uk_bridge_set_direct_mode(b, 1);

    /* Go do "tie" trong che do DIRECT -> da commit thang vao o nhap */
    uk_bridge_key(b, 't', 0, 0);
    uk_bridge_key(b, 'i', 0, 0);
    uk_bridge_key(b, 'e', 0, 0);

    /* Chuyen sang PREEDIT (nhu khi focus sang terminal) */
    uk_bridge_set_direct_mode(b, 0);

    if (strcmp(f.text, "tie") == 0)
        printf("  ok   doi che do giua chung: giu nguyen \"%s\"\n", f.text);
    else {
        printf("  FAIL doi che do giua chung: \"%s\" (mong doi \"tie\")\n", f.text);
        Failures++;
    }

    /* Va nguoc lai: PREEDIT -> DIRECT phai chot phan preedit, khong vut di */
    uk_bridge_reset(b);
    memset(&f, 0, sizeof(f));
    uk_bridge_set_direct_mode(b, 0);
    uk_bridge_key(b, 'v', 0, 0);
    uk_bridge_key(b, 'i', 0, 0);
    uk_bridge_set_direct_mode(b, 1);

    if (strcmp(f.text, "vi") == 0)
        printf("  ok   PREEDIT->DIRECT: chot preedit thanh \"%s\"\n", f.text);
    else {
        printf("  FAIL PREEDIT->DIRECT: \"%s\" (mong doi \"vi\")\n", f.text);
        Failures++;
    }

    uk_bridge_free(b);
}

/*----------------------------------------------------------------
  Policy IBus phai uu tien purpose=TERMINAL hon capability SURROUNDING gia
  ban dau cua IBus GTK. Test nay la regression truc tiep cho loi VTE tao ra
  "dduoc..."/"dđưoơcợc" vi bo qua lenh xoa.
 ----------------------------------------------------------------*/
static void check_ibus_policy_case(const char *name,
                                   int secret, int terminal, int direct_entry,
                                   int caps_known, int can_surround,
                                   int fallback_preedit,
                                   UkIBusMode expect)
{
    UkIBusMode got = uk_ibus_policy_choose(secret, terminal, direct_entry,
                                           caps_known, can_surround,
                                           fallback_preedit);

    if (got == expect)
        printf("  ok   policy %-28s -> %d\n", name, got);
    else {
        printf("  FAIL policy %-28s -> %d (mong doi %d)\n",
               name, got, expect);
        Failures++;
    }
}

static void check_ibus_policy(void)
{
    int terminal_purpose_latch;
    int direct_purpose_latch;
    UkFirefoxEntryState firefox_entry;
    UkFirefoxEntryState trace_entry;
    UkFirefoxEntryState chat_entry;
    int trace_ok;
    int zsh_active = -1;

    if (uk_ibus_policy_select_profile(0, 0) == UK_IBUS_PROFILE_GENERAL &&
        uk_ibus_policy_select_profile(0, 1) == UK_IBUS_PROFILE_FIREFOX_WEB &&
        uk_ibus_policy_select_profile(1, 1) ==
            UK_IBUS_PROFILE_ZSH_TERMINAL) {
        printf("  ok   policy profile priority: zsh > firefox > general\n");
    } else {
        printf("  FAIL policy profile priority\n");
        Failures++;
    }

    if (uk_ibus_policy_parse_profile_event(
            "profile=zsh\nstate=on\n", &zsh_active) && zsh_active == 1 &&
        uk_ibus_policy_parse_profile_event(
            "profile=zsh\nstate=off\n", &zsh_active) && zsh_active == 0 &&
        uk_ibus_policy_parse_profile_event(
            "profile=zsh\nstate=probe\n", &zsh_active) && zsh_active == 2 &&
        !uk_ibus_policy_parse_profile_event(
            "profile=unknown\nstate=on\n", &zsh_active)) {
        printf("  ok   policy zsh profile event parser\n");
    } else {
        printf("  FAIL policy zsh profile event parser\n");
        Failures++;
    }

    /* VTE: purpose terminal thang caps 0x29 gia, nhung van ton trong option. */
    check_ibus_policy_case("terminal preedit + caps gia",
                           0, 1, 0, 1, 1, 1, UK_IBUS_MODE_PREEDIT);
    check_ibus_policy_case("terminal off + caps gia",
                           0, 1, 0, 1, 1, 0, UK_IBUS_MODE_OFF);
    check_ibus_policy_case("terminal off + no surrounding",
                           0, 1, 0, 1, 0, 0, UK_IBUS_MODE_OFF);

    /* Ubuntu 26.04 / IBus 1.5.34: VTE co the gui TERMINAL roi FREE_FORM
       trong cung focus. Terminal phai giu OFF; purpose cu the khac se xoa
       latch neu client tai su dung context. */
    terminal_purpose_latch =
        uk_ibus_policy_update_terminal_purpose_latch(0, 1, 0);
    terminal_purpose_latch =
        uk_ibus_policy_update_terminal_purpose_latch(
            terminal_purpose_latch, 0, 1);
    if (terminal_purpose_latch &&
        uk_ibus_policy_choose(0, terminal_purpose_latch, 0, 1, 1, 0) ==
            UK_IBUS_MODE_OFF &&
        !uk_ibus_policy_update_terminal_purpose_latch(
            terminal_purpose_latch, 0, 0)) {
        printf("  ok   policy terminal latch: TERMINAL -> FREE_FORM -> OFF\n");
    } else {
        printf("  FAIL policy terminal purpose latch\n");
        Failures++;
    }

    /* Chromium address bar khai purpose=URL (5): DIRECT de van go duoc tieng
       Viet khi search, nhung khong vao PREEDIT/predict. */
    check_ibus_policy_case("URL bar + surrounding",
                           0, 0, 1, 1, 1, 1, UK_IBUS_MODE_DIRECT);
    check_ibus_policy_case("URL latched, purpose ve p0",
                           0, 0, 1, 1, 1, 0, UK_IBUS_MODE_DIRECT);
    check_ibus_policy_case("URL bar chua co caps",
                           0, 0, 1, 0, 0, 1, UK_IBUS_MODE_OFF);
    check_ibus_policy_case("URL bar no surrounding",
                           0, 0, 1, 1, 0, 1, UK_IBUS_MODE_OFF);

    /* Firefox awesomebar va chat/input web/Electron gui h0x40/0x41. Confirm
       ngay de khong cho phim dau tien lot vao PREEDIT/predict. */
    firefox_entry = uk_ibus_policy_update_firefox_entry(
        UK_FIREFOX_ENTRY_NONE, 1,
        uk_ibus_policy_is_direct_text_hints(0x40));
    check_ibus_policy_case("text entry + surrounding",
                           0, 0, firefox_entry == UK_FIREFOX_ENTRY_CONFIRMED,
                           1, 1, 1,
                           UK_IBUS_MODE_DIRECT);
    check_ibus_policy_case("text entry chua surrounding",
                           0, 0, firefox_entry == UK_FIREFOX_ENTRY_CONFIRMED,
                           1, 0, 1,
                           UK_IBUS_MODE_OFF);
    check_ibus_policy_case("terminal uu tien Firefox hint",
                           0, 1, firefox_entry == UK_FIREFOX_ENTRY_CONFIRMED,
                           1, 1, 0,
                           UK_IBUS_MODE_OFF);
    check_ibus_policy_case("terminal preedit uu tien hint",
                           0, 1, firefox_entry == UK_FIREFOX_ENTRY_CONFIRMED,
                           1, 1, 1,
                           UK_IBUS_MODE_PREEDIT);
    check_ibus_policy_case("password uu tien Firefox hint",
                           1, 0, firefox_entry == UK_FIREFOX_ENTRY_CONFIRMED,
                           1, 1, 1,
                           UK_IBUS_MODE_OFF);

    /* Replay Firefox Wayland: h0x40 co the den truoc caps surrounding. Candidate
       vao OFF tam thoi, roi DIRECT ngay khi caps0x29 den; h0 sau do khong duoc
       xoa latch. */
    trace_entry = UK_FIREFOX_ENTRY_NONE;
    trace_ok =
        uk_ibus_policy_choose(0, 0, 0, 0, 0, 1) ==
            UK_IBUS_MODE_PREEDIT &&
        uk_ibus_policy_choose(0, 0, 0, 1, 0, 1) ==
            UK_IBUS_MODE_PREEDIT;
    trace_entry = uk_ibus_policy_update_firefox_entry(
        trace_entry, 1, uk_ibus_policy_is_direct_text_hints(0x40));
    trace_ok = trace_ok &&
        trace_entry == UK_FIREFOX_ENTRY_CONFIRMED &&
        uk_ibus_policy_choose(0, 0, 1, 1, 0, 1) ==
            UK_IBUS_MODE_OFF &&
        uk_ibus_policy_choose(0, 0, 1, 1, 1, 1) ==
            UK_IBUS_MODE_DIRECT;
    trace_entry = uk_ibus_policy_update_firefox_entry(
        trace_entry, 1, uk_ibus_policy_is_direct_text_hints(0));
    trace_ok = trace_ok &&
        trace_entry == UK_FIREFOX_ENTRY_CONFIRMED &&
        uk_ibus_policy_choose(0, 0, 1, 1, 1, 1) ==
            UK_IBUS_MODE_DIRECT;
    trace_entry = uk_ibus_policy_update_firefox_entry(
        trace_entry, 0, uk_ibus_policy_is_direct_text_hints(0));
    trace_entry = uk_ibus_policy_update_firefox_entry(
        trace_entry, 1, uk_ibus_policy_is_direct_text_hints(0));
    trace_ok = trace_ok && trace_entry == UK_FIREFOX_ENTRY_NONE &&
        uk_ibus_policy_choose(0, 0, 0, 1, 1, 1) ==
            UK_IBUS_MODE_PREEDIT;
    if (trace_ok)
        printf("  ok   policy text-entry trace: h0x40 -> caps -> DIRECT\n");
    else {
        printf("  FAIL policy text-entry trace\n");
        Failures++;
    }

    /* VS Code add-on chat gui h0x41 + surrounding: no la editable buffer that
       va phai DIRECT. xterm.js h0 van duoc test PREEDIT o duoi. */
    chat_entry = uk_ibus_policy_update_firefox_entry(
        UK_FIREFOX_ENTRY_NONE, 1,
        uk_ibus_policy_is_direct_text_hints(0x41));
    if (chat_entry == UK_FIREFOX_ENTRY_CONFIRMED &&
        uk_ibus_policy_choose(0, 0, 1, 1, 1, 1) ==
            UK_IBUS_MODE_DIRECT) {
        printf("  ok   policy VS Code chat h0x41 -> DIRECT\n");
    } else {
        printf("  FAIL policy VS Code chat h0x41\n");
        Failures++;
    }

    /* Editing/BackSpace truoc caps khong the handoff an toan vi caret/range
       chua xac dinh: reject DIRECT den focus-out. Printable key duoc bridge
       handoff rieng va test o check_pending_direct_handoff(). */
    chat_entry = uk_ibus_policy_update_firefox_entry(
        UK_FIREFOX_ENTRY_NONE, 1,
        uk_ibus_policy_is_direct_text_hints(0x41));
    chat_entry = uk_ibus_policy_note_input(chat_entry, 0);
    if (chat_entry == UK_FIREFOX_ENTRY_REJECTED &&
        uk_ibus_policy_note_input(UK_FIREFOX_ENTRY_CONFIRMED, 1) ==
            UK_FIREFOX_ENTRY_CONFIRMED) {
        printf("  ok   policy text-entry: editing truoc caps -> PREEDIT\n");
    } else {
        printf("  FAIL policy text-entry editing-before-caps\n");
        Failures++;
    }

    /* Luc IBus chua tra capability, khong duoc mao hiem commit DIRECT. */
    check_ibus_policy_case("capability chua biet",
                           0, 0, 0, 0, 0, 0, UK_IBUS_MODE_PREEDIT);

    /* FREE_FORM+h0+surrounding khong du phan biet input=text voi xterm.js,
       nen generic text entry phai giu PREEDIT. */
    check_ibus_policy_case("entry chua phan loai",
                           0, 0, 0, 1, 1, 1, UK_IBUS_MODE_PREEDIT);

    /* Client khong khai purpose terminal cung phai dung PREEDIT an toan. */
    check_ibus_policy_case("legacy fallback preedit",
                           0, 0, 0, 1, 0, 1, UK_IBUS_MODE_PREEDIT);
    check_ibus_policy_case("legacy fallback off",
                           0, 0, 0, 1, 0, 0, UK_IBUS_MODE_PREEDIT);

    /* Bao mat luon co do uu tien cao nhat. */
    check_ibus_policy_case("password terminal",
                           1, 1, 0, 1, 1, 1, UK_IBUS_MODE_OFF);

    direct_purpose_latch = uk_ibus_policy_update_direct_purpose_latch(
        0, 1, 0);
    direct_purpose_latch = uk_ibus_policy_update_direct_purpose_latch(
        direct_purpose_latch, 0, 1);
    if (direct_purpose_latch == 1 &&
        uk_ibus_policy_update_direct_purpose_latch(
            direct_purpose_latch, 0, 0) == 0) {
        printf("  ok   policy purpose latch: ALPHA/URL/EMAIL/NAME -> DIRECT\n");
    } else {
        printf("  FAIL policy direct-purpose latch\n");
        Failures++;
    }

    if (firefox_entry == UK_FIREFOX_ENTRY_CONFIRMED &&
        uk_ibus_policy_is_direct_text_hints(0x40) &&
        uk_ibus_policy_is_direct_text_hints(0x41) &&
        !uk_ibus_policy_is_direct_text_hints(0) &&
        !uk_ibus_policy_is_direct_text_hints(0x42) &&
        uk_ibus_policy_update_firefox_entry(
            UK_FIREFOX_ENTRY_NONE, 1, 0) == UK_FIREFOX_ENTRY_NONE &&
        uk_ibus_policy_update_firefox_entry(
            UK_FIREFOX_ENTRY_NONE, 0, 1) == UK_FIREFOX_ENTRY_NONE &&
        uk_ibus_policy_update_firefox_entry(
            UK_FIREFOX_ENTRY_CONFIRMED, 1, 0) ==
                UK_FIREFOX_ENTRY_CONFIRMED &&
        uk_ibus_policy_update_firefox_entry(
            UK_FIREFOX_ENTRY_REJECTED, 1, 1) ==
                UK_FIREFOX_ENTRY_REJECTED) {
        printf("  ok   policy direct hints: chi 0x40/0x41, latch focus\n");
    } else {
        printf("  FAIL policy direct hints/latch\n");
        Failures++;
    }
}

/* IBus Reset co the den khi click/doi caret/huy composition. No phai xoa
   partial word chu khong commit ngoai y muon. */
static void check_reset_cancels(void)
{
    FakeEntry e;
    UkBridge *b;

    memset(&e, 0, sizeof(e));
    b = uk_bridge_new(&FakeVTable, &e);
    uk_bridge_set_enabled(b, 1);
    uk_bridge_set_input_method(b, UkTelex);
    type_ascii(b, "tie");
    uk_bridge_reset(b);

    if (strcmp(uk_bridge_preedit(b), "") == 0 &&
        strcmp(e.preedit, "") == 0 && strcmp(e.committed, "") == 0) {
        printf("  ok   IBus reset huy partial word, khong commit\n");
    } else {
        printf("  FAIL reset: bridge=%s frontend=%s commit=%s\n",
               uk_bridge_preedit(b), e.preedit, e.committed);
        Failures++;
    }
    uk_bridge_free(b);
}

static void fake_frontend_commit_preedit(void *user, const char *utf8)
{
    FakeEntry *e = (FakeEntry *)user;

    /* Mo phong CommitText -> HidePreeditText cua IBus. Commit phai nhin thay
       composition hien tai; bridge khong phat empty-preedit quanh callback. */
    strncat(e->committed, utf8,
            sizeof(e->committed) - strlen(e->committed) - 1);
    strncat(e->events, "C", sizeof(e->events) - strlen(e->events) - 1);
    e->preedit[0] = '\0';
    strncat(e->events, "H", sizeof(e->events) - strlen(e->events) - 1);
}

/* IBus phai commit vao composition range dang ton tai, roi hide no. Khong
   duoc chen generic empty-preedit (E) vi client co the finalize hai lan. */
static void check_ibus_frontend_commit(void)
{
    FakeEntry e;
    UkBridge *b;
    UkBridgeVTable vt = FakeVTable;

    memset(&e, 0, sizeof(e));
    vt.commit_preedit = fake_frontend_commit_preedit;
    b = uk_bridge_new(&vt, &e);
    uk_bridge_set_enabled(b, 1);
    uk_bridge_set_input_method(b, UkTelex);
    type_ascii(b, "ddang ");

    if (strcmp(e.committed, "đang ") == 0 &&
        strcmp(e.preedit, "") == 0 &&
        strstr(e.events, "CH") != NULL &&
        strchr(e.events, 'E') == NULL) {
        printf("  ok   IBus preedit commit: CommitText -> Hide, khong empty\n");
    } else {
        printf("  FAIL IBus frontend commit: events=%s commit=%s preedit=%s\n",
               e.events, e.committed, e.preedit);
        Failures++;
    }
    uk_bridge_free(b);
}

/*----------------------------------------------------------------
  Trang thai dung chung: hai UkBridge trong cung mot "phien" dong vai
  hai ung dung khac nhau (vi du gedit va gnome-text-editor).
 ----------------------------------------------------------------*/
static void check_shared_state(void)
{
    FakeEntry e1, e2;
    UkBridge *app1, *app2;
    char *statePath;
    size_t statePathLen;
    FILE *stateFile;
    int changed;

    memset(&e1, 0, sizeof(e1));
    memset(&e2, 0, sizeof(e2));

    app1 = uk_bridge_new(&FakeVTable, &e1);
    app2 = uk_bridge_new(&FakeVTable, &e2);

    uk_bridge_set_enabled(app1, 1);
    uk_bridge_set_enabled(app2, 1);
    uk_bridge_state_dispatch(app2);   /* nuot su kien cua chinh no */

    /* app1 tat tieng Viet -> app2 phai thay */
    uk_bridge_toggle(app1);
    changed = uk_bridge_state_dispatch(app2);

    if (changed && uk_bridge_get_enabled(app2) == 0)
        printf("  ok   app1 tat  -> app2 tu tat theo\n");
    else {
        printf("  FAIL app1 tat  -> app2 van dang %s (changed=%d)\n",
               uk_bridge_get_enabled(app2) ? "bat" : "tat", changed);
        Failures++;
    }

    /* app2 doi kieu go -> app1 phai thay */
    uk_bridge_set_input_method(app2, UkVni);
    uk_bridge_state_dispatch(app1);

    if (uk_bridge_get_input_method(app1) == UkVni)
        printf("  ok   app2 sang VNI -> app1 sang theo\n");
    else {
        printf("  FAIL app2 sang VNI -> app1 van o kieu %d\n",
               uk_bridge_get_input_method(app1));
        Failures++;
    }

    /* State bi hong khong duoc bien atoi("abc") thanh Telex/Off. */
    uk_bridge_set_enabled(app1, 1);
    uk_bridge_state_dispatch(app1);  /* drain event cua chinh app1 */
    statePathLen = strlen(getenv("HOME")) + sizeof("/.unikey/state");
    statePath = (char *)malloc(statePathLen);
    snprintf(statePath, statePathLen, "%s/.unikey/state", getenv("HOME"));
    stateFile = fopen(statePath, "w");
    if (stateFile) {
        fputs("enabled=abc\nmethod=abc\n", stateFile);
        fclose(stateFile);
    }
    changed = uk_bridge_state_dispatch(app1);
    if (stateFile && !changed && uk_bridge_get_enabled(app1) == 1 &&
        uk_bridge_get_input_method(app1) == UkVni)
        printf("  ok   state sai cu phap: giu nguyen trang thai hien tai\n");
    else {
        printf("  FAIL state sai cu phap lam doi enabled/method\n");
        Failures++;
    }
    free(statePath);

    uk_bridge_free(app1);
    uk_bridge_free(app2);
}

/*----------------------------------------------------------------
  Parser options: tren amd64, ban cu ghi long 8 byte vao cac truong int 4
  byte va co the de len option ke ben. Dong thoi kiem tra writer atomic cua
  Preferences.
 ----------------------------------------------------------------*/
static void free_config_strings(UkXimOpt *opt)
{
    free(opt->macroFile);
    free(opt->usrKeyMapFile);
    opt->macroFile = NULL;
    opt->usrKeyMapFile = NULL;
}

typedef struct {
    int before;
    int flag;
    int after;
} BoolProbe;

static int file_contains(const char *path, const char *needle)
{
    char line[256];
    FILE *f = fopen(path, "r");

    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static void check_config_paths(void)
{
    char longHome[384];
    const char *currentHome = getenv("HOME");
    char *savedHome = currentHome ? strdup(currentHome) : NULL;
    const char *path;
    int longOk, missingOk;

    memset(longHome, 'h', sizeof(longHome));
    memcpy(longHome, "/tmp/", 5);
    longHome[sizeof(longHome) - 1] = 0;
    setenv("HOME", longHome, 1);
    path = UkGetDefConfFileName();
    longOk = path && strlen(path) > 128 &&
             strcmp(path + strlen(path) - strlen("/.unikey/options"),
                    "/.unikey/options") == 0;

    unsetenv("HOME");
    path = UkGetDefConfFileName();
    missingOk = path && *path;

    if (savedHome) {
        setenv("HOME", savedHome, 1);
        free(savedHome);
    } else
        unsetenv("HOME");

    if (longOk && missingOk)
        printf("  ok   config path: HOME dai/bi unset khong tran bo dem\n");
    else {
        printf("  FAIL config path (long=%d missing=%d)\n", longOk, missingOk);
        Failures++;
    }
}

static void check_config_parser(void)
{
    BoolProbe probe = { 0x13572468, -1, 0x24681357 };
    OptItem probeList[] = {
        {"Flag", "", offsetof(BoolProbe, flag), BoolOpt, NULL}
    };
    UkXimOpt opt;
    FILE *f;
    const char *path, *home;
    char *probePath, *backupPath, *keymapPath;
    size_t pathLen;
    struct stat before, after;
    int ok, atomicOk, backupOk, invalidOk, unreadableOk, userFallbackOk;

    check_config_paths();

    home = getenv("HOME");
    pathLen = strlen(home) + sizeof("/bool-probe");
    probePath = (char *)malloc(pathLen);
    snprintf(probePath, pathLen, "%s/bool-probe", home);
    f = fopen(probePath, "w");
    if (f) {
        fputs("Flag = Yes\n", f);
        fclose(f);
        ok = ParseOptFile(probePath, &probe, probeList, 1) &&
             probe.before == 0x13572468 && probe.flag == 1 &&
             probe.after == 0x24681357;
    } else {
        ok = 0;
    }
    unlink(probePath);
    free(probePath);

    if (ok)
        printf("  ok   BoolOpt 64-bit: khong ghi de hai truong ke ben\n");
    else {
        printf("  FAIL BoolOpt 64-bit ghi de bo nho\n");
        Failures++;
    }

    UkTestDefConfFile();
    path = UkGetDefConfFileName();
    f = fopen(path, "w");
    if (!f) {
        printf("  FAIL config parser: khong tao duoc file test\n");
        Failures++;
        return;
    }
    fputs("AutoSave = No\n"
          "TerminalMode = Preedit\n"
          "InitState = Off\n"
          "Input = VNI\n"
          "FreeStyle = No\n"
          "ModernStyle = Yes\n"
          "Bell = No\n"
          "EnableSpellCheck = No\n"
          "AutoRestoreNonVn = Yes\n"
          "LegacyOption = keep-me\n", f);
    fclose(f);

    UkSetDefOptions(&opt);
    ok = UkParseOptFile(path, &opt) &&
         opt.autoSave == 0 && opt.terminalMode == UkTerminalPreedit &&
         opt.enabled == 0 && opt.inputMethod == UkVni &&
         opt.uk.freeMarking == 0 && opt.uk.modernStyle == 1 &&
         opt.bellNotify == 0 && opt.uk.spellCheckEnabled == 0 &&
         opt.uk.autoNonVnRestore == 1;
    free_config_strings(&opt);

    if (ok)
        printf("  ok   config parser 64-bit: cac option khong de len nhau\n");
    else {
        printf("  FAIL config parser 64-bit\n");
        Failures++;
    }

    /* Tra lai default qua dung duong ghi ma Preferences se dung. File cu
       phai duoc giu mot lan de khong mat khoa legacy chua biet. */
    atomicOk = stat(path, &before) == 0;
    UkSetDefOptions(&opt);
    if (!UkWriteOptFileAtomic(path, &opt))
        atomicOk = 0;
    else if (!atomicOk || stat(path, &after) != 0 ||
             (before.st_dev == after.st_dev && before.st_ino == after.st_ino))
        atomicOk = 0;
    pathLen = strlen(path) + sizeof(".bak");
    backupPath = (char *)malloc(pathLen);
    snprintf(backupPath, pathLen, "%s.bak", path);
    backupOk = file_contains(backupPath, "LegacyOption = keep-me");
    free(backupPath);

    if (atomicOk && backupOk)
        printf("  ok   config writer: atomic + backup khoa legacy\n");
    else {
        printf("  FAIL config writer (atomic=%d backup=%d)\n",
               atomicOk, backupOk);
        Failures++;
    }
    free_config_strings(&opt);

    /* Gia tri sai cua mot khoa da biet phai lam parse that bai, khong duoc
       am tham bao thanh cong roi ap nua default nua config. */
    f = fopen(path, "w");
    if (f) {
        fputs("TerminalMode = khong-hop-le\n", f);
        fclose(f);
    }
    UkSetDefOptions(&opt);
    invalidOk = f && !UkParseOptFile(path, &opt);
    free_config_strings(&opt);
    if (invalidOk)
        printf("  ok   config parser: tu choi gia tri da biet bi sai\n");
    else {
        printf("  FAIL config parser chap nhan gia tri sai\n");
        Failures++;
    }

    /* File ton tai nhung khong doc duoc phai duoc giu nguyen, khong bi thay
       bang default chi vi fopen tra loi. */
    unreadableOk = stat(path, &before) == 0 && chmod(path, 0000) == 0 &&
                   !UkTestDefConfFile() && stat(path, &after) == 0 &&
                   before.st_dev == after.st_dev && before.st_ino == after.st_ino;
    chmod(path, 0600);
    if (unreadableOk)
        printf("  ok   config unreadable: giu nguyen file, khong ghi default\n");
    else {
        printf("  FAIL config unreadable bi thay the hoac bao thanh cong\n");
        Failures++;
    }

    /* USER keymap sai cu phap khong duoc de state/UI bao USER trong khi engine
       van am tham dung Telex hay keymap cu. */
    pathLen = strlen(home) + sizeof("/bad-keymap");
    keymapPath = (char *)malloc(pathLen);
    snprintf(keymapPath, pathLen, "%s/bad-keymap", home);
    f = fopen(keymapPath, "w");
    if (f) {
        fputs("x = LenhKhongTonTai\n", f);
        fclose(f);
    }
    UkSetDefOptions(&opt);
    opt.inputMethod = UkUsrIM;
    opt.usrKeyMapFile = strdup(keymapPath);
    userFallbackOk = f && opt.usrKeyMapFile &&
                     UkWriteOptFileAtomic(path, &opt);
    free_config_strings(&opt);
    if (userFallbackOk) {
        FakeEntry entry;
        UkBridge *bridge;
        memset(&entry, 0, sizeof(entry));
        bridge = uk_bridge_new(&FakeVTable, &entry);
        userFallbackOk = bridge &&
                         uk_bridge_get_input_method(bridge) == UkTelex;
        uk_bridge_free(bridge);
    }
    unlink(keymapPath);
    free(keymapPath);
    if (userFallbackOk)
        printf("  ok   USER keymap sai: fallback Telex, khong lech UI/engine\n");
    else {
        printf("  FAIL USER keymap sai khong fallback Telex\n");
        Failures++;
    }

    UkSetDefOptions(&opt);
    if (!UkWriteOptFileAtomic(path, &opt)) {
        printf("  FAIL khong khoi phuc duoc config test mac dinh\n");
        Failures++;
    }
    free_config_strings(&opt);
}

/* Options la cau hinh tinh. Moi context co mot inotify fd rieng nhung loi
   UniKey la singleton theo tien trinh; context khong focus tuyet doi khong
   duoc reload/reset engine global giua composition cua context dang focus. */
static void check_options_do_not_reload_mid_composition(void)
{
    FakeEntry focused_entry, background_entry;
    UkBridge *focused, *background;
    UkXimOpt opt;
    char shown[256];
    int ok;

    memset(&focused_entry, 0, sizeof(focused_entry));
    memset(&background_entry, 0, sizeof(background_entry));
    focused = uk_bridge_new(&FakeVTable, &focused_entry);
    background = uk_bridge_new(&FakeVTable, &background_entry);
    uk_bridge_state_dispatch(focused);
    uk_bridge_state_dispatch(background);

    type_ascii(focused, "tie");
    UkSetDefOptions(&opt);
    opt.uk.modernStyle = 1;
    ok = UkWriteOptFileAtomic(UkGetDefConfFileName(), &opt) &&
         !uk_bridge_state_dispatch(background);
    free_config_strings(&opt);

    type_ascii(focused, "engs");
    ok = ok && strcmp(visible(&focused_entry, shown, sizeof(shown)),
                      "tiếng") == 0;

    UkSetDefOptions(&opt);
    if (!UkWriteOptFileAtomic(UkGetDefConfFileName(), &opt))
        ok = 0;
    free_config_strings(&opt);
    uk_bridge_free(background);
    uk_bridge_free(focused);

    if (ok)
        printf("  ok   options tinh: context nen khong reset composition\n");
    else {
        printf("  FAIL options reload lam lech singleton/context\n");
        Failures++;
    }
}

/*----------------------------------------------------------------*/
int main(void)
{
    /* Khong dung ~/.unikey cua nguoi dung: test phai doc lap cau hinh may. */
    char tmpl[] = "/tmp/ukbridge-test-XXXXXX";
    char *home = mkdtemp(tmpl);
    if (!home || setenv("HOME", home, 1) != 0) {
        fprintf(stderr, "khong tao duoc HOME tam cho test\n");
        return 1;
    }

    printf("ukbridge:\n");

    check_config_parser();

    /* TELEX */
    check(UkTelex, "telex", "tieengs",       "tiếng");
    check(UkTelex, "telex", "vieejt",        "việt");
    check(UkTelex, "telex", "ddaay",         "đây");
    /* Mac dinh ModernStyle=No => kieu cu "ho'a", khong phai "hoa'" */
    check(UkTelex, "telex", "hoaf",          "hòa");
    check(UkTelex, "telex", "tieengs vieejt","tiếng việt");
    check(UkTelex, "telex", "Vieejt Nam",    "Việt Nam");

    /* Spell-check cua engine 1.0: tu khong phai tieng Viet giu nguyen,
       khong phai go doubling nhu ban cu */
    check(UkTelex, "telex", "linux",         "linux");
    check(UkTelex, "telex", "changes",       "changes");

    /* VNI */
    check(UkVni,   "vni",   "tie61ng",       "tiếng");
    check(UkVni,   "vni",   "vie65t",        "việt");
    check(UkVni,   "vni",   "d9a6y",         "đây");

    /* VIQR */
    check(UkViqr,  "viqr",  "tie^'ng",       "tiếng");
    check(UkViqr,  "viqr",  "vie^.t",        "việt");

    /* BackSpace phai xoa theo ky tu nhin thay, khong phai theo byte UTF-8 */
    check_backspace("tieengs", 1, "tiến");
    check_backspace("tieengs", 2, "tiế");
    check_backspace("ddaay",   1, "đâ");

    /* Che do DIRECT (chi cho client co surrounding that): khong preedit, xoa
       bang erase_before_cursor. Ket qua phai giong het che do preedit. */
    check_direct(UkTelex, "telex", "tieengs",        "tiếng");
    check_direct(UkTelex, "telex", "vieejt",         "việt");
    check_direct(UkTelex, "telex", "ddaay",          "đây");
    check_direct(UkTelex, "telex", "tieengs vieejt", "tiếng việt");
    check_direct(UkTelex, "telex", "linux",          "linux");
    check_direct(UkVni,   "vni",   "tie61ng",        "tiếng");

    /* DIRECT chi dung cho client da xac nhan delete-surrounding. Giu cac cau
       nay de bao dam nhanh do van dung sau khi policy loai VTE ra khoi no. */
    check_direct(UkTelex, "telex", "ddwowcj",        "được");
    check_pending_direct_handoff();
    check_direct(UkTelex, "telex", "chuwa",          "chưa");
    check_direct(UkTelex, "telex", "naof",           "nào");
    check_direct(UkTelex, "telex", "xem ddwowcj chuwa naof", "xem được chưa nào");

    check_mode_switch();
    check_ibus_policy();
    check_reset_cancels();
    check_ibus_frontend_commit();
    check_shared_state();
    check_options_do_not_reload_mid_composition();

    if (Failures == 0)
        printf("=> tat ca deu dat\n");
    else
        printf("=> %d truong hop sai\n", Failures);

    return Failures == 0 ? 0 : 1;
}
