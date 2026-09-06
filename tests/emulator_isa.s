# Instruction shapes the public tests never reach: the logic and shift groups,
# xchg, a signed bgt, and a [%reg + <literal>] operand. The register dump the
# emulator prints at halt is the assertion. Everything but r1, r14 and r15 is
# address independent and must come out as:
#
#   r2=0x00000300  r3=0xffffffff  r4=0x00000000  r5=0xffffffff  r6=0x0f0f0f0f
#   r7=0x00000004  r8=0x00000010  r9=0x00000010  r10=0x000000a5 r11=0x0000002a
#   r12=0x0000003a r13=0x0000002a

.section test_code
start:
    ld $stack_top, %sp

# the logic group, over two literals too wide for a displacement
    ld $0xF0F0F0F0, %r1
    ld $0x0F0F0F0F, %r2
    ld %r1, %r3
    or %r2, %r3
    ld %r1, %r4
    and %r2, %r4
    ld %r1, %r5
    xor %r2, %r5
    ld %r1, %r6
    not %r6

# the shift group
    ld $4, %r7
    ld $1, %r8
    shl %r7, %r8
    ld $0x100, %r9
    shr %r7, %r9

# an arithmetic pair, then a swap of the two registers holding the results
    ld $6, %r10
    ld $7, %r11
    mul %r10, %r11
    ld $100, %r12
    ld $58, %r13
    sub %r13, %r12
    xchg %r12, %r13

# bgt is the one comparison the ISA defines as signed, so -1 is not greater
# than 1 and the branch must fall through
    ld $0, %r10
    ld $0xFFFFFFFF, %r1
    ld $1, %r2
    bgt %r1, %r2, compared_unsigned
    ld $0xA5, %r10
compared_unsigned:

# a word reached through [%reg + <literal>], then a stack round trip
    ld $numbers, %r1
    ld [%r1 + 8], %r2
    push %r2
    ld $0, %r2
    pop %r2
    halt

.section test_data
numbers:
.word 0x100, 0x200, 0x300
.skip 64
stack_top:

.end
