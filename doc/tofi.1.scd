tofi(1)

# NAME

tofi - Tiny dynamic menu for Wayland, inspired by *rofi*(1) and *dmenu*(1).

# SYNOPSIS

*tofi* [options...]

*tofi-run* [options...]

*tofi-drun* [options...]

# DESCRIPTION

*tofi* is a tiny dynamic menu for Wayland compositors supporting the
layer-shell protocol. It reads newline-separated items from stdin, and displays
a graphical selection menu. When a selection is made, it is printed to stdout.

When invoked via the name *tofi-run*, *tofi* will not accept items on stdin,
instead presenting a list of executables in the user's $PATH.

When invoked via the name *tofi-drun*, *tofi* will not accept items on stdin,
and will generate a list of applications from desktop files as described in the
Desktop Entry Specification.

# OPTIONS

*-h, --help*
	Print help and exit.

*-c, --config* <path>
	Specify path to custom config file.

All config file options described in *tofi*(5) are also accepted, in the form
*--key=value*.

# KEYS

<Ctrl>-a
	Select all input text.

<Shift>-<Left> | <Shift>-<Right>
	Extend or shrink the input text selection.

<Ctrl>-<Left> | <Ctrl>-<Right>
	Move the text cursor one word.

<Ctrl>-<Shift>-<Left> | <Ctrl>-<Shift>-<Right>
	Extend or shrink the input text selection by one word.

<Ctrl>-c
	Copy selected input text. If no input text is selected, quit without making
	a selection.

<Ctrl>-x
	Cut selected input text.

<Ctrl>-v
	Paste clipboard text.

<Ctrl>-<Insert> | <Shift>-<Insert> | <Shift>-<Delete>
	Copy, paste, or cut selected input text, respectively.

<Home> | <End>
	Move the text cursor to the start or end of the input.

<Shift>-<Home> | <Shift>-<End>
	Extend the input text selection to the start or end of the input.

<Up> | <Left> | <Ctrl>-k | <Ctrl>-p | <Ctrl>-b | <Alt>-k | <Alt>-p | <Alt>-h |
<Shift>-<Tab>
	Move the selection back one entry.

<Down> | <Right> | <Ctrl>-j | <Ctrl>-n | <Ctrl>-f | <Alt>-j | <Alt>-n  | <Alt>-l |
<Tab>
	Move the selection forward one entry.

<Page Up>
	Move the selection back one page.

<Page Down>
	Move the selection forward one page.

<Backspace> | <Ctrl>-h
	Delete character.

<Delete>
	Delete the next character.

<Ctrl>-u
	Delete line.

<Ctrl>-w | <Ctrl>-<Backspace>
	Delete word.

<Ctrl>-<Delete>
	Delete the next word.

<Enter> | <Ctrl>-m
	Confirm the current selection and quit.

<Escape> | <Ctrl>-g | <Ctrl>-[
	Quit without making a selection.

# FILES

_/etc/xdg/tofi/config_
	Example configuration file.

_$XDG_CONFIG_HOME/tofi/config_
	The default configuration file location.

_$XDG_CACHE_HOME/tofi-compgen_
	Cached list of executables under $PATH, regenerated as necessary.

_$XDG_CACHE_HOME/tofi-drun_
	Cached list of desktop applications, regenerated as necessary.

_$XDG_STATE_HOME/tofi-history_
	Numeric count of commands selected in *tofi-run*, to enable sorting
	results by run count.

_$XDG_STATE_HOME/tofi-drun-history_
	Numeric count of commands selected in *tofi-drun*, to enable sorting
	results by run count.

# EXIT STATUS

*tofi* exits with one of the following values:

0
	Success; a selection was made, or *tofi* was invoked with the *-h*
	option.

1
	An error occurred, or the user exited without making a selection.

# AUTHORS

Philip Jones <philj56@gmail.com>

# SEE ALSO

*tofi*(5), *dmenu*(1), *rofi*(1)
