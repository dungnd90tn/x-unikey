// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey standalone bridge -- xem ukbridge.h */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/inotify.h>

#include "keycons.h"
#include "unikey.h"
#include "vnconv.h"
#include "ukopt.h"
#include "ukbridge.h"
#include "ukkeys.h"

/*----------------------------------------------------------------
  Bo dem preedit: chuoi UTF-8 tu quan ly, khong dung glib de plugin
  Qt cung xai duoc.
 ----------------------------------------------------------------*/
typedef struct {
    char  *data;
    int    len;      /* so byte, khong tinh NUL */
    int    cap;
} UkBuf;

struct _UkBridge {
    UkBridgeVTable vt;
    void          *user_data;
    UkBuf          preedit;
    UkBuf          commit_buf;   /* dung cho che do DIRECT */
    int            enabled;
    int            inputMethod;

    int            inotify_fd;
    int            inotify_wd;

    int            direct_mode;   /* commit thang thay vi dung preedit */
    int            terminal_mode; /* UkTerminalOff | UkTerminalPreedit */
};

static int GlobalInited = 0;

static void state_path(char *buf, int n);
static void state_write(UkBridge *b);
static void state_read(UkBridge *b);
static void state_watch_init(UkBridge *b);

/*----------------------------------------------------------------*/
static void buf_init(UkBuf *b)
{
    b->cap = 64;
    b->data = (char *)malloc(b->cap);
    b->data[0] = '\0';
    b->len = 0;
}

static void buf_free(UkBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static void buf_clear(UkBuf *b)
{
    b->len = 0;
    b->data[0] = '\0';
}

static void buf_reserve(UkBuf *b, int extra)
{
    int need = b->len + extra + 1;
    if (need <= b->cap)
        return;
    while (b->cap < need)
        b->cap *= 2;
    b->data = (char *)realloc(b->data, b->cap);
}

static void buf_append(UkBuf *b, const char *bytes, int n)
{
    if (n <= 0)
        return;
    buf_reserve(b, n);
    memcpy(b->data + b->len, bytes, n);
    b->len += n;
    b->data[b->len] = '\0';
}

/* Xoa `n` KY TU (khong phai byte) o cuoi chuoi UTF-8. */
static void buf_chop_utf8(UkBuf *b, int n)
{
    int i = b->len;
    while (n > 0 && i > 0) {
        i--;
        /* lui qua cac byte tiep noi 10xxxxxx */
        while (i > 0 && ((unsigned char)b->data[i] & 0xC0) == 0x80)
            i--;
        n--;
    }
    b->len = i;
    b->data[b->len] = '\0';
}

/*----------------------------------------------------------------
  Khoi tao engine dung chung. UkEngine la mot instance toan cuc trong
  libukint, moi tien trinh chi setup mot lan.
 ----------------------------------------------------------------*/
void uk_bridge_global_init(void)
{
    if (GlobalInited)
        return;
    UnikeySetup();
    /* Buoc engine xuat UTF-8: front-end hien dai chi nhan UTF-8, va nho
       vay khong phai goi vnconv o duong key-press nong. */
    UnikeySetOutputCharset(CONV_CHARSET_XUTF8);
    GlobalInited = 1;
}

/*----------------------------------------------------------------*/
UkBridge *uk_bridge_new(const UkBridgeVTable *vt, void *user_data)
{
    UkBridge *b = (UkBridge *)calloc(1, sizeof(UkBridge));
    uk_bridge_global_init();
    b->vt = *vt;
    b->user_data = user_data;
    b->enabled = 1;
    b->inputMethod = UkTelex;
    b->inotify_fd = -1;
    b->inotify_wd = -1;
    /* DIRECT la mac dinh cho front-end nao cung cap erase_before_cursor
       (hien la IBus engine): go muot giong UniKey tren Windows, khong gach chan.
       Cac front-end khac (GTK/Qt) khong co callback do nen tu dong dung PREEDIT.

       Front-end chiu trach nhiem TAT dong o nhung o nhap ma co che xoa khong
       chay -- terminal la truong hop da biet, xem set_content_type trong
       src/unikey-ibus/engine.c. */
    b->direct_mode = (vt->erase_before_cursor != 0);
    buf_init(&b->preedit);
    buf_init(&b->commit_buf);
    uk_bridge_load_config(b);
    state_read(b);          /* trang thai dung chung de len tren file cau hinh */
    state_watch_init(b);
    return b;
}

void uk_bridge_free(UkBridge *b)
{
    if (!b)
        return;
    if (b->inotify_fd >= 0)
        close(b->inotify_fd);
    buf_free(&b->preedit);
    buf_free(&b->commit_buf);
    free(b);
}

/*----------------------------------------------------------------*/
static void emit_preedit(UkBridge *b)
{
    if (b->vt.preedit_changed)
        b->vt.preedit_changed(b->user_data, b->preedit.data);
}

static void emit_commit(UkBridge *b, const char *s)
{
    if (s && *s && b->vt.commit)
        b->vt.commit(b->user_data, s);
}

/*----------------------------------------------------------------
  Ap ket qua mot lan goi engine vao preedit.
  Engine tra ve: xoa `UnikeyBackspaces` ky tu cuoi, roi noi them
  `UnikeyBufChars` byte trong UnikeyBuf.
 ----------------------------------------------------------------*/
static void apply_engine_output(UkBridge *b)
{
    if (b->direct_mode) {
        /* Khong co preedit: xoa thang van ban da commit roi commit phan moi.
           Day la mo hinh cua UniKey tren Windows -- nguoi dung khong thay
           gach chan va khong co trang thai "dang go do". */
        if (UnikeyBackspaces > 0 && b->vt.erase_before_cursor)
            b->vt.erase_before_cursor(b->user_data, UnikeyBackspaces);
        if (UnikeyBufChars > 0) {
            /* Bo dem co gian, khong dung mang co dinh: macro co the dai toi
               MAX_MACRO_TEXT_LEN (1024) byte, mang 256 se cat cut am tham. */
            buf_clear(&b->commit_buf);
            buf_append(&b->commit_buf, (const char *)UnikeyBuf, UnikeyBufChars);
            emit_commit(b, b->commit_buf.data);
        }
        return;
    }

    if (UnikeyBackspaces > 0)
        buf_chop_utf8(&b->preedit, UnikeyBackspaces);
    if (UnikeyBufChars > 0)
        buf_append(&b->preedit, (const char *)UnikeyBuf, UnikeyBufChars);
}

/*----------------------------------------------------------------*/
void uk_bridge_flush(UkBridge *b)
{
    if (b->preedit.len > 0) {
        char *s = strdup(b->preedit.data);
        buf_clear(&b->preedit);
        emit_preedit(b);          /* xoa preedit TRUOC khi commit */
        emit_commit(b, s);
        free(s);
    }
    UnikeyResetBuf();
}

void uk_bridge_reset(UkBridge *b)
{
    if (b->preedit.len > 0) {
        buf_clear(&b->preedit);
        emit_preedit(b);
    }
    UnikeyResetBuf();
}

const char *uk_bridge_preedit(UkBridge *b)
{
    return b->preedit.data;
}

/*----------------------------------------------------------------*/
UkBridgeResult uk_bridge_key(UkBridge *b, unsigned int unicode,
                             int shift_pressed, int capslock_on)
{
    char utf8[8];
    int  n;

    if (!b->enabled || unicode == 0)
        return UK_BRIDGE_PASS;

    /* Engine chi lam viec voi ky tu 8-bit; ky tu ngoai vung do (chu Han,
       emoji, chu Viet dan tu clipboard...) ket thuc am tiet hien tai. */
    if (unicode > 0xFF) {
        uk_bridge_flush(b);
        return UK_BRIDGE_PASS;
    }

    UnikeySetCapsState(shift_pressed, capslock_on);
    UnikeyFilter(unicode);

    if (UnikeyBufChars == 0 && UnikeyBackspaces == 0) {
        /* Engine cho qua nguyen ven. */
        n = 0;
        if (unicode < 0x80) {
            utf8[n++] = (char)unicode;
        } else {
            utf8[n++] = (char)(0xC0 | (unicode >> 6));
            utf8[n++] = (char)(0x80 | (unicode & 0x3F));
        }
        utf8[n] = '\0';

        if (b->direct_mode) {
            /* Tu commit ky tu, KHONG tra ve PASS.
               Neu tra PASS thi ung dung tu chen ky tu qua duong su kien ban
               phim, trong khi lenh xoa di duong input-method -- hai duong
               khong dong bo, va khi go nhanh thi dao thu tu: "đấy" ra
               "đâdấyd". Cho tat ca di chung mot kenh commit thi thu tu chac
               chan dung. */
            emit_commit(b, utf8);
            return UK_BRIDGE_CONSUMED;
        }
        buf_append(&b->preedit, utf8, n);
    } else {
        apply_engine_output(b);
    }

    if (b->direct_mode)
        return UK_BRIDGE_CONSUMED;

    /* Het am tiet (dau cach, dau cau...) thi chot preedit thanh van ban that. */
    if (UnikeyAtWordBeginning())
        uk_bridge_flush(b);
    else
        emit_preedit(b);

    return UK_BRIDGE_CONSUMED;
}

/*----------------------------------------------------------------*/
UkBridgeResult uk_bridge_backspace(UkBridge *b)
{
    if (b->direct_mode) {
        if (!b->enabled)
            return UK_BRIDGE_PASS;

        /* Bao cho engine biet co phim xoa, de bo dem noi bo lui theo... */
        UnikeyBackspacePress();

        /* ...nhung KHONG BAO GIO nuot phim BackSpace.
           Truoc day cho nay nuot phim roi goi erase_before_cursor de tu xoa.
           Trong ung dung ma lenh xoa bi lo di (VTE/gnome-terminal), ket qua la
           phim mat ma chu khong bi xoa -- nguoi dung phai bam giu mot luc,
           den khi bo dem engine can thi phim moi lot qua duoc.
           De ung dung tu xoa thi phim BackSpace luon co tac dung, o moi noi. */
        return UK_BRIDGE_PASS;
    }

    if (!b->enabled || b->preedit.len == 0) {
        /* Khong con gi trong preedit -- de app tu xoa van ban that. */
        UnikeyResetBuf();
        return UK_BRIDGE_PASS;
    }

    UnikeyBackspacePress();
    if (UnikeyBackspaces > 0 || UnikeyBufChars > 0)
        apply_engine_output(b);
    else if (!b->direct_mode)
        buf_chop_utf8(&b->preedit, 1);

    if (b->direct_mode)
        return UK_BRIDGE_CONSUMED;

    emit_preedit(b);
    return UK_BRIDGE_CONSUMED;
}

/*----------------------------------------------------------------*/
void uk_bridge_set_direct_mode(UkBridge *b, int on)
{
    if (b->direct_mode == !!on)
        return;
    uk_bridge_flush(b);      /* chot phan dang go truoc khi doi che do */
    b->direct_mode = !!on;
}

int uk_bridge_get_direct_mode(UkBridge *b)
{
    return b->direct_mode;
}

int uk_bridge_get_terminal_mode(UkBridge *b)
{
    return b->terminal_mode;
}

/*----------------------------------------------------------------*/
int uk_bridge_get_enabled(UkBridge *b)
{
    return b->enabled;
}

void uk_bridge_set_enabled(UkBridge *b, int on)
{
    if (b->enabled == on)
        return;
    uk_bridge_flush(b);
    b->enabled = on;
    state_write(b);
}

void uk_bridge_toggle(UkBridge *b)
{
    uk_bridge_set_enabled(b, !b->enabled);
}

void uk_bridge_set_input_method(UkBridge *b, int im)
{
    uk_bridge_flush(b);
    b->inputMethod = im;
    UnikeySetInputMethod((UkInputMethod)im);
    state_write(b);
}

int uk_bridge_get_input_method(UkBridge *b)
{
    return b->inputMethod;
}

void uk_bridge_set_single_mode(UkBridge *b)
{
    (void)b;
    UnikeySetSingleMode();
}

void uk_bridge_restore_keystrokes(UkBridge *b)
{
    UnikeyRestoreKeyStrokes();
    apply_engine_output(b);
    emit_preedit(b);
}

/*----------------------------------------------------------------*/
int uk_bridge_apply_action(UkBridge *b, int action)
{
    switch ((UkKeyAction)action) {
    case UK_ACTION_TOGGLE:      uk_bridge_toggle(b);                        return 1;
    case UK_ACTION_TELEX:       uk_bridge_set_input_method(b, UkTelex);     return 1;
    case UK_ACTION_VNI:         uk_bridge_set_input_method(b, UkVni);       return 1;
    case UK_ACTION_VIQR:        uk_bridge_set_input_method(b, UkViqr);      return 1;
    case UK_ACTION_USER_IM:     uk_bridge_set_input_method(b, UkUsrIM);     return 1;
    case UK_ACTION_RESTORE:     uk_bridge_restore_keystrokes(b);            return 1;
    case UK_ACTION_SINGLE_MODE: uk_bridge_set_single_mode(b);               return 1;
    case UK_ACTION_NONE:
    default:                                                                return 0;
    }
}

/*================================================================
  Trang thai dung chung giua cac tien trinh (~/.unikey/state)
 ================================================================*/

static void state_path(char *buf, int n)
{
    const char *home = getenv("HOME");
    snprintf(buf, n, "%s/.unikey/state", home ? home : "/tmp");
}

/* Ghi nguyen tu: ghi ra file tam roi rename, de tien trinh khac khong bao gio
   doc phai noi dung dang viet do dang. rename() cung sinh IN_MOVED_TO -- day la
   su kien inotify ma ta theo doi. */
static void state_write(UkBridge *b)
{
    char path[256], tmp[300];
    FILE *f;

    state_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());

    f = fopen(tmp, "w");
    if (!f)
        return;
    fprintf(f, "enabled=%d\nmethod=%d\n", b->enabled, b->inputMethod);
    fclose(f);

    if (rename(tmp, path) != 0)
        unlink(tmp);
}

static void state_read(UkBridge *b)
{
    char path[256], line[128];
    FILE *f;
    int enabled = b->enabled;
    int method = b->inputMethod;

    state_path(path, sizeof(path));
    f = fopen(path, "r");
    if (!f)
        return;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "enabled=", 8) == 0)
            enabled = atoi(line + 8);
        else if (strncmp(line, "method=", 7) == 0)
            method = atoi(line + 7);
    }
    fclose(f);

    if (method != b->inputMethod) {
        b->inputMethod = method;
        UnikeySetInputMethod((UkInputMethod)method);
    }
    if (enabled != b->enabled) {
        uk_bridge_flush(b);
        b->enabled = enabled;
    }
}

static void state_watch_init(UkBridge *b)
{
    char path[256], *slash;

    b->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (b->inotify_fd < 0)
        return;

    /* Theo doi THU MUC chu khong phai file: state duoc thay bang rename() nen
       watch tren chinh file se mat hieu luc ngay sau lan ghi dau tien. */
    state_path(path, sizeof(path));
    slash = strrchr(path, '/');
    if (slash)
        *slash = '\0';

    b->inotify_wd = inotify_add_watch(b->inotify_fd, path,
                                      IN_MOVED_TO | IN_CLOSE_WRITE);
    if (b->inotify_wd < 0) {
        close(b->inotify_fd);
        b->inotify_fd = -1;
    }
}

int uk_bridge_state_fd(UkBridge *b)
{
    return b->inotify_fd;
}

int uk_bridge_state_dispatch(UkBridge *b)
{
    char ebuf[4096];
    ssize_t n;
    int relevant = 0;

    if (b->inotify_fd < 0)
        return 0;

    while ((n = read(b->inotify_fd, ebuf, sizeof(ebuf))) > 0) {
        ssize_t i = 0;
        while (i + (ssize_t)sizeof(struct inotify_event) <= n) {
            struct inotify_event *ev = (struct inotify_event *)(ebuf + i);
            if (ev->len > 0 && strcmp(ev->name, "state") == 0)
                relevant = 1;
            i += sizeof(struct inotify_event) + ev->len;
        }
    }

    if (!relevant)
        return 0;

    /* Khong can loc su kien do chinh minh gay ra: state_read() so sanh voi
       trang thai hien tai nen doc lai chinh minh la mot phep khong lam gi.
       Truoc day co mot co "writing_state" de loc, nhung no bi ket lai tu lan
       ghi truoc va nuot mat thay doi that su cua tien trinh khac. */
    {
        int old_enabled = b->enabled, old_im = b->inputMethod;
        state_read(b);
        return (old_enabled != b->enabled || old_im != b->inputMethod);
    }
}

/*----------------------------------------------------------------
  Dung chung ~/.unikey/options voi ukxim -- mot file cau hinh cho ca
  bo go, khong phat minh dinh dang moi.
 ----------------------------------------------------------------*/
void uk_bridge_load_config(UkBridge *b)
{
    UkXimOpt opt;
    char *fname;

    UkSetDefOptions(&opt);
    UkTestDefConfFile();
    fname = UkGetDefConfFileName();   /* bo dem tinh trong ukopt.c, khong duoc free */
    if (fname)
        UkParseOptFile(fname, &opt);

    if (opt.usrKeyMapFile && *opt.usrKeyMapFile)
        UnikeyLoadUserKeyMap(opt.usrKeyMapFile);
    if (opt.macroFile && *opt.macroFile) {
        if (UnikeyLoadMacroTable(opt.macroFile))
            opt.uk.macroEnabled = 1;
    }

    UnikeySetOptions(&opt.uk);
    b->terminal_mode = opt.terminalMode;
    b->enabled = opt.enabled;
    b->inputMethod = opt.inputMethod;
    UnikeySetInputMethod((UkInputMethod)opt.inputMethod);

    /* Charset luon la UTF-8 o front-end hien dai; tuy chon Charset trong
       file chi con y nghia voi ukxim (X11). */
    UnikeySetOutputCharset(CONV_CHARSET_XUTF8);

    free(opt.macroFile);
    free(opt.usrKeyMapFile);
}
