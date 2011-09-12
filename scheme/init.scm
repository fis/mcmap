(define (tell str)
  (packet-inject 'client (make-packet 'chat #:text str)))

(define (chat str)
  (packet-inject 'server (make-packet 'chat #:text str)))
