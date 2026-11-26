# One half of a library section that is aggregated by the linker, not the assembler.

.global addTwo

.section lib
addTwo:
    ld $2, %r2
    add %r2, %r1
    ret

.end
