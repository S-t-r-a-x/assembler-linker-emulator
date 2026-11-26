# The other half. Same section name, a separate file, so the two only meet at link time.

.global mulThree

.section lib
mulThree:
    ld $3, %r2
    mul %r2, %r1
    ret

.end
