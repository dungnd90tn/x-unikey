// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* Test cho ukbridge: go mot chuoi phim, kiem tra van ban thu duoc.
 *
 * Chay khong can X/Wayland/toolkit nao -- day la toan bo logic go tieng Viet
 * cua cac module GTK3/GTK4/Qt, nen test o day phu duoc phan rui ro nhat.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keycons.h"
#include "ukbridge.h"

/*----------------------------------------------------------------
  Front-end gia: gom commit + preedit lai thanh van ban cuoi cung,
  dung nhu mot o nhap that se hien thi.
 ----------------------------------------------------------------*/
typedef struct {
    char committed[512];
    char preedit[256];
} FakeEntry;

static void fake_commit(void *user, const char *utf8)
{
    FakeEntry *e = (FakeEntry *)user;
    strncat(e->committed, utf8, sizeof(e->committed) - strlen(e->committed) - 1);
}

static void fake_preedit(void *user, const char *utf8)
{
    FakeEntry *e = (FakeEntry *)user;
    snprintf(e->preedit, sizeof(e->preedit), "%s", utf8 ? utf8 : "");
}

static const UkBridgeVTable FakeVTable = { fake_commit, fake_preedit };

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

/* Mo phong dung thu ma engine that lam: gui n phim BackSpace, moi phim xoa
   mot ky tu o cuoi. */
static void direct_erase(void *user, int nchars)
{
    field_chop_utf8((FakeField *)user, nchars);
}

static const UkBridgeVTable DirectVTable = {
    direct_commit,
    direct_preedit,
    direct_erase
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
  Trang thai dung chung: hai UkBridge trong cung mot "phien" dong vai
  hai ung dung khac nhau (vi du gedit va gnome-text-editor).
 ----------------------------------------------------------------*/
static void check_shared_state(void)
{
    FakeEntry e1, e2;
    UkBridge *app1, *app2;
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

    uk_bridge_free(app1);
    uk_bridge_free(app2);
}

/*----------------------------------------------------------------*/
int main(void)
{
    /* Khong dung ~/.unikey cua nguoi dung: test phai doc lap cau hinh may. */
    char tmpl[] = "/tmp/ukbridge-test-XXXXXX";
    char *home = mkdtemp(tmpl);
    if (home)
        setenv("HOME", home, 1);

    printf("ukbridge:\n");

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

    /* Che do DIRECT (mac dinh cua IBus engine): khong preedit, xoa bang
       erase_before_cursor. Ket qua phai giong het che do preedit. */
    check_direct(UkTelex, "telex", "tieengs",        "tiếng");
    check_direct(UkTelex, "telex", "vieejt",         "việt");
    check_direct(UkTelex, "telex", "ddaay",          "đây");
    check_direct(UkTelex, "telex", "tieengs vieejt", "tiếng việt");
    check_direct(UkTelex, "telex", "linux",          "linux");
    check_direct(UkVni,   "vni",   "tie61ng",        "tiếng");

    /* Ca cau da tung ra sai that: che do DIRECT dung delete-surrounding, ma
       VTE (gnome-terminal) lo di lenh xoa, nen ky tu cu con nguyen va chu moi
       noi them vao -- "xem dduocjduocj..." thay vi "xem duoc". Gio DIRECT gui
       phim BackSpace that. Giu ca cau nay lam moc chong tai pham. */
    check_direct(UkTelex, "telex", "ddwowcj",        "được");
    check_direct(UkTelex, "telex", "chuwa",          "chưa");
    check_direct(UkTelex, "telex", "naof",           "nào");
    check_direct(UkTelex, "telex", "xem ddwowcj chuwa naof", "xem được chưa nào");

    check_mode_switch();
    check_shared_state();

    if (Failures == 0)
        printf("=> tat ca deu dat\n");
    else
        printf("=> %d truong hop sai\n", Failures);

    return Failures == 0 ? 0 : 1;
}
