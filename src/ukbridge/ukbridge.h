// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey standalone bridge
 *
 * Lop trung gian giua UkEngine va cac front-end hien dai (GTK3/GTK4/Qt/Wayland).
 *
 * UkEngine noi chuyen bang cap (so backspace, bytes moi). Cac front-end cu bien
 * "so backspace" thanh phim BackSpace gia -- chi chay tren X11 va pha undo cua app.
 * Lop nay giu am tiet dang go trong mot chuoi preedit, nen "backspace" chi con la
 * xoa N ky tu cuoi cua chuoi do: khong con phim gia, chay duoc tren Wayland.
 *
 * Thuan C, khong phu thuoc toolkit nao.
 */
#ifndef __UK_BRIDGE_H
#define __UK_BRIDGE_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _UkBridge UkBridge;

/* Front-end cung cap cac callback nay. Chuoi truyen vao luon la UTF-8,
   da NUL-terminated.

   `erase_before_cursor` chi can khi front-end muon dung che do commit truc tiep
   (xem uk_bridge_set_direct_mode). De NULL neu khong ho tro.

   LUU Y: chi bat DIRECT sau khi front-end xac nhan client thuc su chap nhan
   thao tac xoa. IBus phai handshake surrounding-text va loai VTE khoi DIRECT;
   neu khong dam bao duoc thi dung PREEDIT. Khong tron phim BackSpace gia voi
   callback commit, vi hai hang doi co the dao thu tu; all-forward neu co phai
   la mot backend rieng. */
typedef struct {
    void (*commit)(void *user_data, const char *utf8);
    void (*preedit_changed)(void *user_data, const char *utf8);
    void (*erase_before_cursor)(void *user_data, int nchars);
} UkBridgeVTable;

typedef enum {
    UK_BRIDGE_PASS = 0,   /* khong xu ly, de app tu lo phim nay */
    UK_BRIDGE_CONSUMED    /* da xu ly xong */
} UkBridgeResult;

/* Khoi tao engine dung chung cho ca tien trinh. Goi nhieu lan vo hai. */
void uk_bridge_global_init(void);

UkBridge *uk_bridge_new(const UkBridgeVTable *vt, void *user_data);
void      uk_bridge_free(UkBridge *b);

/* Xu ly mot ky tu. `unicode` la ma Unicode cua phim (0 neu khong phai ky tu). */
UkBridgeResult uk_bridge_key(UkBridge *b, unsigned int unicode,
                             int shift_pressed, int capslock_on);

/* Phim BackSpace. Tra ve PASS neu preedit rong (de app tu xoa van ban that). */
UkBridgeResult uk_bridge_backspace(UkBridge *b);

void        uk_bridge_flush(UkBridge *b);   /* commit preedit dang co */
void        uk_bridge_reset(UkBridge *b);   /* bo preedit, reset engine */
const char *uk_bridge_preedit(UkBridge *b); /* luon khac NULL */

/* Mac dinh bridge xoa preedit truoc roi moi commit, phu hop voi cac module
   toolkit cu. Front-end IBus/Wayland phai lam nguoc lai: CommitText ket thuc
   composition mot cach nguyen tu; neu xoa truoc, Chromium/xterm.js co the tu
   chot composition cu roi lai nhan them CommitText, thanh lap nguyen tu. */
void uk_bridge_set_commit_before_preedit_clear(UkBridge *b, int on);

/*----------------------------------------------------------------
  Hai che do lam viec
 ----------------------------------------------------------------

  PREEDIT (mac dinh): am tiet dang go nam trong preedit, chua vao van ban that.
  Cach trang tri do front-end quyet dinh; IBus terminal hien no nhu chu thuong.

  DIRECT: commit thang tung ky tu, sua lai bang cach xoa van ban da commit
  (erase_before_cursor). Giong het UniKey tren Windows: khong gach chan, khong
  trang thai tam.

  Front-end chi duoc bat DIRECT sau khi da xac nhan erase_before_cursor thuc su
  hoat dong. Co callback khong co nghia la client se chap nhan lenh xoa.

  Doi che do se chot phan dang go truoc.
*/
void uk_bridge_set_direct_mode(UkBridge *b, int on);
int  uk_bridge_get_direct_mode(UkBridge *b);

/* Cach xu ly client khai bao input-purpose TERMINAL: UkTerminalOff hay
   UkTerminalPreedit. Doc tu ~/.unikey/options, mac dinh Off. Client khong khai
   purpose (vi du terminal VS Code) duoc IBus giu o PREEDIT an toan. */
int  uk_bridge_get_terminal_mode(UkBridge *b);

int  uk_bridge_get_enabled(UkBridge *b);
void uk_bridge_set_enabled(UkBridge *b, int on);
void uk_bridge_toggle(UkBridge *b);

/* im: UkTelex | UkVni | UkViqr | UkUsrIM (xem keycons.h) */
void uk_bridge_set_input_method(UkBridge *b, int im);
int  uk_bridge_get_input_method(UkBridge *b);

void uk_bridge_set_single_mode(UkBridge *b);        /* CTRL-SHIFT-Z */
void uk_bridge_restore_keystrokes(UkBridge *b);     /* CTRL-SHIFT-ESC */

/* Thuc thi mot UkKeyAction do uk_keys_shortcut() nhan dien (xem ukkeys.h).
   Tra ve 1 neu da nuot phim. Moi front-end goi ham nay, khong tu viet switch. */
int  uk_bridge_apply_action(UkBridge *b, int action);

/* Nap ~/.unikey/options (kieu go, spell-check, macro...). */
void uk_bridge_load_config(UkBridge *b);

/*----------------------------------------------------------------
  Trang thai dung chung giua cac tien trinh (~/.unikey/state)
 ----------------------------------------------------------------

  Moi ung dung nap module deu co mot ban engine rieng. De bat/tat tieng Viet
  o app nay co hieu luc o app kia -- giong fcitx5 -- trang thai duoc ghi ra
  ~/.unikey/state va theo doi bang inotify. Khong can daemon, khong them
  phu thuoc: inotify la loi Linux.

  Front-end phai gan fd nay vao vong lap su kien cua no:
    GLib: g_unix_fd_add(uk_bridge_state_fd(b), G_IO_IN, cb, b)
    Qt:   QSocketNotifier(uk_bridge_state_fd(b), QSocketNotifier::Read)
  roi goi uk_bridge_state_dispatch() moi khi fd co du lieu.
*/

/* -1 neu khong theo doi duoc (khong co inotify, khong tao duoc ~/.unikey). */
int uk_bridge_state_fd(UkBridge *b);

/* Doc het su kien inotify va ap trang thai moi.
   Tra ve 1 neu trang thai that su thay doi. */
int uk_bridge_state_dispatch(UkBridge *b);

#if defined(__cplusplus)
}
#endif

#endif
