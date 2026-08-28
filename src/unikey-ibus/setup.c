// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey Preferences
 *
 * Cua so nay chi duoc mo khi nguoi dung bam Preferences cua IBus. No khong
 * chay ngam, khong co tray icon va khong khoi dong cung desktop.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ukopt.h"

typedef struct {
    UkXimOpt opt;
    char *config_dir;
    char *options_path;
    char *state_path;

    GtkWidget *dialog;
    GtkComboBoxText *input_combo;
    GtkToggleButton *free_style;
    GtkToggleButton *modern_style;
    GtkToggleButton *spell_check;
    GtkToggleButton *auto_restore;
    GtkEntry *macro_file;
    GtkEntry *keymap_file;
} SetupApp;

static gboolean KeymapValidatorInitialized = FALSE;

static void free_option_strings(UkXimOpt *opt)
{
    free(opt->macroFile);
    free(opt->usrKeyMapFile);
    opt->macroFile = NULL;
    opt->usrKeyMapFile = NULL;
}

static void app_clear(SetupApp *app)
{
    free_option_strings(&app->opt);
    g_free(app->config_dir);
    g_free(app->options_path);
    g_free(app->state_path);
    if (KeymapValidatorInitialized) {
        UnikeyCleanup();
        KeymapValidatorInitialized = FALSE;
    }
}

static gboolean app_init_paths(SetupApp *app, GError **error)
{
    const char *home = g_get_home_dir();

    if (!home || !*home) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOENT,
                            "Không xác định được thư mục HOME");
        return FALSE;
    }

    app->config_dir = g_build_filename(home, ".unikey", NULL);
    app->options_path = g_build_filename(app->config_dir, "options", NULL);
    app->state_path = g_build_filename(app->config_dir, "state", NULL);
    return TRUE;
}

static gboolean state_parse_int(const char *text, int *value)
{
    char *end;
    long parsed;

    if (!text || !value)
        return FALSE;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || parsed < INT_MIN || parsed > INT_MAX)
        return FALSE;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end)
        return FALSE;
    *value = (int)parsed;
    return TRUE;
}

static void state_read_values(const char *path, int *enabled, int *method)
{
    FILE *f;
    char line[128];
    int value;

    f = fopen(path, "r");
    if (!f)
        return;

    while (fgets(line, sizeof(line), f)) {
        if (enabled && strncmp(line, "enabled=", 8) == 0 &&
            state_parse_int(line + 8, &value) &&
            (value == 0 || value == 1))
            *enabled = value;
        else if (method && strncmp(line, "method=", 7) == 0 &&
                 state_parse_int(line + 7, &value)) {
            if (value == UkTelex || value == UkVni || value == UkViqr ||
                value == UkUsrIM)
                *method = value;
        }
    }
    fclose(f);
}

/* Nap default truoc, sau do de file cua nguoi dung ghi de. Khong goi
 * UkTestDefConfFile(): --check phai hoan toan headless va khong tao file. */
static gboolean app_load_options(SetupApp *app, GError **error)
{
    UkSetDefOptions(&app->opt);

    if (g_file_test(app->options_path, G_FILE_TEST_EXISTS) &&
        !UkParseOptFile(app->options_path, &app->opt)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "Không đọc được %s", app->options_path);
        return FALSE;
    }

    /* ~/.unikey/state la nguon live cho cong tac va kieu go, nen no phai
       duoc hien trong Preferences thay vi gia tri Input co the da cu. */
    state_read_values(app->state_path, &app->opt.enabled,
                      &app->opt.inputMethod);
    return TRUE;
}

static char *expand_home_path(const char *path)
{
    if (!path)
        return g_strdup("");
    if (path[0] == '~' && path[1] == '/')
        return g_build_filename(g_get_home_dir(), path + 2, NULL);
    return g_strdup(path);
}

static gboolean is_readable_regular_file(const char *path)
{
    return path && *path &&
           g_file_test(path, G_FILE_TEST_IS_REGULAR) &&
           g_access(path, R_OK) == 0;
}

static gboolean path_round_trips_config(const char *path)
{
    size_t len;

    if (!path || !*path)
        return TRUE;
    len = strlen(path);
    return path[0] != ' ' && path[0] != '\t' &&
           path[len - 1] != ' ' && path[len - 1] != '\t' &&
           strchr(path, '#') == NULL && strchr(path, '\n') == NULL &&
           strchr(path, '\r') == NULL;
}

static gboolean user_keymap_is_valid(const char *path)
{
    if (!KeymapValidatorInitialized) {
        UnikeySetup();
        KeymapValidatorInitialized = TRUE;
    }
    return UnikeyLoadUserKeyMap(path) != 0;
}

static void set_file_error(GError **error, const char *format,
                           const char *path)
{
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, format,
                (path && *path) ? path : "(trống)");
}

static gboolean validate_controls(SetupApp *app, GError **error)
{
    const char *input = gtk_combo_box_get_active_id(
        GTK_COMBO_BOX(app->input_combo));
    const char *macro = gtk_entry_get_text(app->macro_file);
    const char *keymap = gtk_entry_get_text(app->keymap_file);
    char *expanded_macro = expand_home_path(macro);
    char *expanded_keymap = expand_home_path(keymap);
    gboolean ok = TRUE;

    if (!path_round_trips_config(macro) ||
        !path_round_trips_config(keymap)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                            "Đường dẫn không được chứa #, xuống dòng, hoặc "
                            "khoảng trắng ở hai đầu");
        ok = FALSE;
    } else if (macro && *macro && !is_readable_regular_file(expanded_macro)) {
        set_file_error(error,
                       "Tệp macro không tồn tại hoặc không đọc được: %s",
                       macro);
        ok = FALSE;
    } else if (g_strcmp0(input, "USER") == 0) {
        if (!is_readable_regular_file(expanded_keymap)) {
            set_file_error(error,
                           "Kiểu gõ Tự định nghĩa cần một tệp keymap đọc được: %s",
                           keymap);
            ok = FALSE;
        } else if (!user_keymap_is_valid(expanded_keymap)) {
            set_file_error(error,
                           "Tệp keymap không đúng cú pháp UniKey: %s",
                           keymap);
            ok = FALSE;
        }
    }

    g_free(expanded_macro);
    g_free(expanded_keymap);
    return ok;
}

static int input_method_from_id(const char *id)
{
    if (g_strcmp0(id, "VNI") == 0)
        return UkVni;
    if (g_strcmp0(id, "VIQR") == 0)
        return UkViqr;
    if (g_strcmp0(id, "USER") == 0)
        return UkUsrIM;
    return UkTelex;
}

static const char *input_method_id(int method)
{
    switch ((UkInputMethod)method) {
    case UkVni:   return "VNI";
    case UkViqr:  return "VIQR";
    case UkUsrIM: return "USER";
    case UkTelex:
    default:      return "TELEX";
    }
}

static gboolean collect_controls(SetupApp *app, GError **error)
{
    const char *input = gtk_combo_box_get_active_id(
        GTK_COMBO_BOX(app->input_combo));
    const char *macro = gtk_entry_get_text(app->macro_file);
    const char *keymap = gtk_entry_get_text(app->keymap_file);
    char *new_macro = NULL;
    char *new_keymap = NULL;

    if (macro && *macro) {
        new_macro = strdup(macro);
        if (!new_macro)
            goto no_memory;
    }
    if (keymap && *keymap) {
        new_keymap = strdup(keymap);
        if (!new_keymap)
            goto no_memory;
    }

    app->opt.inputMethod = input_method_from_id(input);
    /* IBus policy moi luon tat bo go trong terminal. Giu truong options o
       gia tri OFF de file cu van doc duoc ma UI khong ghi lai PREEDIT. */
    app->opt.terminalMode = UkTerminalOff;
    app->opt.uk.freeMarking = gtk_toggle_button_get_active(app->free_style);
    app->opt.uk.modernStyle = gtk_toggle_button_get_active(app->modern_style);
    app->opt.uk.spellCheckEnabled =
        gtk_toggle_button_get_active(app->spell_check);
    app->opt.uk.autoNonVnRestore =
        gtk_toggle_button_get_active(app->auto_restore);

    free(app->opt.macroFile);
    free(app->opt.usrKeyMapFile);
    app->opt.macroFile = new_macro;
    app->opt.usrKeyMapFile = new_keymap;
    return TRUE;

no_memory:
    free(new_macro);
    free(new_keymap);
    g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
                        "Không đủ bộ nhớ để lưu cấu hình");
    return FALSE;
}

/* Ghi file tam trong cung thu muc, fsync, roi rename. Nhu vay cac engine dang
 * theo doi bang inotify khong bao gio doc phai nua dong state. */
static gboolean state_write_atomic(const char *path, int enabled, int method,
                                   GError **error)
{
    char *tmp = g_strdup_printf("%s.tmp.XXXXXX", path);
    int fd = g_mkstemp(tmp);
    FILE *f;
    int saved_errno;

    if (fd < 0) {
        saved_errno = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    "Không tạo được tệp trạng thái tạm: %s",
                    g_strerror(saved_errno));
        g_free(tmp);
        return FALSE;
    }

    (void)fchmod(fd, S_IRUSR | S_IWUSR);
    f = fdopen(fd, "w");
    if (!f) {
        saved_errno = errno;
        close(fd);
        g_unlink(tmp);
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    "Không mở được tệp trạng thái tạm: %s",
                    g_strerror(saved_errno));
        g_free(tmp);
        return FALSE;
    }

    if (fprintf(f, "enabled=%d\nmethod=%d\n", enabled ? 1 : 0, method) < 0 ||
        fflush(f) != 0 || fsync(fileno(f)) != 0) {
        saved_errno = errno;
        fclose(f);
        g_unlink(tmp);
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    "Không ghi được trạng thái UniKey: %s",
                    g_strerror(saved_errno));
        g_free(tmp);
        return FALSE;
    }

    if (fclose(f) != 0) {
        saved_errno = errno;
        g_unlink(tmp);
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    "Không đóng được tệp trạng thái: %s",
                    g_strerror(saved_errno));
        g_free(tmp);
        return FALSE;
    }

    if (g_rename(tmp, path) != 0) {
        saved_errno = errno;
        g_unlink(tmp);
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    "Không cập nhật được %s: %s", path,
                    g_strerror(saved_errno));
        g_free(tmp);
        return FALSE;
    }

    /* Rename da nguyen tu; fsync thu muc la best-effort de tang do ben sau
       mat dien ma khong bien mot save da ap dung thanh loi gia. */
    {
        char *dir = g_path_get_dirname(path);
        int dir_fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd >= 0) {
            (void)fsync(dir_fd);
            (void)close(dir_fd);
        }
        g_free(dir);
    }

    g_free(tmp);
    return TRUE;
}

static gboolean save_configuration(SetupApp *app, GError **error)
{
    int old_enabled, old_method;
    GError *rollback_error = NULL;

    if (!validate_controls(app, error))
        return FALSE;

    if (g_mkdir_with_parents(app->config_dir, 0700) != 0) {
        int saved_errno = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    "Không tạo được %s: %s", app->config_dir,
                    g_strerror(saved_errno));
        return FALSE;
    }

    old_enabled = app->opt.enabled ? 1 : 0;
    old_method = app->opt.inputMethod;
    state_read_values(app->state_path, &old_enabled, &old_method);

    if (!collect_controls(app, error))
        return FALSE;

    /* Cap nhat state truoc de method co hieu luc live. Neu options that bai,
       dua state ve snapshot cu de UI khong bao loi trong khi bo go lai o mot
       trang thai nua moi nua cu. */
    if (!state_write_atomic(app->state_path, old_enabled,
                            (int)app->opt.inputMethod, error))
        return FALSE;

    if (!UkWriteOptFileAtomic(app->options_path, &app->opt)) {
        if (!state_write_atomic(app->state_path, old_enabled, old_method,
                                &rollback_error)) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                        "Không ghi được %s; đồng thời không khôi phục được "
                        "trạng thái cũ: %s", app->options_path,
                        rollback_error ? rollback_error->message : "lỗi không rõ");
            g_clear_error(&rollback_error);
        } else {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                        "Không ghi được %s; trạng thái đang chạy đã được "
                        "khôi phục", app->options_path);
        }
        return FALSE;
    }

    return TRUE;
}

static void show_error(GtkWindow *parent, const GError *error)
{
    GtkWidget *dialog = gtk_message_dialog_new(
        parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
        "Không lưu được cấu hình");

    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "%s", error->message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void browse_file(GtkButton *button, gpointer user_data)
{
    GtkEntry *entry = GTK_ENTRY(user_data);
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    GtkWidget *chooser = gtk_file_chooser_dialog_new(
        "Chọn tệp", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Hủy", GTK_RESPONSE_CANCEL,
        "_Chọn", GTK_RESPONSE_ACCEPT,
        NULL);
    const char *current = gtk_entry_get_text(entry);

    if (current && *current) {
        char *expanded = expand_home_path(current);
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), expanded);
        g_free(expanded);
    }

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        gtk_entry_set_text(entry, filename);
        g_free(filename);
    }
    gtk_widget_destroy(chooser);
}

static GtkWidget *file_entry_row(GtkGrid *grid, int row, const char *label_text,
                                 const char *value, GtkEntry **entry_out)
{
    GtkWidget *label = gtk_label_new_with_mnemonic(label_text);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry = gtk_entry_new();
    GtkWidget *button = gtk_button_new_with_label("Chọn…");

    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    gtk_entry_set_text(GTK_ENTRY(entry), value ? value : "");
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    gtk_grid_attach(grid, label, 0, row, 1, 1);
    gtk_grid_attach(grid, box, 1, row, 1, 1);
    g_signal_connect(button, "clicked", G_CALLBACK(browse_file), entry);

    *entry_out = GTK_ENTRY(entry);
    return box;
}

static GtkWidget *section_label(const char *text)
{
    GtkWidget *label = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped("<b>%s</b>", text);

    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    g_free(markup);
    return label;
}

static void build_dialog(SetupApp *app)
{
    GtkWidget *content;
    GtkWidget *outer;
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *mode_box;
    GtkWidget *mode_text;
    GtkWidget *file_grid;
    int row = 0;

    app->dialog = gtk_dialog_new_with_buttons(
        "Cấu hình UniKey", NULL,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Hủy", GTK_RESPONSE_CANCEL,
        "_Lưu", GTK_RESPONSE_APPLY,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(app->dialog), 590, -1);
    gtk_dialog_set_default_response(GTK_DIALOG(app->dialog), GTK_RESPONSE_APPLY);

    content = gtk_dialog_get_content_area(GTK_DIALOG(app->dialog));
    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 16);
    gtk_box_pack_start(GTK_BOX(content), outer, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(outer), section_label("Bộ gõ"), FALSE, FALSE, 0);
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(outer), grid, FALSE, FALSE, 0);

    label = gtk_label_new_with_mnemonic("_Kiểu gõ:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    app->input_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(app->input_combo, "TELEX", "Telex");
    gtk_combo_box_text_append(app->input_combo, "VNI", "VNI");
    gtk_combo_box_text_append(app->input_combo, "VIQR", "VIQR");
    gtk_combo_box_text_append(app->input_combo, "USER", "Tự định nghĩa");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->input_combo),
                                input_method_id((int)app->opt.inputMethod));
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), GTK_WIDGET(app->input_combo));
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->input_combo), 1, row++, 1, 1);

    label = gtk_label_new("Bảng mã:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    label = gtk_label_new("Unicode (UTF-8) — cố định cho IBus/Wayland");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(label),
                                GTK_STYLE_CLASS_DIM_LABEL);
    gtk_grid_attach(GTK_GRID(grid), label, 1, row++, 1, 1);

    label = gtk_label_new("Profile terminal / zsh:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    label = gtk_label_new("Tắt bộ gõ — tự động");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(label),
                                GTK_STYLE_CLASS_DIM_LABEL);
    gtk_grid_attach(GTK_GRID(grid), label, 1, row++, 1, 1);

    mode_box = gtk_frame_new("Chế độ xử lý: Tự động");
    mode_text = gtk_label_new(
        "Terminal dùng Off; address bar Firefox/Chromium dùng Direct; mọi ô "
        "nhập khác dùng Preedit/Predict. VS Code không khai purpose TERMINAL, "
        "nên zsh cần source /usr/share/x-unikey/profile-zsh.zsh trong ~/.zshrc.");
    gtk_label_set_line_wrap(GTK_LABEL(mode_text), TRUE);
    gtk_label_set_xalign(GTK_LABEL(mode_text), 0.0f);
    gtk_widget_set_margin_start(mode_text, 9);
    gtk_widget_set_margin_end(mode_text, 9);
    gtk_widget_set_margin_top(mode_text, 9);
    gtk_widget_set_margin_bottom(mode_text, 9);
    gtk_container_add(GTK_CONTAINER(mode_box), mode_text);
    gtk_box_pack_start(GTK_BOX(outer), mode_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(outer), section_label("Cách đặt dấu"),
                       FALSE, FALSE, 0);
    app->free_style = GTK_TOGGLE_BUTTON(
        gtk_check_button_new_with_label("Cho phép đặt dấu tự do"));
    app->modern_style = GTK_TOGGLE_BUTTON(
        gtk_check_button_new_with_label("Dùng kiểu đặt dấu hiện đại (oà, uý)"));
    app->spell_check = GTK_TOGGLE_BUTTON(
        gtk_check_button_new_with_label("Kiểm tra chính tả tiếng Việt"));
    app->auto_restore = GTK_TOGGLE_BUTTON(
        gtk_check_button_new_with_label("Tự hoàn nguyên phím với từ không phải tiếng Việt"));
    gtk_toggle_button_set_active(app->free_style, app->opt.uk.freeMarking != 0);
    gtk_toggle_button_set_active(app->modern_style, app->opt.uk.modernStyle != 0);
    gtk_toggle_button_set_active(app->spell_check,
                                 app->opt.uk.spellCheckEnabled != 0);
    gtk_toggle_button_set_active(app->auto_restore,
                                 app->opt.uk.autoNonVnRestore != 0);
    gtk_box_pack_start(GTK_BOX(outer), GTK_WIDGET(app->free_style),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), GTK_WIDGET(app->modern_style),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), GTK_WIDGET(app->spell_check),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), GTK_WIDGET(app->auto_restore),
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(outer), section_label("Macro và kiểu gõ riêng"),
                       FALSE, FALSE, 0);
    file_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(file_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(file_grid), 12);
    gtk_widget_set_hexpand(file_grid, TRUE);
    gtk_box_pack_start(GTK_BOX(outer), file_grid, FALSE, FALSE, 0);
    file_entry_row(GTK_GRID(file_grid), 0, "Tệp _macro:",
                   app->opt.macroFile, &app->macro_file);
    file_entry_row(GTK_GRID(file_grid), 1, "Tệp _keymap:",
                   app->opt.usrKeyMapFile, &app->keymap_file);

    label = gtk_label_new(
        "Để trống tệp macro nếu không dùng. Kiểu gõ Tự định nghĩa bắt buộc "
        "phải có tệp keymap hợp lệ. Kiểu gõ được áp dụng ngay; các tuỳ chọn "
        "còn lại có hiệu lực sau khi khởi động lại IBus hoặc ứng dụng.");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(label),
                                GTK_STYLE_CLASS_DIM_LABEL);
    gtk_box_pack_start(GTK_BOX(outer), label, FALSE, FALSE, 0);

    gtk_widget_show_all(app->dialog);
}

static int run_check(SetupApp *app)
{
    GError *error = NULL;

    if (!app_init_paths(app, &error) || !app_load_options(app, &error)) {
        g_printerr("ibus-setup-unikey: %s\n", error->message);
        g_clear_error(&error);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    SetupApp app;
    GError *error = NULL;
    int response;
    int status = 0;

    memset(&app, 0, sizeof(app));

    if (argc == 2 && strcmp(argv[1], "--check") == 0) {
        status = run_check(&app);
        app_clear(&app);
        return status;
    }

    if (!gtk_init_check(&argc, &argv)) {
        g_printerr("ibus-setup-unikey: không mở được màn hình đồ họa\n");
        return 1;
    }

    if (!app_init_paths(&app, &error) || !app_load_options(&app, &error)) {
        g_printerr("ibus-setup-unikey: %s\n", error->message);
        g_clear_error(&error);
        app_clear(&app);
        return 1;
    }

    build_dialog(&app);
    for (;;) {
        response = gtk_dialog_run(GTK_DIALOG(app.dialog));
        if (response != GTK_RESPONSE_APPLY)
            break;

        if (save_configuration(&app, &error))
            break;

        show_error(GTK_WINDOW(app.dialog), error);
        g_clear_error(&error);
    }

    gtk_widget_destroy(app.dialog);
    app_clear(&app);
    return status;
}
