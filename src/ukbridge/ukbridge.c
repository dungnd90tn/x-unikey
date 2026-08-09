// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey standalone bridge -- xem ukbridge.h */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
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
    int            passthrough_pending; /* raw key da vao app, cho DIRECT */
    int            terminal_mode; /* UkTerminalOff | UkTerminalPreedit */
    int            user_keymap_loaded;
};

static int GlobalInited = 0;

static char *state_path(void);
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
    /* PREEDIT la mac dinh an toan. DIRECT chi danh cho front-end biet chac
       erase_before_cursor sua van ban that; capability protocol cua mot
       textarea an (VS Code/xterm.js) khong phai dam bao do. */
    b->direct_mode = 0;
    b->passthrough_pending = 0;
    buf_init(&b->preedit);
    buf_init(&b->commit_buf);
    uk_bridge_load_config(b);
    /* Dat watch truoc snapshot state: save xay ra trong luc khoi tao se hoac
       nam trong snapshot, hoac de lai event cho lan dispatch dau tien. */
    state_watch_init(b);
    state_read(b);          /* trang thai dung chung de len tren file cau hinh */
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
        if (b->vt.commit_preedit) {
            /* Frontend tu chon dung signal va thu tu ket thuc composition.
               IBus can CommitText vao range hien tai roi HidePreeditText;
               bridge khong phat empty-preedit chung truoc/sau callback. */
            b->vt.commit_preedit(b->user_data, s);
        } else {
            emit_preedit(b);
            emit_commit(b, s);
        }
        free(s);
    }
    b->passthrough_pending = 0;
    UnikeyResetBuf();
}

void uk_bridge_reset(UkBridge *b)
{
    if (b->preedit.len > 0) {
        buf_clear(&b->preedit);
        emit_preedit(b);
    }
    b->passthrough_pending = 0;
    UnikeyResetBuf();
}

const char *uk_bridge_preedit(UkBridge *b)
{
    return b->preedit.data;
}

/* Mot text-entry DIRECT candidate co the nhan phim dau tien truoc khi IBus
   cong bo SURROUNDING_TEXT. App tu chen raw key do; bridge chi dua no vao bo
   dem UniKey bang pass(), khong emit gi. Khi caps toi va bat DIRECT, engine
   van biet ky tu da nam truoc caret de lan bien doi sau xoa/thay dung cho. */
void uk_bridge_note_passthrough_key(UkBridge *b, unsigned int unicode,
                                    int shift_pressed, int capslock_on)
{
    if (!b->enabled || unicode == 0 || unicode > 0xFF)
        return;
    UnikeySetCapsState(shift_pressed, capslock_on);
    UnikeyPutChar(unicode);
    b->passthrough_pending = 1;
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
    if (on && b->passthrough_pending && b->preedit.len == 0) {
        /* Raw key da nam trong app va trong bo dem UniKey. Khong flush/reset
           luc handoff, neu khong am tiet se mat ky tu dau. */
        b->direct_mode = 1;
        b->passthrough_pending = 0;
        return;
    }
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
    if (im != UkTelex && im != UkVni && im != UkViqr && im != UkUsrIM)
        return;
    if (im == UkUsrIM && !b->user_keymap_loaded)
        return;
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

static char *state_path(void)
{
    const char *options = UkGetDefConfFileName();
    const char *slash;
    size_t dirLen;
    char *path;

    if (!options)
        return NULL;
    slash = strrchr(options, '/');
    if (!slash)
        return strdup("state");

    dirLen = (size_t)(slash - options) + 1;
    path = (char *)malloc(dirLen + sizeof("state"));
    if (!path)
        return NULL;
    memcpy(path, options, dirLen);
    memcpy(path + dirLen, "state", sizeof("state"));
    return path;
}

/* Ghi nguyen tu: ghi ra file tam roi rename, de tien trinh khac khong bao gio
   doc phai noi dung dang viet do dang. rename() cung sinh IN_MOVED_TO -- day la
   su kien inotify ma ta theo doi. */
static void state_write(UkBridge *b)
{
    char *path, *tmp;
    size_t tmpLen;
    FILE *f;
    int fd, ok = 1;

    path = state_path();
    if (!path)
        return;
    tmpLen = strlen(path) + sizeof(".tmp.XXXXXX");
    tmp = (char *)malloc(tmpLen);
    if (!tmp) {
        free(path);
        return;
    }
    snprintf(tmp, tmpLen, "%s.tmp.XXXXXX", path);

    fd = mkstemp(tmp);
    if (fd < 0) {
        free(tmp);
        free(path);
        return;
    }
    f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmp);
        free(tmp);
        free(path);
        return;
    }

    if (fprintf(f, "enabled=%d\nmethod=%d\n",
                b->enabled, b->inputMethod) < 0 ||
        fflush(f) != 0 || fsync(fileno(f)) != 0)
        ok = 0;
    if (fclose(f) != 0)
        ok = 0;

    if (ok && rename(tmp, path) != 0)
        ok = 0;
    if (ok) {
        char *dir = strdup(path);
        char *slash = dir ? strrchr(dir, '/') : NULL;
        int dir_fd;

        if (slash) {
            if (slash == dir)
                slash[1] = '\0';
            else
                *slash = '\0';
        }
        dir_fd = dir ? open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
        if (dir_fd >= 0) {
            (void)fsync(dir_fd);
            (void)close(dir_fd);
        }
        free(dir);
    }
    if (!ok)
        unlink(tmp);
    free(tmp);
    free(path);
}

static int state_parse_int(const char *text, int *value)
{
    char *end;
    long parsed;

    if (!text || !value)
        return 0;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || parsed < INT_MIN || parsed > INT_MAX)
        return 0;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end)
        return 0;
    *value = (int)parsed;
    return 1;
}

static void state_read(UkBridge *b)
{
    char *path, line[128];
    FILE *f;
    int enabled = b->enabled;
    int method = b->inputMethod;
    int parsed;

    path = state_path();
    if (!path)
        return;
    f = fopen(path, "r");
    free(path);
    if (!f)
        return;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "enabled=", 8) == 0 &&
            state_parse_int(line + 8, &parsed) &&
            (parsed == 0 || parsed == 1))
            enabled = parsed;
        else if (strncmp(line, "method=", 7) == 0 &&
                 state_parse_int(line + 7, &parsed))
            method = parsed;
    }
    fclose(f);

    enabled = enabled ? 1 : 0;
    if (method != UkTelex && method != UkVni && method != UkViqr &&
        method != UkUsrIM)
        method = b->inputMethod;
    if (method == UkUsrIM && !b->user_keymap_loaded)
        method = UkTelex;

    /* Chot/reset am tiet cu truoc khi doi bat/tat hoac bo go. */
    if (method != b->inputMethod || enabled != b->enabled)
        uk_bridge_flush(b);
    if (method != b->inputMethod) {
        b->inputMethod = method;
        UnikeySetInputMethod((UkInputMethod)method);
    }
    if (enabled != b->enabled)
        b->enabled = enabled;
}

static void state_watch_init(UkBridge *b)
{
    char *path, *slash;

    b->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (b->inotify_fd < 0)
        return;

    /* Theo doi THU MUC chu khong phai file: state duoc thay bang rename() nen
       watch tren chinh file se mat hieu luc sau lan ghi dau. */
    path = state_path();
    if (!path) {
        close(b->inotify_fd);
        b->inotify_fd = -1;
        return;
    }
    slash = strrchr(path, '/');
    if (slash)
        *slash = '\0';

    b->inotify_wd = inotify_add_watch(b->inotify_fd, path,
                                      IN_MOVED_TO | IN_CLOSE_WRITE);
    free(path);
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
    union {
        struct inotify_event alignment;
        char bytes[4096];
    } ebuf;
    ssize_t n;
    int relevant = 0;

    if (b->inotify_fd < 0)
        return 0;

    while ((n = read(b->inotify_fd, ebuf.bytes, sizeof(ebuf.bytes))) > 0) {
        ssize_t i = 0;
        while (i + (ssize_t)sizeof(struct inotify_event) <= n) {
            struct inotify_event *ev =
                (struct inotify_event *)(ebuf.bytes + i);
            if ((ev->mask & IN_Q_OVERFLOW) ||
                (ev->len > 0 && strcmp(ev->name, "state") == 0))
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
        return old_enabled != b->enabled || old_im != b->inputMethod;
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
    int user_keymap_loaded = 0;

    UkSetDefOptions(&opt);
    if (!UkTestDefConfFile())
        return;
    fname = UkGetDefConfFileName();   /* bo dem tinh trong ukopt.c, khong duoc free */
    if (!fname || !UkParseOptFile(fname, &opt)) {
        free(opt.macroFile);
        free(opt.usrKeyMapFile);
        return;
    }

    if (opt.usrKeyMapFile && *opt.usrKeyMapFile)
        user_keymap_loaded = UnikeyLoadUserKeyMap(opt.usrKeyMapFile);
    if (opt.inputMethod == UkUsrIM && !user_keymap_loaded)
        opt.inputMethod = UkTelex;
    if (opt.macroFile && *opt.macroFile) {
        if (UnikeyLoadMacroTable(opt.macroFile))
            opt.uk.macroEnabled = 1;
    }

    UnikeySetOptions(&opt.uk);
    b->terminal_mode = opt.terminalMode;
    b->user_keymap_loaded = user_keymap_loaded;
    b->enabled = opt.enabled;
    b->inputMethod = opt.inputMethod;
    UnikeySetInputMethod((UkInputMethod)opt.inputMethod);

    /* Charset luon la UTF-8 o front-end hien dai; tuy chon Charset trong
       file chi con y nghia voi ukxim (X11). */
    UnikeySetOutputCharset(CONV_CHARSET_XUTF8);

    free(opt.macroFile);
    free(opt.usrKeyMapFile);
}
