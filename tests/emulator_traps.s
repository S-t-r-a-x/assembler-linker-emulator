# The two synchronous ways into the handler: the int instruction, and an opcode
# the processor has no interpretation for. Both push the pc of the instruction
# after them, so iret resumes past the fault rather than repeating it.
# Expected output on the terminal: 41

.equ term_out, 0xFFFFFF00

.section trap_code
start:
    ld $stack_top, %sp
    ld $handler, %r1
    csrwr %r1, %handler

    int                     # cause 4
    .word 0xAAAAAAAA        # OC 0xA does not exist, so cause 1

    ld $10, %r1
    st %r1, term_out        # newline, so the state dump starts on its own line
    halt

handler:
    push %r1
    push %r2
    csrrd %cause, %r1
    ld $48, %r2             # ascii '0'
    add %r2, %r1
    st %r1, term_out
    pop %r2
    pop %r1
    iret

.section trap_data
.skip 64
stack_top:

.end
