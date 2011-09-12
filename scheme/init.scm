(define (tell str)
  (packet-inject 'client (make-packet 'chat #:message str)))

(define (chat str)
  (packet-inject 'server (make-packet 'chat #:message str)))
