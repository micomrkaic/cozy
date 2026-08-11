;;; cozy-mode.el --- Major mode for the Cozy language -*- lexical-binding: t; -*-

;; Author: Mico Mrkaic and Claude
;; Keywords: languages
;; URL: https://github.com/micomrkaic/cozy
;; Package-Requires: ((emacs "26.1"))
;; SPDX-License-Identifier: GPL-3.0-or-later

;;; Commentary:
;; Editing support for Cozy (.cz): syntax highlighting, block-aware
;; indentation, and an inferior REPL.
;;
;; Install: put this file on your `load-path' and add to your init file:
;;   (require 'cozy-mode)
;; Files ending in .cz open in `cozy-mode' automatically.
;;
;; REPL: M-x run-cozy, then from a .cz buffer:
;;   C-c C-r  send region      C-c C-b  send buffer
;;   C-c C-l  load this file   C-c C-z  switch to the REPL
;;
;; The builtin-name list below is generated from the interpreter's own
;; documentation table by tools/gen_emacs_mode.py, so highlighting cannot
;; drift from the language.

;;; Code:

(require 'comint)

(defgroup cozy nil
  "Editing Cozy code."
  :group 'languages)

(defcustom cozy-indent-offset 2
  "Indentation per block or bracket level in Cozy."
  :type 'integer :group 'cozy)

(defcustom cozy-program "cozy"
  "Program name for the inferior Cozy REPL."
  :type 'string :group 'cozy)

;; ---------------------------------------------------------------------
;; Syntax
;; ---------------------------------------------------------------------
(defvar cozy-mode-syntax-table
  (let ((st (make-syntax-table)))
    (modify-syntax-entry ?% "<" st)     ; % comments to end of line
    (modify-syntax-entry ?\n ">" st)
    (modify-syntax-entry ?\" "\"" st)   ; double-quoted strings
    (modify-syntax-entry ?\\ "\\" st)
    (modify-syntax-entry ?' "." st)     ; transpose operator, NOT a quote
    (modify-syntax-entry ?_ "_" st)
    st)
  "Syntax table for `cozy-mode'.")

(defconst cozy--keywords
  '("let" "fn" "if" "then" "else" "elseif" "end" "for" "while" "where" "do" "in"
    "return" "break" "continue")
  "Cozy reserved words.")

(defconst cozy--constants
  '("true" "false" "null" "ans")
  "Cozy literal words and the REPL's `ans'.")

(defconst cozy--builtins
  '("abs"
    "acos"
    "acosh"
    "all"
    "angle"
    "any"
    "arg"
    "asin"
    "asinh"
    "assert"
    "ast"
    "atan"
    "atan2"
    "atanh"
    "besselj"
    "bessely"
    "beta"
    "betainc"
    "body"
    "buildinfo"
    "cbrt"
    "cd"
    "ceil"
    "chol"
    "clear"
    "conj"
    "contains"
    "corr"
    "cos"
    "cosh"
    "cov"
    "cumprod"
    "cumsum"
    "dense"
    "det"
    "diag"
    "diff"
    "digamma"
    "dis"
    "dot"
    "dual"
    "dualeps"
    "dualval"
    "e"
    "eig"
    "endswith"
    "eps"
    "erf"
    "erfc"
    "error"
    "eulergamma"
    "eval"
    "exit"
    "exp"
    "eye"
    "fields"
    "find"
    "fliplr"
    "flipud"
    "floor"
    "fminbnd"
    "fmt"
    "format"
    "fzero"
    "gamma"
    "gammainc"
    "getfield"
    "hdual"
    "hdual12"
    "hdualval"
    "help"
    "hist"
    "hypot"
    "imag"
    "inf"
    "input"
    "integral"
    "inv"
    "isfinite"
    "isinf"
    "isnan"
    "keep"
    "kron"
    "lbeta"
    "length"
    "lgamma"
    "linspace"
    "ln"
    "load"
    "log"
    "log10"
    "log2"
    "lower"
    "ls"
    "lu"
    "manual"
    "map"
    "max"
    "mean"
    "median"
    "mem"
    "min"
    "mod"
    "more"
    "names"
    "nan"
    "nnz"
    "norm"
    "norminv"
    "now"
    "num"
    "numel"
    "ones"
    "pause"
    "phi"
    "pi"
    "pick"
    "plot"
    "pretty"
    "print"
    "prod"
    "pwd"
    "qr"
    "quantile"
    "rand"
    "randi"
    "randn"
    "readcsv"
    "readtable"
    "real"
    "rem"
    "repmat"
    "reshape"
    "rng"
    "round"
    "save"
    "setfield"
    "sign"
    "sin"
    "sinh"
    "size"
    "sort"
    "sparse"
    "speye"
    "sprand"
    "sprandn"
    "sqrt"
    "startswith"
    "std"
    "str"
    "strfind"
    "strjoin"
    "strrep"
    "strsplit"
    "sum"
    "svd"
    "system"
    "tan"
    "tanh"
    "tic"
    "toc"
    "trace"
    "trim"
    "trunc"
    "unique"
    "upper"
    "var"
    "version"
    "who"
    "whof"
    "whor"
    "whos"
    "whov"
    "writecsv"
    "zeros")
  "Builtin names, generated from eval.c — do not edit by hand.")

(defvar cozy-font-lock-keywords
  `((,(concat "\\_<let[ \t]+\\([A-Za-z_][A-Za-z0-9_]*\\)[ \t]*=[ \t]*fn\\_>")
     (1 font-lock-function-name-face))
    (,(concat "\\_<let[ \t]+\\([A-Za-z_][A-Za-z0-9_]*\\)")
     (1 font-lock-variable-name-face))
    (,(regexp-opt cozy--keywords 'symbols) . font-lock-keyword-face)
    (,(regexp-opt cozy--constants 'symbols) . font-lock-constant-face)
    (,(regexp-opt cozy--builtins 'symbols) . font-lock-builtin-face))
  "Font-lock rules for `cozy-mode'.")

;; ---------------------------------------------------------------------
;; Indentation: bracket depth (via the parser state) plus block keywords
;; (if/for/while ... end). fn bodies are parenthesized in idiomatic
;; Cozy, so bracket depth carries most of the weight.
;; ---------------------------------------------------------------------
(defconst cozy--block-open-re
  "\\_<\\(if\\|for\\|while\\)\\_>")
(defconst cozy--block-close-re
  "\\_<end\\_>")

(defun cozy--block-depth-before (pos)
  "Block-keyword depth (opens minus ends) in code from buffer start to POS."
  (save-excursion
    (goto-char (point-min))
    (let ((depth 0))
      (while (re-search-forward
              "\\_<\\(?:if\\|for\\|while\\|end\\)\\_>" pos t)
        (let ((state (syntax-ppss)))
          (unless (or (nth 3 state) (nth 4 state))  ; not in string/comment
            (if (string= (match-string 0) "end")
                (setq depth (max 0 (1- depth)))
              (setq depth (1+ depth))))))
      depth)))

(defun cozy-indent-line ()
  "Indent the current line of Cozy code."
  (interactive)
  (let* ((bol (save-excursion (beginning-of-line) (point)))
         (paren-depth (car (syntax-ppss bol)))
         (block-depth (cozy--block-depth-before bol))
         (dedent (save-excursion
                   (beginning-of-line)
                   (skip-chars-forward " \t")
                   (if (looking-at "\\_<\\(end\\|else\\)\\_>\\|[])}]") 1 0)))
         (target (* cozy-indent-offset
                    (max 0 (- (+ paren-depth block-depth) dedent)))))
    (if (<= (current-column) (current-indentation))
        (indent-line-to target)
      (save-excursion (indent-line-to target)))))

;; ---------------------------------------------------------------------
;; Inferior REPL
;; ---------------------------------------------------------------------
(defvar cozy-repl-buffer "*cozy*")

;;;###autoload
(defun run-cozy ()
  "Start (or switch to) an inferior Cozy REPL."
  (interactive)
  (unless (comint-check-proc cozy-repl-buffer)
    (with-current-buffer (make-comint "cozy" cozy-program)
      (setq-local comint-prompt-regexp "^cozy> *")
      (setq-local comint-prompt-read-only t)))
  (pop-to-buffer cozy-repl-buffer))

(defun cozy--send (text)
  (run-cozy)
  (with-current-buffer cozy-repl-buffer
    (comint-send-string (get-buffer-process (current-buffer))
                        (concat text "\n"))))

(defun cozy-send-region (beg end)
  "Send the region to the Cozy REPL."
  (interactive "r")
  (cozy--send (buffer-substring-no-properties beg end)))

(defun cozy-send-buffer ()
  "Send the whole buffer to the Cozy REPL."
  (interactive)
  (cozy-send-region (point-min) (point-max)))

(defun cozy-load-file ()
  "Load the current file in the Cozy REPL (saves first)."
  (interactive)
  (save-buffer)
  (cozy--send (format "load(%S)" (buffer-file-name))))

(defun cozy-switch-to-repl ()
  "Switch to the Cozy REPL buffer."
  (interactive)
  (run-cozy))

;; ---------------------------------------------------------------------
;; The mode
;; ---------------------------------------------------------------------
(defvar cozy-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-r") #'cozy-send-region)
    (define-key map (kbd "C-c C-b") #'cozy-send-buffer)
    (define-key map (kbd "C-c C-l") #'cozy-load-file)
    (define-key map (kbd "C-c C-z") #'cozy-switch-to-repl)
    map)
  "Keymap for `cozy-mode'.")

;;;###autoload
(define-derived-mode cozy-mode prog-mode "Cozy"
  "Major mode for editing Cozy code."
  :syntax-table cozy-mode-syntax-table
  (setq-local comment-start "% ")
  (setq-local comment-start-skip "%+[ \t]*")
  (setq-local font-lock-defaults '(cozy-font-lock-keywords))
  (setq-local indent-line-function #'cozy-indent-line))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.cz\\'" . cozy-mode))

(provide 'cozy-mode)
;;; cozy-mode.el ends here
