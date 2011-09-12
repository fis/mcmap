(define (tell str)
  (packet-inject 'client (make-packet 'chat #:message str)))

(define (chat str)
  (packet-inject 'server (make-packet 'chat #:message str)))

(define-syntax on-packet
  (syntax-rules ()
    ((on-packet origin (type binding) body ...)
     (add-hook!
      (packet-hook 'type)
      (lambda (from binding)
        (if (or (eq? 'origin 'any) (eq? from 'origin))
            (begin body ...)))))))
