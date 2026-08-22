# x-unikey profile integration for interactive zsh terminals.
# Source this file once from ~/.zshrc.

[[ -o interactive ]] || return 0
(( $+commands[unikey-profile] )) || return 0
[[ -z ${_X_UNIKEY_PROFILE_LOADED-} ]] || return 0
typeset -g _X_UNIKEY_PROFILE_LOADED=1

function _x_unikey_profile_emit() {
  command unikey-profile zsh "$1" >/dev/null 2>&1
}

function _x_unikey_profile_focus_in() {
  _x_unikey_profile_emit on
}

function _x_unikey_profile_focus_out() {
  _x_unikey_profile_emit off
}

# Chi bat focus reporting trong luc ZLE dang doc command line. Tat truoc khi
# chay command de CSI focus khong lot vao stdin cua chuong trinh con.
function _x_unikey_profile_line_init() {
  printf '\e[?1004h'
  _x_unikey_profile_emit probe
}

function _x_unikey_profile_line_finish() {
  printf '\e[?1004l'
}

autoload -Uz add-zle-hook-widget
zle -N _x_unikey_profile_focus_in
zle -N _x_unikey_profile_focus_out
add-zle-hook-widget line-init _x_unikey_profile_line_init
add-zle-hook-widget line-finish _x_unikey_profile_line_finish

for _x_unikey_keymap in emacs viins vicmd; do
  bindkey -M "$_x_unikey_keymap" $'\e[I' _x_unikey_profile_focus_in \
    2>/dev/null || true
  bindkey -M "$_x_unikey_keymap" $'\e[O' _x_unikey_profile_focus_out \
    2>/dev/null || true
done
unset _x_unikey_keymap
